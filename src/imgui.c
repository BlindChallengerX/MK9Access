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
#include <ctype.h>
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
static uint32_t g_off_io = 0x28;        /* ImGuiContext.IO; io.ConfigFlags is its first field */
static int g_ready;

#define IMT_MAX 256
#define IMT_LEN 240
typedef struct { char s[IMT_LEN]; uint32_t id; float x, y; uint32_t col; } ImT;
static ImT g_build[IMT_MAX], g_done[IMT_MAX];
static int g_nbuild, g_ndone;
/* every item's box (a text box has no text of its own: its row is found by the box) */
typedef struct { uint32_t id; float x0, y0, x1, y1; } ImItem;
#define IMI_MAX 512
static ImItem g_ibuild[IMI_MAX], g_idone[IMI_MAX];
static int g_nibuild, g_nidone;
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
    if (f)
        for (int i = 0; i < 24; i++)
            if (f[i] == 0x8B && f[i + 1] == 0x88) { memcpy(&g_off_nav, f + i + 2, 4); log_line("imgui: NavId at +%x", g_off_nav); break; }
    /* GetIO: mov rax,[rip+G]; add rax,imm8 (48 83 C0 xx) or imm32 (48 05 xxxxxxxx) */
    f = (const uint8_t *)GetProcAddress(rt, "?GetIO@ImGui@@YAAEAUImGuiIO@@XZ");
    if (f)
        for (int i = 0; i < 16; i++) {
            if (f[i] == 0x48 && f[i + 1] == 0x83 && f[i + 2] == 0xC0) { g_off_io = f[i + 3]; break; }
            if (f[i] == 0x48 && f[i + 1] == 0x05) { memcpy(&g_off_io, f + i + 2, 4); break; }
        }
    log_line("imgui: IO at +%x", g_off_io);
}

/* ---------- render thread ---------- */
static void add_text(const char *b, const char *e, const ImVec2 *pos, uint32_t col) {
    if (!b) return;
    size_t n = e ? (size_t)(e - b) : strlen(b);
    while (n && (b[n - 1] == ' ' || b[n - 1] == '\n')) n--;
    if (!n) return;
    /* ImGui's implicit "Debug" window is drawn every frame, even during fights: not a port screen */
    if (n == 5 && !strncmp(b, "Debug", 5)) return;
    /* the room list's "· 7 min" and "~191 ms": the dot as nothing, the tilde as a word */
    const char *pre = "";
    if (n > 3 && !strncmp(b, "\xc2\xb7 ", 3)) { b += 3; n -= 3; }
    else if (n > 1 && b[0] == '~' && isdigit((unsigned char)b[1])) { b++; n--; pre = "about "; }
    if (n + strlen(pre) >= IMT_LEN) n = IMT_LEN - 1 - strlen(pre);
    /* the room's code, "Room WJ9753" ("Sala" in Spanish/Portuguese): spelled, so it can be told to someone */
    char spelled[40];
    if ((n == 11 && (!strncmp(b, "Room ", 5) || !strncmp(b, "Sala ", 5)))) {
        int code = 1;
        for (int i = 5; i < 11; i++) code &= (b[i] >= 'A' && b[i] <= 'Z') || (b[i] >= '0' && b[i] <= '9');
        if (code) {
            int k = snprintf(spelled, sizeof spelled, "%.5s", b);
            for (int i = 5; i < 11; i++) k += snprintf(spelled + k, sizeof spelled - k, i < 10 ? "%c " : "%c", b[i]);
            b = spelled; n = (size_t)k;
        }
    }
    float x = pos ? pos->x : 0, y = pos ? pos->y : 0;
    /* outlined text is drawn several times a pixel apart: keep one */
    size_t np = strlen(pre);
    for (int i = g_nbuild - 1; i >= 0 && i >= g_nbuild - 12; i--)
        if (fabsf(g_build[i].y - y) < 4 && fabsf(g_build[i].x - x) < 4 && !strncmp(g_build[i].s + np, b, n) &&
            !g_build[i].s[np + n]) return;
    if (g_nbuild >= IMT_MAX) return;
    ImT *t = &g_build[g_nbuild++];
    memcpy(t->s, pre, np); memcpy(t->s + np, b, n); t->s[np + n] = 0;
    t->id = g_cur_item; t->x = x; t->y = y; t->col = col;
}

