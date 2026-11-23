/*
 * The port's own screens: first-start steps, F1 online, F2 options, the update
 * prompt, the Kratos download. They are Dear ImGui, a static copy inside
 * mk9.exe (not exported). rexruntimerd.dll exports the same ImGui build by
 * name, so each function we need is found in mk9.exe by matching the first
 * bytes of the DLL's copy (calls and RIP-relative operands wildcarded). The
 * struct offsets (NavId) are read from the DLL's ImGui::IsItemFocused.
 *
 * Render thread: ItemAdd(bb, id) starts an item; the AddText calls after it
 * belong to it (plain Text widgets add id 0); Render ends the frame and
 * publishes it. Mod thread (im_poll): speaks the focused item (NavId) when it
 * changes, and a new screen's plain text when the screen changes.
 */
#include "access.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "MinHook.h"

typedef struct { float x, y; } ImVec2;
typedef void (__cdecl *addtext_fn)(void *self, void *font, float size, const ImVec2 *pos, uint32_t col,
                                   const char *b, const char *e, float wrap, const void *clip);
typedef char (__cdecl *itemadd_fn)(const void *bb, uint32_t id, const void *nav_bb, int flags);
typedef void (__cdecl *render_fn)(void);
typedef void *(__cdecl *ctx_fn)(void);

static addtext_fn o_addtext, o_addtext_rt;
static itemadd_fn o_itemadd;
static render_fn o_render;
static ctx_fn p_ctx;
static void *t_addtext, *t_addtext_rt, *t_itemadd, *t_render;
static uint32_t g_off_nav = 0x1fbc;
static int g_ready;

#define IMT_MAX 256
#define IMT_LEN 240
typedef struct { char s[IMT_LEN]; uint32_t id; float x, y; uint32_t col; } ImT;
static ImT g_build[IMT_MAX], g_done[IMT_MAX];
static int g_nbuild, g_ndone;
static uint32_t g_cur_item;
static volatile LONG g_frame;
static volatile DWORD g_last_frame_t;
static CRITICAL_SECTION g_lock;
static volatile LONG g_dump;

/* ---------- finding the functions ---------- */
static uint8_t *g_text_lo, *g_text_hi;

static void exe_text(void) {
    uint8_t *exe = (uint8_t *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!memcmp(s->Name, ".text", 5)) { g_text_lo = exe + s->VirtualAddress; g_text_hi = g_text_lo + s->Misc.VirtualSize; return; }
}

/* mask[i] = 1: byte must match. */
static void make_mask(const uint8_t *b, int n, uint8_t *mask) {
    int i = 0;
    while (i < n) {
        if ((b[i] == 0xE8 || b[i] == 0xE9) && i + 5 <= n) { mask[i] = 1; memset(mask + i + 1, 0, 4); i += 5; continue; }
        if (i + 7 <= n && (b[i] == 0x48 || b[i] == 0x4C || b[i] == 0x44) && (b[i + 2] & 0xC7) == 0x05 &&
            (b[i + 1] == 0x8B || b[i + 1] == 0x8D || b[i + 1] == 0x89 || b[i + 1] == 0x63 || b[i + 1] == 0x3B ||
             b[i + 1] == 0x39 || b[i + 1] == 0x03)) {
            memset(mask + i, 1, 3); memset(mask + i + 3, 0, 4); i += 7; continue;
        }
        mask[i++] = 1;
    }
}