static void __cdecl h_addtext(void *self, void *font, float size, const ImVec2 *pos, uint32_t col,
                              const char *b, const char *e, float wrap, const void *clip) {
    InterlockedIncrement(&g_inflight);
    /* a line scrolled half out of a list (the chat at its top edge) is drawn and clipped: keep a line only when
     * its middle is inside the clip rectangle (the draw list's current one: ImDrawList._CmdHeader.ClipRect, +96) */
    const float *cr = clip ? (const float *)clip : self ? (const float *)((const uint8_t *)self + 96) : NULL;
    float mid = pos ? pos->y + size * 0.5f : 0;
    int visible = !cr || !pos || (mid >= cr[1] && mid <= cr[3]);
    /* a player without a photo gets a letter in a circle (alpha 0xEB): "B", "?" - not text to read */
    int avatar_letter = b && (col >> 24) == 0xEB && (e ? e - b : (long long)strlen(b)) == 1;
    if ((col >> 24) > 0x40 && visible && !avatar_letter) add_text(b, e, pos, col);
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
    if (id && bb && g_nibuild < IMI_MAX) {
        const float *r4 = (const float *)bb;
        ImItem *it = &g_ibuild[g_nibuild++];
        it->id = id; it->x0 = r4[0]; it->y0 = r4[1]; it->x1 = r4[2]; it->y1 = r4[3];
    }
    char r = o_itemadd(bb, id, nav_bb, flags);
    InterlockedDecrement(&g_inflight);
    return r;
}

static void __cdecl h_render(void) {
    InterlockedIncrement(&g_inflight);
    EnterCriticalSection(&g_lock);
    memcpy(g_done, g_build, sizeof(ImT) * g_nbuild);
    g_ndone = g_nbuild;
    memcpy(g_idone, g_ibuild, sizeof(ImItem) * g_nibuild);
    g_nidone = g_nibuild;
    LeaveCriticalSection(&g_lock);
    if (g_nbuild) g_last_frame_t = GetTickCount();
    if (g_dump > 0 && g_nbuild) {
        for (int i = 0; i < g_nbuild; i++)
            log_line("IM [%08x] %s @%.0f,%.0f col %08x", g_build[i].id, g_build[i].s, g_build[i].x, g_build[i].y, g_build[i].col);
        g_dump = 0;
    }
    g_nbuild = 0; g_nibuild = 0; g_cur_item = 0;
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

/* The port only turns on ImGui's gamepad navigation: F1/F2 then answer the controller and the mouse, and the keyboard
 * reaches nothing but a text box that already has focus (the username could not be typed without a mouse). Keyboard
 * navigation (arrows move, Enter or Space choose, Escape backs out, Tab steps through) is ImGui's own; while a port
 * window is open the port keeps every input device from the game (mk9_overlay_input GameMayRead), so the keys do not
 * also move the game's menu behind it. Set again every poll: the port rebuilds the flags it owns. */
static void keyboard_nav(void) {
    uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
    if (!c) return;
    volatile int32_t *flags = (volatile int32_t *)(c + g_off_io);
    if (!(*flags & 1)) {
        InterlockedOr((volatile LONG *)flags, 1);   /* ImGuiConfigFlags_NavEnableKeyboard */
        log_line("imgui: keyboard navigation on");
    }
}

static ImT g_view[IMT_MAX];
static int g_nview;
static ImItem g_iview[IMI_MAX];
static int g_niview;
static volatile DWORD g_menu_text_t;    /* last frame with port-screen text (the Practice panels don't count) */

int im_menu_on(void) { return g_ready && GetTickCount() - g_menu_text_t < 300; }
static uint32_t g_said_nav;
static char g_said_item[1024];
static char g_screen_sig[2048];

static int by_pos(const void *a, const void *b) {
    const ImT *x = a, *y = b;
    if (fabsf(x->y - y->y) > 6) return x->y < y->y ? -1 : 1;
    return x->x < y->x ? -1 : x->x > y->x;
}

/* Between two pieces of text: ", " on the same row, ". " on a new one, a space after a label's colon. */
static void join_sep(char *out, size_t cap, const ImT *prev, const ImT *next) {
    if (!out[0]) return;
    size_t n = strlen(out);
    const char *sep = out[n - 1] == ':' ? " " : prev && fabsf(prev->y - next->y) <= 6 ? ", " : ". ";
    strncat(out, sep, cap - n - 1);
}

/* The focused item's text. A button in a row of a table (the room list's "Watch" / "Enter") or after a label
 * ("Join with a code:" ... "Join") gets the row's plain text before it, as a sighted player reads the row. */
/* Returns 0 when the item has no text of its own (a text box). */
static int join_item(uint32_t id, char *out, size_t cap) {
    out[0] = 0;
    const ImItem *box = NULL;
    for (int i = 0; i < g_niview; i++) if (g_iview[i].id == id) { box = &g_iview[i]; break; }
    if (box) {
        static ImT row[24];
        int n = 0;
        for (int i = 0; i < g_nview && n < 24; i++)
            if (!g_view[i].id && g_view[i].y >= box->y0 - 6 && g_view[i].y <= box->y1 && g_view[i].x < box->x0)
                row[n++] = g_view[i];
        qsort(row, n, sizeof(ImT), by_pos);
        /* from the last label ("You: BlindChallengerX, Change", not the connection status before it) */
        int from = 0;
        for (int i = 0; i < n; i++) { size_t l = strlen(row[i].s); if (l && row[i].s[l - 1] == ':') from = i; }
        for (int i = from; i < n; i++) {
            join_sep(out, cap, i > from ? &row[i - 1] : NULL, &row[i]);
            strncat(out, row[i].s, cap - strlen(out) - 1);
        }
    }
    int own0 = -1;
    for (int i = 0; i < g_nview; i++) {
        if (g_view[i].id != id) continue;
        int dup = 0;                                   /* the item's own text drawn twice */
        for (int j = own0 >= 0 ? own0 : i; j < i && !dup; j++) dup = g_view[j].id == id && !strcmp(g_view[j].s, g_view[i].s);
        if (dup) continue;
        if (own0 < 0) own0 = i;
        if (out[0]) strncat(out, out[strlen(out) - 1] == ':' ? " " : ", ", cap - strlen(out) - 1);
        strncat(out, g_view[i].s, cap - strlen(out) - 1);
    }
    if (own0 < 0 && box) {
        /* unlabelled: a scrolling list or chat (a child window: text inside its box; Enter goes in), a text box
         * (wide, empty: what is typed is not drawn through AddText) or a picture (the rival's photo) */
        int inside = 0;
        for (int i = 0; i < g_nview; i++)
            if (g_view[i].x >= box->x0 && g_view[i].x < box->x1 && g_view[i].y >= box->y0 && g_view[i].y < box->y1) inside++;
        int wide = box->x1 - box->x0 > 2.5f * (box->y1 - box->y0);
        if (out[0]) strncat(out, out[strlen(out) - 1] == ':' ? " " : ", ", cap - strlen(out) - 1);
        strncat(out, inside ? "list. Enter goes into it, Escape comes back out." :
                     wide ? "text box. Enter to type in it, Enter again when done." : "picture button", cap - strlen(out) - 1);
    }
    return own0 >= 0;
}

/* Screen text in reading order; plain_only: only text outside interactive items. */
static void screen_text(char *out, size_t cap, int plain_only) {
    static ImT tmp[IMT_MAX];
    int n = g_nview;
    memcpy(tmp, g_view, sizeof(ImT) * n);
    qsort(tmp, n, sizeof(ImT), by_pos);
    out[0] = 0;
    const ImT *prev = NULL;
    for (int i = 0; i < n; i++) {
        if (plain_only && tmp[i].id) continue;
        join_sep(out, cap, prev, &tmp[i]);
        strncat(out, tmp[i].s, cap - strlen(out) - 1);
        prev = &tmp[i];
    }
}

/* The port's Online window opens by itself at every start until a username is saved (online_name.txt), and it
 * takes the keyboard: say how to get past it. */
const wchar_t k_name_hint[] = L"Arrow keys move to the name box. Press Enter, type a username and press Enter to save it. "
                              L"Or press F1 to hide this window and play.";
int im_name_prompt_open(void) { return strstr(g_screen_sig, "Choose your username") != NULL; }

static void badge_say_all(int interrupt);
static volatile DWORD g_badge_t;

int im_read_screen(void) {
    if (!im_menu_on()) {
        /* no port window: in an online session, F5 reads the connection corner */
        if (g_ready && GetTickCount() - g_badge_t < 500) { badge_say_all(1); return 1; }
        return 0;
    }
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

/* ---------- the online session's corner (online_ui.cpp DrawSessionBadge) ----------
 * Through a whole session (menus and fights) a small window says "vs <rival>", signal bars, "<ping> ms - delay N"
 * and, when they apply, warnings. It has no buttons. Read generically it would mute the hum for the whole fight
 * and speak again at every ping change, so it is taken out: the rival is named once, a warning is said when it comes
 * up (without its ticking numbers), and F5 reads the whole corner when no port window is open. English, Spanish
 * and Portuguese (mk9_online_language follows the game language). */
static const char *const k_badge_warn[][3] = {
    { "Waiting for ", "Esperando a ", "Esperando " },
    { "Unstable connection", "Conexi\xc3\xb3n inestable", "Conex\xc3\xa3o inst\xc3\xa1vel" },
    { "Slow connection", "Conexi\xc3\xb3n lenta", "Conex\xc3\xa3o lenta" },
    { "The two games stopped", "Las dos partidas dejaron", "As duas partidas deixaram" },
    { "Connecting to ", "Conectando con ", "Conectando com " },
};
#define NWARN 5
static ImT g_badge[16];
static int g_nbadge;
static char g_badge_rival[96];
static DWORD g_warn_seen[NWARN];         /* last frame each warning was up */

static int badge_ms_line(const char *s) {       /* "180 ms", "180 ms - delay 2", "... - relay" */
    if (!isdigit((unsigned char)*s)) return 0;
    while (isdigit((unsigned char)*s)) s++;
    return !strncmp(s, " ms", 3) && (!s[3] || s[3] == ' ');
}
static int badge_warn_kind(const char *s) {
    for (int k = 0; k < NWARN; k++)
        for (int l = 0; l < 3; l++) if (starts(s, k_badge_warn[k][l])) return k;
    return -1;
}
static int badge_watchers(const char *s) {      /* "2 watching", "2 mirando", "2 assistindo" */
    if (!isdigit((unsigned char)*s)) return 0;
    while (isdigit((unsigned char)*s)) s++;
    return !strcmp(s, " watching") || !strcmp(s, " mirando") || !strcmp(s, " assistindo");
}

/* Takes the corner's lines out of g_view into g_badge (reading order). */
static void split_badge(void) {
    const ImT *ms = NULL;
    for (int i = 0; i < g_nview; i++) if (!g_view[i].id && badge_ms_line(g_view[i].s)) { ms = &g_view[i]; break; }
    float my = ms ? ms->y : -1e9f;
    int k = 0, nb = 0;
    for (int i = 0; i < g_nview; i++) {
        ImT t = g_view[i];
        int mine = !t.id && ((ms && fabsf(t.y - my) < 8 && t.x <= ms->x) || badge_warn_kind(t.s) >= 0 || badge_watchers(t.s));
        if (mine && nb < 16) g_badge[nb++] = t;
        else g_view[k++] = t;
    }
    g_nview = k;
    if (!nb) return;
    qsort(g_badge, nb, sizeof(ImT), by_pos);
    g_nbadge = nb;
    g_badge_t = GetTickCount();
}

/* "vs Scorpion99. 180 ms - delay 2. ..." */
static void badge_say_all(int interrupt) {
    char out[1024] = "";
    for (int i = 0; i < g_nbadge; i++) {
        if (out[0]) strncat(out, !strcmp(g_badge[i - 1].s, "vs") ? " " : ". ", sizeof out - strlen(out) - 1);
        strncat(out, g_badge[i].s, sizeof out - strlen(out) - 1);
    }
    if (out[0]) speak_utf8(out, interrupt);
}

static void badge_poll(void) {
    if (GetTickCount() - g_badge_t > 300 || !g_nbadge) return;
    DWORD now = GetTickCount();
    /* the rival, once per game run (a session is a game run of its own) */
    for (int i = 0; i + 1 < g_nbadge; i++) {
        if (strcmp(g_badge[i].s, "vs") || badge_ms_line(g_badge[i + 1].s)) continue;
        if (strcmp(g_badge_rival, g_badge[i + 1].s)) {
            snprintf(g_badge_rival, sizeof g_badge_rival, "%s", g_badge[i + 1].s);
            speakf(0, "Connected to %s. F5 reads the connection.", g_badge_rival);
            log_line("online: session with %s", g_badge_rival);
        }
        break;
    }
    for (int i = 0; i < g_nbadge; i++) {
        int k = badge_warn_kind(g_badge[i].s);
        if (k < 0) continue;
        /* said again only after it was gone for 10 s (an unstable link flickers) */
        if (now - g_warn_seen[k] > 10000) {
            char s[IMT_LEN];
            memcpy(s, g_badge[i].s, sizeof s); s[sizeof s - 1] = 0;
            char *cut = NULL;
            if (k == 0) cut = strstr(s, "...");            /* "Waiting for X... 3 s": the seconds tick */
            else if (k == 1) cut = strstr(s, " (");        /* "(4% lost, jitter 35 ms)" */
            if (cut) *cut = 0;
            if (k != 4) speak_utf8(s, 0);                  /* "Connecting to X" is followed by "Connected to X" */
            log_line("online: %s", g_badge[i].s);
        }
        g_warn_seen[k] = now;
    }
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

/* A line with its digits folded ("Ends in 19 s" = "Ends in 18 s"): a ticking counter, a ping or a download
 * percent does not make it a new line. */
static void fold_digits(const char *s, char *o, size_t cap) {
    size_t n = 0;
    for (; *s && n + 1 < cap; s++) {
        if (isdigit((unsigned char)*s)) { if (!n || o[n - 1] != '#') o[n++] = '#'; }
        else o[n++] = *s;
    }
    o[n] = 0;
}

/* The last screen's plain lines, folded. */
static char g_prev[IMT_MAX][IMT_LEN];
static int g_nprev;

/* A new screen is said once it has held for a moment: a list that scrolls itself to the bottom on its second frame
 * (the chats) would otherwise be read twice. */
#define SETTLE_MS 150
static char g_pend[4096], g_pend_item[1024];
static DWORD g_pend_t;
static int g_pend_on, g_pend_hint;

static void pending_flush(void) {
    if (!g_pend_on || GetTickCount() - g_pend_t < SETTLE_MS) return;
    g_pend_on = 0;
    if (g_pend[0]) speak_utf8(g_pend, 1);
    if (g_pend_item[0]) speak_utf8(g_pend_item, !g_pend[0]);
    if (g_pend_hint) speak(k_name_hint, 0);
}

/* The room's "Copy code" button (and the lobby's "Copy diagnostics") copy silently: say what reached the clipboard. */
static void clipboard_poll(void) {
    static DWORD seq;
    DWORD s = GetClipboardSequenceNumber();
    if (!seq) { seq = s; return; }
    if (s == seq) return;
    seq = s;
    if (!im_menu_on() || !OpenClipboard(NULL)) return;
    wchar_t text[64] = L"";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *p = h ? (const wchar_t *)GlobalLock(h) : NULL;
    if (p) { wcsncpy(text, p, 63); text[63] = 0; GlobalUnlock(h); }
    CloseClipboard();
    int code = wcslen(text) == 6;
    for (int i = 0; i < 6 && code; i++) code = (text[i] >= L'A' && text[i] <= L'Z') || (text[i] >= L'0' && text[i] <= L'9');
    if (code) {
        speakf(1, "Code copied: %lc %lc %lc %lc %lc %lc. Paste it to the other player; they choose Join with a code.",
               text[0], text[1], text[2], text[3], text[4], text[5]);
    } else if (!wcsncmp(text, L"mk9-lauta ", 10)) {
        speak(L"Diagnostics copied.", 1);
    }
}

void im_poll(void) {
    if (!g_ready) return;
    keyboard_nav();
    clipboard_poll();
    pending_flush();
    static LONG seen;
    LONG f = g_frame;
    if (f == seen) return;
    seen = f;
    EnterCriticalSection(&g_lock);
    g_nview = g_ndone;
    memcpy(g_view, g_done, sizeof(ImT) * g_nview);
    g_niview = g_nidone;
    memcpy(g_iview, g_idone, sizeof(ImItem) * g_niview);
    LeaveCriticalSection(&g_lock);
    practice_panels();
    split_badge();
    badge_poll();
    /* a port window is open when something on it can be chosen (a button, a tab, a title bar): text-only corners
     * (the online toast, the spectator's corner) don't mute the fight cues */
    int interactive = 0;
    for (int i = 0; i < g_nview && !interactive; i++) interactive = g_view[i].id != 0;
    if (interactive) g_menu_text_t = GetTickCount();
    static DWORD window_t;
    if (g_nview) window_t = GetTickCount();
    if (!g_nview) {
        /* gone for a moment (the corners and the Practice panels may still be drawn) */
        if (GetTickCount() - window_t > 300 && (g_screen_sig[0] || g_nprev)) {
            if (g_loaded_said && im_name_prompt_open()) speak(L"Online window hidden. F1 shows it again.", 1);
            g_said_nav = 0; g_said_item[0] = 0; g_screen_sig[0] = 0; g_nprev = 0; g_pend_on = 0;
        }
        return;
    }
    /* the plain lines in reading order */
    static ImT cur[IMT_MAX];
    int n = 0;
    for (int i = 0; i < g_nview; i++) if (!g_view[i].id) cur[n++] = g_view[i];
    qsort(cur, n, sizeof(ImT), by_pos);
    static char folded[IMT_MAX][IMT_LEN];
    for (int i = 0; i < n; i++) fold_digits(cur[i].s, folded[i], IMT_LEN);
    /* lines that were not there before; the same line twice on a screen counts once */
    int kept = 0, fresh = 0;
    static unsigned char is_new[IMT_MAX];
    for (int i = 0; i < n; i++) {
        int found = 0;
        for (int j = 0; j < g_nprev && !found; j++) found = !strcmp(folded[i], g_prev[j]);
        is_new[i] = !found;
        if (found) kept++; else fresh++;
    }
    char sig[2048];
    screen_text(sig, sizeof sig, 1);
    uint32_t nav = nav_id();
    /* The focused button gone (Create private room, once the room opens): ImGui keeps its id, and Tab does nothing
     * until an arrow key is pressed. After 300 ms the focus moves to the window's first button below its title. */
    {
        static DWORD lost_t;
        int alive = !nav;
        for (int i = 0; i < g_niview && !alive; i++) alive = g_iview[i].id == nav;
        if (alive || !g_niview) lost_t = 0;
        else if (!lost_t) lost_t = GetTickCount();
        else if (GetTickCount() - lost_t > 300) {
            /* the two topmost labelled items: the title bar, then the first button */
            const ImItem *top[2] = { NULL, NULL };
            for (int i = 0; i < g_niview; i++) {
                const ImItem *it = &g_iview[i];
                int texted = 0;
                for (int j = 0; j < g_nview && !texted; j++) texted = g_view[j].id == it->id;
                if (!texted) continue;
                #define ABOVE(a, b) (!(b) || (a)->y0 < (b)->y0 - 2 || (fabsf((a)->y0 - (b)->y0) <= 2 && (a)->x0 < (b)->x0))
                if (ABOVE(it, top[0])) { top[1] = top[0]; top[0] = it; }
                else if (ABOVE(it, top[1])) top[1] = it;
                #undef ABOVE
            }
            const ImItem *best = top[1];
            uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
            if (c && best && *(volatile uint32_t *)(c + g_off_nav) == nav) {
                *(volatile uint32_t *)(c + g_off_nav) = best->id;
                log_line("imgui: focused item %08x gone, focus to %08x", nav, best->id);
                nav = best->id;
            }
            lost_t = 0;
        }
    }
    char item[1024] = "";
    if (nav) join_item(nav, item, sizeof item);
    int had = g_nprev > 0 || g_screen_sig[0];
    for (int i = 0; i < n; i++) strcpy(g_prev[i], folded[i]);
    g_nprev = n;
    snprintf(g_screen_sig, sizeof g_screen_sig, "%s", sig);
    if (!had || (fresh && kept * 2 < n)) {
        /* a new screen: all of it, then the focused item (once it settles) */
        snprintf(g_pend, sizeof g_pend, "%s", sig);
        snprintf(g_pend_item, sizeof g_pend_item, "%s", item);
        g_pend_hint = g_loaded_said && im_name_prompt_open();   /* before that, dllmain says it */
        g_pend_t = GetTickCount(); g_pend_on = 1;
        g_said_nav = nav; fold_digits(item, g_said_item, sizeof g_said_item);
        return;
    }
    /* the focused item first (a counter ticking in its row does not make it new), then the new lines */
    char fitem[1024];
    fold_digits(item, fitem, sizeof fitem);
    if (g_pend_on) {                                   /* the screen is still settling: it will say all of it */
        snprintf(g_pend, sizeof g_pend, "%s", sig);
        snprintf(g_pend_item, sizeof g_pend_item, "%s", item);
        g_said_nav = nav; strcpy(g_said_item, fitem);
        return;
    }
    if (nav != g_said_nav || strcmp(fitem, g_said_item)) {
        if (item[0]) speak_utf8(item, 1);
        g_said_nav = nav; strcpy(g_said_item, fitem);
    }
    if (fresh) {
        /* the same screen with new lines (a chat message, someone entering the room, a room in the list, a status
         * line, the focused button's tooltip): only those, each with the rest of its row ("Room of X, X, about
         * 191 ms, waiting", not a bare "waiting"), after whatever is being said */
        char add[2048] = "";
        static unsigned char done[IMT_MAX];
        memset(done, 0, sizeof done);
        for (int i = 0; i < n; i++) {
            if (!is_new[i] || done[i]) continue;
            const ImT *prev = NULL;
            for (int j = 0; j < n; j++) {
                if (fabsf(cur[j].y - cur[i].y) > 6) continue;
                done[j] = 1;
                if (strstr(item, cur[j].s)) continue;      /* already said with the focused item */
                if (add[0]) strncat(add, prev ? ", " : ". ", sizeof add - strlen(add) - 1);
                strncat(add, cur[j].s, sizeof add - strlen(add) - 1);
                prev = &cur[j];
            }
        }
        if (add[0]) speak_utf8(add, 0);
    }
}