static void *find_in_exe(HMODULE rt, const char *name) {
    const uint8_t *tpl = (const uint8_t *)GetProcAddress(rt, name);
    if (!tpl || !g_text_lo) return NULL;
    static const int lens[] = { 48, 32, 24 };
    for (int li = 0; li < 3; li++) {
        int n = lens[li];
        uint8_t mask[64];
        make_mask(tpl, n, mask);
        uint8_t *hit = NULL; int hits = 0;
        for (uint8_t *p = g_text_lo; p + n <= g_text_hi; p += 16) {      /* functions are 16-byte aligned */
            if (p[0] != tpl[0] || p[1] != tpl[1]) continue;
            int ok = 1;
            for (int i = 2; i < n; i++) if (mask[i] && p[i] != tpl[i]) { ok = 0; break; }
            if (ok) { hit = p; if (++hits > 1) break; }
        }
        if (hits == 1) {
            log_line("imgui: %s at exe+%llx (%d bytes)", name, (unsigned long long)(hit - (uint8_t *)GetModuleHandleW(NULL)), n);
            return hit;
        }
        if (hits > 1) break;           /* longer pattern matched none, shorter matches many */
    }
    log_line("imgui: %s not found in mk9.exe", name);
    return NULL;
}

/* IsItemFocused: mov rax,[rip+G]; mov ecx,[rax+NavId] (8B 88 disp32) */
static void read_offsets(HMODULE rt) {
    const uint8_t *f = (const uint8_t *)GetProcAddress(rt, "?IsItemFocused@ImGui@@YA_NXZ");
    if (!f) return;
    for (int i = 0; i < 24; i++)
        if (f[i] == 0x8B && f[i + 1] == 0x88) { memcpy(&g_off_nav, f + i + 2, 4); log_line("imgui: NavId at +%x", g_off_nav); return; }
}

/* ---------- render thread ---------- */
static void add_text(const char *b, const char *e, const ImVec2 *pos, uint32_t col) {
    if (!b) return;
    size_t n = e ? (size_t)(e - b) : strlen(b);
    while (n && (b[n - 1] == ' ' || b[n - 1] == '\n')) n--;
    if (!n) return;
    /* ImGui's implicit "Debug" window is drawn every frame, even during fights: not a port screen */
    if (n == 5 && !strncmp(b, "Debug", 5)) return;
    if (n >= IMT_LEN) n = IMT_LEN - 1;
    float x = pos ? pos->x : 0, y = pos ? pos->y : 0;
    /* outlined text is drawn several times a pixel apart: keep one */
    for (int i = g_nbuild - 1; i >= 0 && i >= g_nbuild - 12; i--)
        if (fabsf(g_build[i].y - y) < 4 && fabsf(g_build[i].x - x) < 4 && !strncmp(g_build[i].s, b, n) && !g_build[i].s[n]) return;
    if (g_nbuild >= IMT_MAX) return;
    ImT *t = &g_build[g_nbuild++];
    memcpy(t->s, b, n); t->s[n] = 0;
    t->id = g_cur_item; t->x = x; t->y = y; t->col = col;
}

static void __cdecl h_addtext(void *self, void *font, float size, const ImVec2 *pos, uint32_t col,
                              const char *b, const char *e, float wrap, const void *clip) {
    InterlockedIncrement(&g_inflight);
    if ((col >> 24) > 0x40) add_text(b, e, pos, col);
    o_addtext(self, font, size, pos, col, b, e, wrap, clip);
    InterlockedDecrement(&g_inflight);
}

/* The SDK's own dialogs (rexruntimerd.dll ImGui): only logged on "imdump". */
static void __cdecl h_addtext_rt(void *self, void *font, float size, const ImVec2 *pos, uint32_t col,
                                 const char *b, const char *e, float wrap, const void *clip) {
    InterlockedIncrement(&g_inflight);
    if (g_dump > 0 && b) log_line("IMRT %.*s", (int)(e ? e - b : (long long)strlen(b)), b);
    o_addtext_rt(self, font, size, pos, col, b, e, wrap, clip);
    InterlockedDecrement(&g_inflight);
}

static char __cdecl h_itemadd(const void *bb, uint32_t id, const void *nav_bb, int flags) {
    InterlockedIncrement(&g_inflight);
    g_cur_item = id;
    char r = o_itemadd(bb, id, nav_bb, flags);
    InterlockedDecrement(&g_inflight);
    return r;
}

static void __cdecl h_render(void) {
    InterlockedIncrement(&g_inflight);
    EnterCriticalSection(&g_lock);
    memcpy(g_done, g_build, sizeof(ImT) * g_nbuild);
    g_ndone = g_nbuild;
    LeaveCriticalSection(&g_lock);
    if (g_nbuild) g_last_frame_t = GetTickCount();
    if (g_dump > 0 && g_nbuild) {
        for (int i = 0; i < g_nbuild; i++)
            log_line("IM [%08x] %s @%.0f,%.0f col %08x", g_build[i].id, g_build[i].s, g_build[i].x, g_build[i].y, g_build[i].col);
        g_dump = 0;
    }
    g_nbuild = 0; g_cur_item = 0;
    InterlockedIncrement(&g_frame);
    o_render();
    InterlockedDecrement(&g_inflight);
}

/* ---------- setup ---------- */
static int hook(void *t, void *d, void **o) {
    return t && MH_CreateHook(t, d, o) == MH_OK && MH_EnableHook(t) == MH_OK;
}

void im_init(void) {
    InitializeCriticalSection(&g_lock);
    HMODULE rt = GetModuleHandleW(L"rexruntimerd.dll");
    if (!rt) { log_line("imgui: no rexruntimerd.dll"); return; }
    exe_text();
    read_offsets(rt);
    t_addtext = find_in_exe(rt, "?AddText@ImDrawList@@QEAAXPEAUImFont@@MAEBUImVec2@@IPEBD2MPEBUImVec4@@@Z");
    t_itemadd = find_in_exe(rt, "?ItemAdd@ImGui@@YA_NAEBUImRect@@IPEBU2@H@Z");
    t_render = find_in_exe(rt, "?Render@ImGui@@YAXXZ");
    p_ctx = (ctx_fn)find_in_exe(rt, "?GetCurrentContext@ImGui@@YAPEAUImGuiContext@@XZ");
    if (!t_addtext || !t_itemadd || !t_render || !p_ctx) { log_line("imgui: port screens will not be read"); return; }
    if (!hook(t_addtext, (void *)h_addtext, (void **)&o_addtext) || !hook(t_itemadd, (void *)h_itemadd, (void **)&o_itemadd) ||
        !hook(t_render, (void *)h_render, (void **)&o_render)) { log_line("imgui: hooking failed"); return; }
    t_addtext_rt = (void *)GetProcAddress(rt, "?AddText@ImDrawList@@QEAAXPEAUImFont@@MAEBUImVec2@@IPEBD2MPEBUImVec4@@@Z");
    if (!hook(t_addtext_rt, (void *)h_addtext_rt, (void **)&o_addtext_rt)) t_addtext_rt = NULL;
    g_ready = 1;
    log_line("imgui: port screens hooked");
}

void im_shutdown(void) {
    void *ts[] = { t_addtext, t_itemadd, t_render, t_addtext_rt };
    for (int i = 0; i < 4; i++) if (ts[i]) { MH_DisableHook(ts[i]); MH_RemoveHook(ts[i]); }
    g_ready = 0;
}

void im_dump(void) { g_dump = 1; }

/* ---------- mod thread ---------- */
static uint32_t nav_id(void) {
    uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
    return c ? *(uint32_t *)(c + g_off_nav) : 0;
}

static ImT g_view[IMT_MAX];
static int g_nview;
static volatile DWORD g_menu_text_t;    /* last frame with port-screen text (the Practice panels don't count) */

int im_menu_on(void) { return g_ready && GetTickCount() - g_menu_text_t < 300; }
static uint32_t g_said_nav;
static char g_said_item[1024];
static char g_screen_sig[2048];

static void join_item(uint32_t id, char *out, size_t cap) {
    out[0] = 0;
    for (int i = 0; i < g_nview; i++) {
        if (g_view[i].id != id || strstr(out, g_view[i].s)) continue;
        if (out[0]) strncat(out, ", ", cap - strlen(out) - 1);
        strncat(out, g_view[i].s, cap - strlen(out) - 1);
    }
}

static int by_pos(const void *a, const void *b) {
    const ImT *x = a, *y = b;
    if (fabsf(x->y - y->y) > 6) return x->y < y->y ? -1 : 1;
    return x->x < y->x ? -1 : x->x > y->x;
}

/* Screen text in reading order; plain_only: only text outside interactive items. */
static void screen_text(char *out, size_t cap, int plain_only) {
    static ImT tmp[IMT_MAX];
    int n = g_nview;
    memcpy(tmp, g_view, sizeof(ImT) * n);
    qsort(tmp, n, sizeof(ImT), by_pos);
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        if (plain_only && tmp[i].id) continue;
        if (out[0]) strncat(out, ". ", cap - strlen(out) - 1);
        strncat(out, tmp[i].s, cap - strlen(out) - 1);
    }
}

/* The port's Online window opens by itself at every start until a username is saved (online_name.txt), and it
 * takes the keyboard: say how to get past it. */
const wchar_t k_name_hint[] = L"Type a username and press Enter to save it, or press F1 to hide this window and play.";
int im_name_prompt_open(void) { return strstr(g_screen_sig, "Choose your username") != NULL; }

int im_read_screen(void) {
    if (!im_menu_on()) return 0;
    char buf[4096];
    screen_text(buf, sizeof buf, 0);
    if (buf[0]) speak_utf8(buf, 1);
    return 1;
}

/* ---------- Practice panels (port 1.4.0 frame data, 1.4.2 opponent recording) ----------
 * Both are always-on ImGui windows at the right of the screen during Practice fights. They are not screens: they are
 * taken out of the generic reading (and don't count as "a port screen is open", which mutes the hum), and read here:
 * recording changes are spoken as they happen, frame data on F10 (Shift+F10: after every attack). The panel texts
 * come in the port's language (English and Spanish handled). */
typedef struct { int mode, loop; char len[32]; } RecView;   /* mode 0 none, 1 idle, 2 recording, 3 playing */
static volatile DWORD g_rec_panel_t, g_fd_panel_t;
static RecView g_rec;
static char g_fd_text[256];
static int g_fd_auto, g_fd_flash, g_rec_hint_said;

static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
static int is_number(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) if (!strchr("+-0123456789", *s)) return 0;
    return 1;
}
static const char *const k_rec_lines[] = { "RECORD THE OPPONENT", "GRABAR AL RIVAL", "RECORDING", "GRABANDO", "PLAYING",
    "REPRODUCIENDO", "Your controller", "Tu mando", "In a loop", "En bucle", "Once", "Una vez", "Recording:", "Grabaci",
    "F5 (BACK", "F6 (BACK", "F7 (BACK", "The opponent", "El rival", "\xc2\xbf" };
static const char *const k_fd_lines[] = { "FRAME DATA", "Throw an attack", "Peg\xc3\xa1 un golpe", "Startup", "Arranque",
    "Total", "Whiffed", "No toc", "On hit", "Al golpear", "On block", "Al bloquear" };
static int known(const char *s, const char *const *list, int n) {
    for (int i = 0; i < n; i++) if (starts(s, list[i])) return 1;
    return 0;
}
#define NOF(a) (int)(sizeof a / sizeof a[0])

/* Takes the panels out of g_view (sorted in reading order into rec[] / fd[]). */
static void split_panels(ImT *rec, int *nrec, ImT *fd, int *nfd) {
    const ImT *rt = NULL, *ft = NULL;
    *nrec = *nfd = 0;
    for (int i = 0; i < g_nview; i++) {
        if (g_view[i].id) continue;
        if (!strcmp(g_view[i].s, "RECORD THE OPPONENT") || !strcmp(g_view[i].s, "GRABAR AL RIVAL")) rt = &g_view[i];
        else if (starts(g_view[i].s, "FRAME DATA")) ft = &g_view[i];
    }
    if (!rt && !ft) return;
    float ry = rt ? rt->y - 2 : 0, fx = ft ? ft->x - 2 : 0, fy = ft ? ft->y - 2 : 0;
    int k = 0;
    for (int i = 0; i < g_nview; i++) {
        ImT t = g_view[i];
        /* the panels' own lines anywhere (an auto-sized window's first frames draw them elsewhere); bare numbers
         * only beside the frame data rows */
        int in_rec = rt && !t.id && known(t.s, k_rec_lines, NOF(k_rec_lines));
        int in_fd = !in_rec && ft && !t.id && (known(t.s, k_fd_lines, NOF(k_fd_lines)) ||
                    (is_number(t.s) && t.x >= fx && t.y >= fy && (!rt || t.y < ry)));
        if (in_rec) { if (*nrec < 32) rec[(*nrec)++] = t; }
        else if (in_fd) { if (*nfd < 32) fd[(*nfd)++] = t; }
        else g_view[k++] = t;
    }
    g_nview = k;
    qsort(rec, *nrec, sizeof(ImT), by_pos);
    qsort(fd, *nfd, sizeof(ImT), by_pos);
}

static void say_rec_change(const RecView *n) {
    const RecView *o = &g_rec;
    if (n->mode == 2 && o->mode != 2) {
        if (!g_rec_hint_said) {
            speak(L"Recording. Your controls move the opponent while your fighter stands still. F5 ends it. "
                  L"The opponent only moves if Enemy type is Human in the pause menu's AI options.", 1);
            g_rec_hint_said = 1;
        } else speak(L"Recording. F5 ends it.", 1);
    } else if (o->mode == 2 && n->mode == 1) {
        if (n->len[0]) speakf(1, "Recorded %s. F6 plays it back, F7 loops it.", n->len);
        else speak(L"Recording ended. Nothing recorded.", 1);
    } else if (n->mode == 3 && o->mode != 3) {
        speak(n->loop ? L"Playing back, in a loop. F6 stops it." : L"Playing back. F6 stops it.", 1);
    } else if (o->mode == 3 && n->mode == 1) {
        speak(L"Playback stopped.", 1);
    } else if (o->mode && n->mode == o->mode && n->loop != o->loop && n->mode != 2) {
        speak(n->loop ? L"Loop on" : L"Loop off", 1);
    }
}

static void read_rec(const ImT *rec, int n) {
    RecView v = { 1, 0, "" };
    for (int i = 0; i < n; i++) {
        const char *s = rec[i].s, *c;
        if (starts(s, "RECORDING ") || starts(s, "GRABANDO ")) v.mode = 2;
        else if (starts(s, "PLAYING ") || starts(s, "REPRODUCIENDO ")) v.mode = 3;
        if (starts(s, "In a loop") || starts(s, "En bucle")) v.loop = 1;
        if (starts(s, "F7 (BACK") && (c = strrchr(s, ':')) != NULL) v.loop = !strcmp(c, ": yes") || !strcmp(c, ": s\xc3\xad");
        if ((starts(s, "Recording: ") || starts(s, "Grabaci")) && (c = strchr(s, ' ')) != NULL)
            snprintf(v.len, sizeof v.len, "%.1f seconds", atof(c + 1));
    }
    say_rec_change(&v);
    g_rec = v;
    g_rec_panel_t = GetTickCount();
}

/* "Startup 7. On block minus 3. Total 25." from label / value rows (a value is drawn after its label, same line). */
static void read_fd(const ImT *fd, int n) {
    char out[256] = "";
    int flash = 0;
    for (int i = 0; i < n; i++) {
        const char *s = fd[i].s;
        if (starts(s, "FRAME DATA")) {
            if (strstr(s, "OPPONENT") || strstr(s, "RIVAL")) strncat(out, "Opponent. ", sizeof out - strlen(out) - 1);
            continue;
        }
        if (is_number(s)) {
            uint32_t c = fd[i].col;   /* a new result flashes white for a moment */
            if ((c & 0xFF) >= 0xFC && ((c >> 8) & 0xFF) >= 0xFC && ((c >> 16) & 0xFF) >= 0xFC) flash = 1;
            char v[48];
            if (!strcmp(s + (s[0] == '+' || s[0] == '-'), "0")) snprintf(v, sizeof v, " 0");
            else if (s[0] == '-' && s[1]) snprintf(v, sizeof v, " minus %s", s + 1);
            else if (s[0] == '+') snprintf(v, sizeof v, " plus %s", s + 1);
            else snprintf(v, sizeof v, " %s", s);
            strncat(out, v, sizeof out - strlen(out) - 1);
            continue;
        }
        if (out[0] && out[strlen(out) - 1] != ' ') strncat(out, ". ", sizeof out - strlen(out) - 1);
        strncat(out, s, sizeof out - strlen(out) - 1);
    }
    strcpy(g_fd_text, out);
    if (flash && !g_fd_flash && g_fd_auto) speak_utf8(out, 1);
    g_fd_flash = flash;
    g_fd_panel_t = GetTickCount();
}

static void practice_panels(void) {
    static ImT rec[32], fd[32];
    int nrec, nfd;
    split_panels(rec, &nrec, fd, &nfd);
    if (nrec) read_rec(rec, nrec);
    else if (g_rec.mode && GetTickCount() - g_rec_panel_t > 500) memset(&g_rec, 0, sizeof g_rec);
    if (nfd) read_fd(fd, nfd);
}

int im_practice_record_on(void) { return g_ready && GetTickCount() - g_rec_panel_t < 500; }
int im_practice_nothing_recorded(void) { return im_practice_record_on() && g_rec.mode == 1 && !g_rec.len[0]; }

void im_frame_data_key(int shift) {
    if (!g_ready || GetTickCount() - g_fd_panel_t > 500) { speak(L"Frame data is only shown in Practice", 1); return; }
    if (shift) {
        g_fd_auto = !g_fd_auto;
        speak(g_fd_auto ? L"Frame data after every attack" : L"Frame data on F10 only", 1);
        return;
    }
    speak_utf8(g_fd_text[0] ? g_fd_text : "No frame data yet", 1);
}

void im_poll(void) {
    if (!g_ready) return;
    static LONG seen;
    LONG f = g_frame;
    if (f == seen) return;
    seen = f;
    EnterCriticalSection(&g_lock);
    g_nview = g_ndone;
    memcpy(g_view, g_done, sizeof(ImT) * g_nview);
    LeaveCriticalSection(&g_lock);
    practice_panels();
    if (g_nview) g_menu_text_t = GetTickCount();
    if (!g_nview) {
        if (GetTickCount() - g_last_frame_t > 300) {
            if (g_loaded_said && im_name_prompt_open()) speak(L"Online window hidden. F1 shows it again.", 1);
            g_said_nav = 0; g_said_item[0] = 0; g_screen_sig[0] = 0;
        }
        return;
    }
    char sig[2048];
    screen_text(sig, sizeof sig, 1);
    uint32_t nav = nav_id();
    char item[1024] = "";
    if (nav) join_item(nav, item, sizeof item);
    int new_screen = strcmp(sig, g_screen_sig) != 0;
    /* a ticking counter (download percent) changes the text every frame: only a big change is a new screen */
    if (new_screen && g_screen_sig[0]) {
        size_t a = strlen(sig), b = strlen(g_screen_sig), same = 0;
        while (same < a && same < b && sig[same] == g_screen_sig[same]) same++;
        if (same > 40 && same * 10 > (a > b ? a : b) * 6) new_screen = 0;
    }
    if (new_screen) {
        strcpy(g_screen_sig, sig);
        if (sig[0]) speak_utf8(sig, 1);
        if (item[0]) speak_utf8(item, 0);
        if (g_loaded_said && im_name_prompt_open()) speak(k_name_hint, 0);   /* before that, dllmain says it */
        g_said_nav = nav; strcpy(g_said_item, item);
        return;
    }
    if (nav != g_said_nav || strcmp(item, g_said_item)) {
        if (item[0]) speak_utf8(item, 1);
        g_said_nav = nav; strcpy(g_said_item, item);
    }
}
