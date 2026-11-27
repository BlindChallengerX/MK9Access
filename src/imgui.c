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
 *
 * Windows with tabs (F2 options: Graphics, Controls, ...; F1 online): the tab row is the first row of items under
 * the title, and the open tab is the last tab drawn before its content (ImGui draws the tabs in order and the open
 * tab's content right after its own tab). Ctrl+Tab / Ctrl+Shift+Tab change tabs (keys.c drops them before ImGui,
 * which would switch windows); a focused tab says "<name> tab, k of n". A setting's help is the text right under it
 * (F2 draws it in grey under each option); checkboxes say checked / not checked (ImGui::Checkbox hooked).
 * Forum report 2026-11-25: "navigation on the F2 menu is very tricky" - the help heard was the one above (lines that
 * scrolled into view were read as new), the whole window was read at once, and the tabs could not be found.
 */
#include "access.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <wctype.h>
#include "MinHook.h"

typedef struct { float x, y; } ImVec2;
typedef void (__cdecl *addtext_fn)(void *self, void *font, float size, const ImVec2 *pos, uint32_t col,
                                   const char *b, const char *e, float wrap, const void *clip);
typedef char (__cdecl *itemadd_fn)(const void *bb, uint32_t id, const void *nav_bb, int flags);
typedef void (__cdecl *render_fn)(void);
typedef void *(__cdecl *ctx_fn)(void);
typedef char (__cdecl *checkbox_fn)(const char *label, char *v);
typedef char (__cdecl *tabitem_fn)(const char *label, char *p_open, int flags);
typedef char (__cdecl *inputtext_fn)(const char *label, char *buf, size_t buf_size, int flags, void *callback, void *user_data);
static inputtext_fn o_inputtext;
static void *t_inputtext;
static const char *g_tb_buf; static int g_tb_size;    /* render thread: inside InputText, its buffer */

static addtext_fn o_addtext, o_addtext_rt;
static itemadd_fn o_itemadd;
static checkbox_fn o_checkbox;
static void *t_checkbox;
static int g_cb_state;                  /* render thread: inside ImGui::Checkbox, 1 off / 2 on */
static tabitem_fn o_tabitem;
static void *t_tabitem;
static uint32_t g_off_curwin = 0x1470, g_off_drawlist = 0x2c8;   /* ImGuiContext.CurrentWindow, ImGuiWindow.DrawList */
static render_fn o_render;
static ctx_fn p_ctx;
static void *t_addtext, *t_addtext_rt, *t_itemadd, *t_render;
static uint32_t g_off_nav = 0x1fbc;
static uint32_t g_off_active = 0x14e4;    /* ActiveId: the item being used (a text box being typed in) */
static uint32_t g_off_io = 0x28;        /* ImGuiContext.IO; io.ConfigFlags is its first field */
static int g_ready;

#define IMT_MAX 256
#define IMT_LEN 240
typedef struct { char s[IMT_LEN]; uint32_t id; float x, y; uint32_t col; int ord; const void *dl; } ImT;   /* ord: draw order; dl: its window's draw list */
static ImT g_build[IMT_MAX], g_done[IMT_MAX];
static int g_nbuild, g_ndone;
/* every item's box (a text box has no text of its own: its row is found by the box) */
/* check: 0, 1 off, 2 on; tab: 0, 1 a tab, 2 the open tab (ImGui::BeginTabItem); dl: its window's draw list */
/* buf: a text box's own buffer (ImGui::InputText), read with ReadProcessMemory: its text is not drawn through AddText */
typedef struct { uint32_t id; float x0, y0, x1, y1; int check, tab; const void *dl; char lab[48]; const char *buf; int bufsz; } ImItem;
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

/* A function whose first bytes match several others (Checkbox: a long register-saving prologue, 5 hits) is found by a
 * string only it uses: the LEA [rip+string] in mk9.exe's code, then back to the start that matches the DLL's copy. */
static void *find_by_string(HMODULE rt, const char *name, const char *needle) {
    const uint8_t *tpl = (const uint8_t *)GetProcAddress(rt, name);
    if (!tpl || !g_text_lo) return NULL;
    uint8_t mask[24];
    make_mask(tpl, 24, mask);
    uint8_t *exe = (uint8_t *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    size_t nl = strlen(needle) + 1;              /* with its NUL */
    for (int si = 0; si < nt->FileHeader.NumberOfSections; si++, sec++) {
        if (!memcmp(sec->Name, ".text", 5) || !(sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        uint8_t *lo = exe + sec->VirtualAddress, *hi = lo + sec->Misc.VirtualSize;
        for (uint8_t *str = lo; str + nl <= hi; str++) {
            if (*str != (uint8_t)needle[0] || memcmp(str, needle, nl)) continue;
            for (uint8_t *q = g_text_lo; q + 7 <= g_text_hi; q++) {
                if ((q[0] != 0x48 && q[0] != 0x4C) || q[1] != 0x8D || (q[2] & 0xC7) != 0x05) continue;
                int32_t disp; memcpy(&disp, q + 3, 4);
                if (q + 7 + disp != str) continue;
                for (uint8_t *f = (uint8_t *)((uintptr_t)q & ~(uintptr_t)15); f > q - 0x4000 && f > g_text_lo; f -= 16) {
                    int ok = 1;
                    for (int i = 0; i < 24 && ok; i++) if (mask[i] && f[i] != tpl[i]) ok = 0;
                    if (ok) {
                        log_line("imgui: %s at exe+%llx (by \"%s\")", name, (unsigned long long)(f - exe), needle);
                        return f;
                    }
                }
            }
        }
    }
    log_line("imgui: %s not found by \"%s\"", name, needle);
    return NULL;
}

/* ImGui::InputText in mk9.exe: its first bytes match the DLL's copy only for 10 bytes (a different stack frame), too
 * short to search for. The port's room-code box (label "##code", online_ui.cpp) calls it: the first call after the
 * LEA of that label is InputText, checked against the DLL's first 10 bytes. */
static void *find_inputtext(HMODULE rt) {
    const uint8_t *tpl = (const uint8_t *)GetProcAddress(rt, "?InputText@ImGui@@YA_NPEBDPEAD_KHP6AHPEAUImGuiInputTextCallbackData@@@ZPEAX@Z");
    if (!tpl || !g_text_lo) return NULL;
    uint8_t *exe = (uint8_t *)GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    static const char needle[] = "##code";
    for (int si = 0; si < nt->FileHeader.NumberOfSections; si++, sec++) {
        if (!memcmp(sec->Name, ".text", 5) || !(sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        uint8_t *lo = exe + sec->VirtualAddress, *hi = lo + sec->Misc.VirtualSize;
        for (uint8_t *str = lo; str + sizeof needle <= hi; str++) {
            if (*str != '#' || memcmp(str, needle, sizeof needle)) continue;
            for (uint8_t *q = g_text_lo; q + 7 <= g_text_hi; q++) {
                if ((q[0] != 0x48 && q[0] != 0x4C) || q[1] != 0x8D || (q[2] & 0xC7) != 0x05) continue;
                int32_t disp; memcpy(&disp, q + 3, 4);
                if (q + 7 + disp != str) continue;
                for (uint8_t *c = q + 7; c < q + 200 && c + 5 <= g_text_hi; c++) {
                    if (*c != 0xE8) continue;
                    int32_t rel; memcpy(&rel, c + 1, 4);
                    uint8_t *f = c + 5 + rel;
                    if (f >= g_text_lo && f + 10 <= g_text_hi && !memcmp(f, tpl, 10)) {
                        log_line("imgui: InputText at exe+%llx (called with \"##code\")", (unsigned long long)(f - exe));
                        return f;
                    }
                    break;                               /* the first call only */
                }
            }
        }
    }
    log_line("imgui: InputText not found (text boxes not read)");
    return NULL;
}

/* IsItemFocused: mov rax,[rip+G]; mov ecx,[rax+NavId] (8B 88 disp32) */
static void read_offsets(HMODULE rt) {
    const uint8_t *f = (const uint8_t *)GetProcAddress(rt, "?IsItemFocused@ImGui@@YA_NXZ");
    if (f)
        for (int i = 0; i < 24; i++)
            if (f[i] == 0x8B && f[i + 1] == 0x88) { memcpy(&g_off_nav, f + i + 2, 4); log_line("imgui: NavId at +%x", g_off_nav); break; }
    /* IsItemActive: mov rax,[rip+G]; mov ecx,[rax+ActiveId] */
    f = (const uint8_t *)GetProcAddress(rt, "?IsItemActive@ImGui@@YA_NXZ");
    if (f)
        for (int i = 0; i < 24; i++)
            if (f[i] == 0x8B && f[i + 1] == 0x88) { memcpy(&g_off_active, f + i + 2, 4); log_line("imgui: ActiveId at +%x", g_off_active); break; }
    /* GetIO: mov rax,[rip+G]; add rax,imm8 (48 83 C0 xx) or imm32 (48 05 xxxxxxxx) */
    f = (const uint8_t *)GetProcAddress(rt, "?GetIO@ImGui@@YAAEAUImGuiIO@@XZ");
    if (f)
        for (int i = 0; i < 16; i++) {
            if (f[i] == 0x48 && f[i + 1] == 0x83 && f[i + 2] == 0xC0) { g_off_io = f[i + 3]; break; }
            if (f[i] == 0x48 && f[i + 1] == 0x05) { memcpy(&g_off_io, f + i + 2, 4); break; }
        }
    log_line("imgui: IO at +%x", g_off_io);
    /* GetWindowDrawList: mov rax,[rip+G]; mov rcx,[rax+CurrentWindow] (48 8B 88); ...; mov rax,[rax+DrawList] (48 8B 80) */
    f = (const uint8_t *)GetProcAddress(rt, "?GetWindowDrawList@ImGui@@YAPEAUImDrawList@@XZ");
    if (f)
        for (int i = 0; i < 32; i++) {
            if (f[i] == 0x48 && f[i + 1] == 0x8B && f[i + 2] == 0x88) memcpy(&g_off_curwin, f + i + 3, 4);
            if (f[i] == 0x48 && f[i + 1] == 0x8B && f[i + 2] == 0x80) memcpy(&g_off_drawlist, f + i + 3, 4);
            if (f[i] == 0xC3) break;
        }
    log_line("imgui: CurrentWindow at +%x, DrawList at +%x", g_off_curwin, g_off_drawlist);
}

/* ---------- render thread ---------- */
static void add_text(const void *dl, const char *b, const char *e, const ImVec2 *pos, uint32_t col) {
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
    t->id = g_cur_item; t->x = x; t->y = y; t->col = col; t->dl = dl;
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
    if ((col >> 24) > 0x40 && visible && !avatar_letter) add_text(self, b, e, pos, col);
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
        it->check = g_cb_state; g_cb_state = 0;
        it->tab = 0; it->lab[0] = 0; it->dl = NULL;
        it->buf = g_tb_buf; it->bufsz = g_tb_size; g_tb_buf = NULL;   /* a text box's first item is the box */
        uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
        uint8_t *win = c ? *(uint8_t **)(c + g_off_curwin) : NULL;
        if (win) it->dl = *(void **)(win + g_off_drawlist);
    }
    char r = o_itemadd(bb, id, nav_bb, flags);
    InterlockedDecrement(&g_inflight);
    return r;
}

/* ImGui::Checkbox(label, bool *v): its ItemAdd is the box; *v says whether it is ticked */
static char __cdecl h_checkbox(const char *label, char *v) {
    InterlockedIncrement(&g_inflight);
    g_cb_state = v ? (*v ? 2 : 1) : 0;
    char r = o_checkbox(label, v);
    g_cb_state = 0;
    InterlockedDecrement(&g_inflight);
    return r;
}

/* ImGui::InputText: the port's text boxes (the room code, the username, the chats). Its ItemAdd is the box. */
static char __cdecl h_inputtext(const char *label, char *buf, size_t buf_size, int flags, void *callback, void *user_data) {
    InterlockedIncrement(&g_inflight);
    g_tb_buf = buf; g_tb_size = buf_size > 4096 ? 4096 : (int)buf_size;
    char r = o_inputtext(label, buf, buf_size, flags, callback, user_data);
    g_tb_buf = NULL;
    InterlockedDecrement(&g_inflight);
    return r;
}

/* ImGui::BeginTabItem(label, p_open, flags): its ItemAdd is the tab; true = the open tab */
static char __cdecl h_tabitem(const char *label, char *p_open, int flags) {
    InterlockedIncrement(&g_inflight);
    int before = g_nibuild;
    char r = o_tabitem(label, p_open, flags);
    if (before < g_nibuild) {
        ImItem *it = &g_ibuild[before];
        it->tab = r ? 2 : 1;
        size_t k = 0;                                /* the label up to "##" (its id part) */
        for (; label && label[k] && k < sizeof it->lab - 1 && !(label[k] == '#' && label[k + 1] == '#'); k++) it->lab[k] = label[k];
        it->lab[k] = 0;
    }
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
        for (int i = 0; i < g_nibuild; i++)
            log_line("IMI [%08x] %.0f,%.0f - %.0f,%.0f check %d", g_ibuild[i].id, g_ibuild[i].x0, g_ibuild[i].y0, g_ibuild[i].x1,
                     g_ibuild[i].y1, g_ibuild[i].check);
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
    t_checkbox = find_in_exe(rt, "?Checkbox@ImGui@@YA_NPEBDPEA_N@Z");
    if (!t_checkbox) t_checkbox = find_by_string(rt, "?Checkbox@ImGui@@YA_NPEBDPEA_N@Z", "[~]");
    t_tabitem = find_in_exe(rt, "?BeginTabItem@ImGui@@YA_NPEBDPEA_NH@Z");
    if (t_tabitem && !hook(t_tabitem, (void *)h_tabitem, (void **)&o_tabitem)) t_tabitem = NULL;
    if (t_checkbox && !hook(t_checkbox, (void *)h_checkbox, (void **)&o_checkbox)) t_checkbox = NULL;
    t_inputtext = find_inputtext(rt);
    if (t_inputtext && !hook(t_inputtext, (void *)h_inputtext, (void **)&o_inputtext)) t_inputtext = NULL;
    t_addtext_rt = (void *)GetProcAddress(rt, "?AddText@ImDrawList@@QEAAXPEAUImFont@@MAEBUImVec2@@IPEBD2MPEBUImVec4@@@Z");
    if (!hook(t_addtext_rt, (void *)h_addtext_rt, (void **)&o_addtext_rt)) t_addtext_rt = NULL;
    g_ready = 1;
    log_line("imgui: port screens hooked");
}

void im_shutdown(void) {
    void *ts[] = { t_addtext, t_itemadd, t_render, t_addtext_rt, t_checkbox, t_tabitem, t_inputtext };
    for (int i = 0; i < 7; i++) if (ts[i]) { MH_DisableHook(ts[i]); MH_RemoveHook(ts[i]); }
    g_ready = 0;
}

void im_dump(void) { g_dump = 1; }

/* ---------- mod thread ---------- */
static uint32_t nav_id(void) {
    uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
    return c ? *(uint32_t *)(c + g_off_nav) : 0;
}
static uint32_t active_id(void) {
    uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
    return c ? *(uint32_t *)(c + g_off_active) : 0;
}

/* The port only turns on ImGui's gamepad navigation: F1/F2 then answer the controller and the mouse, and the keyboard
 * reaches nothing but a text box that already has focus (the username could not be typed without a mouse). Keyboard
 * navigation (arrows move, Enter or Space choose, Escape backs out, Tab steps through) is ImGui's own; while a port
 * window is open the port keeps every input device from the game (mk9_overlay_input GameMayRead), so the keys do not
 * also move the game's menu behind it. Set again every poll: the port rebuilds the flags it owns. */
int im_menu_on(void);
static uint32_t nav_id(void);

/* Escape at the top of a port window (and some window changes) leave ImGui with no focused item: Tab then does
 * nothing at all, only an arrow key brings the focus back. To a player that is a window gone dead (2026-11-24).
 * So a Tab with nothing focused in an open port window sends one Down arrow, which focuses the first item. */
static void tab_revive(void) {
    static int was;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    int down = pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_TAB) & 0x8000);
    if (down && !was && im_menu_on() && !nav_id()) {
        INPUT in[2];
        memset(in, 0, sizeof in);
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = VK_DOWN;
        in[0].ki.dwFlags = in[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        in[1].ki.dwFlags |= KEYEVENTF_KEYUP;
        SendInput(1, &in[0], sizeof(INPUT));
        Sleep(40);
        SendInput(1, &in[1], sizeof(INPUT));
        log_line("imgui: Tab with nothing focused: Down sent");
    }
    was = down;
}

static void keyboard_nav(void) {
    uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
    if (!c) return;
    volatile int32_t *flags = (volatile int32_t *)(c + g_off_io);
    if (!(*flags & 1)) {
        InterlockedOr((volatile LONG *)flags, 1);   /* ImGuiConfigFlags_NavEnableKeyboard */
        log_line("imgui: keyboard navigation on");
    }
    tab_revive();
}

static ImT g_view[IMT_MAX];
static int g_nview;
static ImItem g_iview[IMI_MAX];
static int g_niview;
static volatile DWORD g_menu_text_t;    /* last frame with port-screen text (the Practice panels don't count) */

int im_menu_on(void) { return g_ready && GetTickCount() - g_menu_text_t < 300; }
static uint32_t g_said_nav;
static char g_said_item[1024];
static int g_said_tab = -1;             /* the open tab last said */
static DWORD g_head_t;                  /* when the open tab was last named */
static char g_said_title[160];          /* the tabbed window last said */
static char g_screen_sig[2048];

static int by_pos(const void *a, const void *b) {
    const ImT *x = a, *y = b;
    if (fabsf(x->y - y->y) > 6) return x->y < y->y ? -1 : 1;
    return x->x < y->x ? -1 : x->x > y->x;
}

/* ---------- windows with tabs ---------- */
#define NTAB 10
static struct { uint32_t id; char s[64]; } g_tab[NTAB];
static int g_ntab, g_tab_sel = -1;      /* the tabs of the window g_tab_title, kept while it is hidden */
static int g_tabs_live;                 /* that window is on screen now */
#define TABS_LIVE() (g_tabs_live && g_ntab >= 2)
static char g_tab_title[160];           /* the window whose tabs these are */
static volatile LONG g_tab_req;         /* Ctrl+Tab +1 / Ctrl+Shift+Tab -1, from keys.c's message hook */

/* tab names ImGui cut short for lack of room ("Display and langu"): the port's full ones */
static void tab_full_name(const char *s, char *out, size_t cap) {
    static const char *const full[] = { "Display and language", "Pantalla e idioma", "Tela e idioma", "Affichage et langue",
                                        "Anzeige und Sprache", "Schermo e lingua" };
    size_t n = strlen(s);
    for (size_t i = 0; n >= 6 && i < sizeof full / sizeof full[0]; i++)
        if (!strncmp(full[i], s, n)) { snprintf(out, cap, "%s", full[i]); return; }
    snprintf(out, cap, "%.63s", s);
}

/* The window's tab row: the first row of items under its title (3 or more texts with ids on one line), seen either
 * around the open tab's content (the tabs after the open one are drawn after it) or as a row known from before. The
 * open tab: the last of that row drawn before the first text off the row. Kept while the tabs are scrolled away. */
/* The tabs (marked by the BeginTabItem hook) of the window with the keyboard focus; with the focus elsewhere (a
 * dropdown list, nothing), of the tabbed window already in use, else of the first one. The title: the first text drawn
 * in that window (its title bar). A tab scrolled out of view keeps its name from before. */
static const void *g_tab_dl;
static void tabs_scan(void) {
    uint32_t nav = nav_id();
    const void *want = NULL;
    for (int i = 0; i < g_niview && !want; i++) if (nav && g_iview[i].id == nav) want = g_iview[i].dl;
    int has = 0;
    for (int i = 0; i < g_niview && !has; i++) has = g_iview[i].tab && g_iview[i].dl == want;
    if (!has) {
        want = NULL;
        for (int i = 0; i < g_niview && !want; i++) if (g_iview[i].tab && g_iview[i].dl == g_tab_dl) want = g_tab_dl;
        for (int i = 0; i < g_niview && !want; i++) if (g_iview[i].tab) want = g_iview[i].dl;
    }
    g_tabs_live = 0;
    if (!want) return;
    int idx[NTAB], n = 0;
    for (int i = 0; i < g_niview && n < NTAB; i++) if (g_iview[i].tab && g_iview[i].dl == want) idx[n++] = i;
    for (int x = 0; x < n; x++) for (int y = x + 1; y < n; y++)   /* left to right */
        if (g_iview[idx[y]].x0 < g_iview[idx[x]].x0) { int t = idx[x]; idx[x] = idx[y]; idx[y] = t; }
    char title[160] = "";
    for (int i = 0; i < g_nview && !title[0]; i++) if (g_view[i].dl == want) snprintf(title, sizeof title, "%.159s", g_view[i].s);
    int same = want == g_tab_dl && !strcmp(title, g_tab_title);
    static struct { uint32_t id; char s[64]; } old[NTAB];
    int nold = g_ntab;
    memcpy(old, g_tab, sizeof old);
    g_tab_sel = -1;
    for (int k = 0; k < n; k++) {
        const ImItem *it = &g_iview[idx[k]];
        g_tab[k].id = it->id;
        g_tab[k].s[0] = 0;
        for (int i = 0; i < g_nview && !g_tab[k].s[0]; i++)
            if (g_view[i].id == it->id) tab_full_name(g_view[i].s, g_tab[k].s, sizeof g_tab[k].s);
        for (int j = 0; j < nold && !g_tab[k].s[0] && same; j++)
            if (old[j].id == it->id) snprintf(g_tab[k].s, sizeof g_tab[k].s, "%s", old[j].s);
        if (!g_tab[k].s[0]) tab_full_name(it->lab, g_tab[k].s, sizeof g_tab[k].s);
        if (it->tab == 2) g_tab_sel = k;
    }
    g_ntab = n;
    g_tab_dl = want;
    snprintf(g_tab_title, sizeof g_tab_title, "%s", title);
    g_tabs_live = n >= 2;
}

static int tab_index(uint32_t id) {
    for (int k = 0; k < g_ntab; k++) if (g_tab[k].id == id) return k;
    return -1;
}

int im_tabs_on(void) { return im_menu_on() && TABS_LIVE(); }
void im_tab_step(int dir) { InterlockedExchange(&g_tab_req, dir); }

static void send_key(WORD vk) {
    INPUT in[2];
    memset(in, 0, sizeof in);
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(40);
    SendInput(1, &in[1], sizeof(INPUT));
}

/* Between two pieces of text: ", " on the same row, ". " on a new one, a space after a label's colon. */
static void join_sep(char *out, size_t cap, const ImT *prev, const ImT *next) {
    if (!out[0]) return;
    size_t n = strlen(out);
    const char *sep = out[n - 1] == ':' ? " " : prev && fabsf(prev->y - next->y) <= 6 ? ", " : ". ";
    strncat(out, sep, cap - n - 1);
}

/* ---------- text boxes ----------
 * Forum/user request 2026-11-27: pasting a room code into a box that already held something joined the wrong code
 * without a word. The box's text comes from its own buffer (the port's code_buf_ etc.): said when the box is reached,
 * typed and deleted characters echoed, and a paste that lands after earlier text called out. */
static int box_read(const ImItem *it, char *out, size_t cap) {
    out[0] = 0;
    if (!it || !it->buf || it->bufsz <= 0) return 0;
    size_t n = (size_t)it->bufsz < cap ? (size_t)it->bufsz : cap - 1;
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), it->buf, out, n, &got)) { out[0] = 0; return 0; }
    out[got < cap ? got : cap - 1] = 0;
    out[cap - 1] = 0;
    return 1;
}

static const ImItem *view_item(uint32_t id) {
    for (int i = 0; i < g_niview; i++) if (g_iview[i].id == id) return &g_iview[i];
    return NULL;
}

/* "R V A U J K" for a code or a name without spaces (up to 16 characters), the text itself for a sentence */
static void spell_text(const char *s, char *out, size_t cap) {
    size_t n = strlen(s);
    out[0] = 0;
    if (n > 16 || strchr(s, ' ')) { snprintf(out, cap, "%s", s); return; }
    size_t k = 0;
    for (size_t i = 0; i < n && k + 3 < cap; i++) {
        if (k) out[k++] = ' ';
        out[k++] = s[i];
    }
    out[k] = 0;
}

static void box_describe(const char *s, char *out, size_t cap) {
    if (!s[0]) { snprintf(out, cap, "empty"); return; }
    char sp[280]; spell_text(s, sp, sizeof sp);
    snprintf(out, cap, "has %s", sp);
}

static void char_name(char c, char *out, size_t cap) {
    if (c == ' ') snprintf(out, cap, "space");
    else snprintf(out, cap, "%c", c);
}

/* A room code (4 to 8 letters or digits, spaces around it ignored) on the clipboard, or 0. */
static int clipboard_code(char *out, size_t cap) {
    out[0] = 0;
    if (!OpenClipboard(NULL)) return 0;
    wchar_t t[32] = L"";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *p = h ? (const wchar_t *)GlobalLock(h) : NULL;
    if (p) { wcsncpy(t, p, 31); t[31] = 0; GlobalUnlock(h); }
    CloseClipboard();
    size_t k = 0;
    for (const wchar_t *c = t; *c && k + 1 < cap; c++) {
        if (*c == L' ' || *c == L'\r' || *c == L'\n' || *c == L'\t') continue;
        if (!iswalnum(*c) || *c > 127) { out[0] = 0; return 0; }
        out[k++] = (char)towupper(*c);
    }
    out[k] = 0;
    return k >= 4 && k <= 8;
}

/* The focused item's text. A button in a row of a table (the room list's "Watch" / "Enter") or after a label
 * ("Join with a code:" ... "Join") gets the row's plain text before it, as a sighted player reads the row. */
/* Returns 0 when the item has no text of its own (a text box). */
static int join_item(uint32_t id, char *out, size_t cap) {
    out[0] = 0;
    const ImItem *box = NULL;
    for (int i = 0; i < g_niview; i++) if (g_iview[i].id == id) { box = &g_iview[i]; break; }
    const void *own_dl = NULL;
    for (int i = 0; i < g_nview && !own_dl; i++) if (g_view[i].id == id) own_dl = g_view[i].dl;
    if (box) {
        static ImT row[24];
        int n = 0;
        for (int i = 0; i < g_nview && n < 24; i++)
            if (!g_view[i].id && g_view[i].y >= box->y0 - 6 && g_view[i].y <= box->y1 && g_view[i].x < box->x0 &&
                (!own_dl || g_view[i].dl == own_dl))
                row[n++] = g_view[i];
        qsort(row, n, sizeof(ImT), by_pos);
        /* from the last label ("You: BlindChallengerX, Change", not the connection status before it) */
        int from = 0;
        for (int i = 0; i < n; i++) { size_t l = strlen(row[i].s); if (l && row[i].s[l - 1] == ':') from = i; }
        for (int i = from; i < n; i++) {
            join_sep(out, cap, i > from ? &row[i - 1] : NULL, &row[i]);
            strncat(out, row[i].s, cap - strlen(out) - 1);
        }
        /* a text box earlier in the row: what it holds ("Join with a code: R V A U J K, Join") */
        if (!box->buf)
            for (int i = 0; i < g_niview; i++) {
                const ImItem *tb = &g_iview[i];
                if (!tb->buf || tb->x1 > box->x0 + 1 || tb->y1 < box->y0 || tb->y0 > box->y1 || (box->dl && tb->dl != box->dl)) continue;
                char s[256], d[300]; box_read(tb, s, sizeof s);
                if (s[0]) spell_text(s, d, sizeof d); else snprintf(d, sizeof d, "box empty");
                if (out[0]) strncat(out, out[strlen(out) - 1] == ':' ? " " : ", ", cap - strlen(out) - 1);
                strncat(out, d, cap - strlen(out) - 1);
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
    if (own0 >= 0 && box && box->check)
        strncat(out, box->check == 2 ? ", checkbox, checked" : ", checkbox, not checked", cap - strlen(out) - 1);
    /* its help: the plain lines right under it, before the next row of items (F2 draws each option's help there) */
    if (own0 >= 0 && box && tab_index(id) < 0) {
        float next = 1e9f, left = box->x0;
        for (int i = 0; i < g_niview; i++) {
            const ImItem *it = &g_iview[i];
            if (it->id != id && it->y0 > box->y1 - 1 && it->y0 < next && it->x1 > box->x0 - 900 && it->x0 < box->x1 + 900)
                next = it->y0;                   /* the next row of items in this window, whatever its column */
        }
        for (int i = 0; i < g_nview; i++)        /* the row's label starts the column */
            if (!g_view[i].id && g_view[i].y >= box->y0 - 6 && g_view[i].y <= box->y1 && g_view[i].x < left) left = g_view[i].x;
        int lines = 0;
        uint32_t own_col = g_view[own0].col;
        for (int i = 0; i < g_nview && lines < 4; i++) {
            const ImT *t = &g_view[i];
            if (t->id || t->y < box->y1 - 2 || t->y >= next - 2 || t->x < left - 4 || t->x > box->x1) continue;
            if (t->col == own_col || (own_dl && t->dl != own_dl)) continue;   /* a heading of what follows, another window */
            if (strstr(out, t->s)) continue;
            strncat(out, ". ", cap - strlen(out) - 1);
            strncat(out, t->s, cap - strlen(out) - 1);
            lines++;
        }
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
                     /* Tab into a text box starts typing in it at once (ImGui); arrows only focus it. Before 1.0.8
                      * the hint always said "Enter to type": after Tab that Enter closed the box again (2026-11-24). */
                     !wide ? "picture button" : active_id() == id ? "text box, typing. Type, then press Enter." :
                     "text box. Enter to type in it, Enter again when done.", cap - strlen(out) - 1);
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
    /* the player's own F5 / F6 / F7: past the fight silence (1.0.9 kept these quiet in Practice by mistake) */
    { int was = speech_force(1); say_rec_change(&v); speech_force(was); }
    g_rec = v;
    g_rec_panel_t = GetTickCount();
}

/* "Startup 7. On block minus 3. Recovery 15." from label / value rows (a value is drawn after its label, same line). */
/* Recovery (user request 2026-11-25: "read recovery frames instead of the total"). The port's panel shows startup,
 * the advantage and the total; recovery is measured here with the same rules as the port (mk9_frame_data.cpp), once a
 * game frame on the game thread (fd_frame, from the pad read): an attack starts at a new move id while held (frame 1),
 * its active part at the first frame its phase (+0x2F74) leaves 1, its recovery at the first frame the phase is 3 (a
 * contact puts it there at once), and it ends at the first frame the fighter is free (action kind without 0x40).
 * startup = active frame, total = free frame, recovery = free - recovery start. The panel's line is read with the
 * recovery only when this measure has the panel's own startup and total (the same attack); else with the total.
 * The pad is read about twice a game frame, so a pass counts only when a fighter's animation frame (+0x2D4) moved. */
typedef struct { uint32_t move; int on, n, a, e; } FdRun;
typedef struct { int startup, total, rec; } FdDone;
static FdRun g_fdr[2];
static FdDone g_fdd[2];

void fd_frame(void) {
    if (GetTickCount() - g_fd_panel_t > 3000) return;      /* Practice's panel not up */
    static uint32_t last_anim[2], prev_move[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }, prev_ph[2];
    uint32_t obj[2], anim[2];
    int moved = 0;
    for (int p = 0; p < 2; p++) {
        obj[p] = g32(0x833C6C54u + (uint32_t)p * 0xC8Cu + 0xDF4u);
        if (obj[p] < 0x30000000u || obj[p] >= 0xA0000000u) { obj[p] = 0; continue; }
        anim[p] = g32(obj[p] + 0x2D4u);
        if (anim[p] != last_anim[p]) moved = 1;
        last_anim[p] = anim[p];
    }
    if (!moved) return;                                     /* the same game frame again, or paused */
    for (int p = 0; p < 2; p++) {
        FdRun *m = &g_fdr[p];
        if (!obj[p]) { m->on = 0; continue; }
        uint32_t move = g32(obj[p] + 0x188u), act = g32(obj[p] + 0x1D4u), ph = g32(obj[p] + 0x2F74u);
        uint32_t kind = (act >> 8) & 0xFF;
        int held = (kind & 0x40) != 0, hit = (kind & 0x50) == 0x40;
        /* a new attack: a move id it did not have, held, not hit. An attack in parts (Scorpion's 4: a step, then the
         * kick) changes its id before it reaches its recovery: the same attack still (as the port) */
        if (move != 0xFFFFFFFFu && move != prev_move[p] && held && !hit) {
            if (m->on && !m->e) m->move = move;
            else { m->on = 1; m->move = move; m->n = 0; m->a = m->e = 0; }
        }
        prev_move[p] = move;
        if (m->on) m->n++;
        if (m->on && hit) m->on = 0;                        /* hit while attacking: no measure (as the port) */
        if (m->on) {
            if (!m->a && move == m->move && (ph & 2) && !(prev_ph[p] & 2)) m->a = m->n;
            if (!m->e && ph == 3) { m->e = m->n; if (!m->a) m->a = m->n; }   /* a string's next hit: its contact */
            if (!held) {
                if (m->a && m->e) {
                    g_fdd[p].startup = m->a; g_fdd[p].total = m->n; g_fdd[p].rec = m->n - m->e;
                    log_line("framedata: p%d startup %d recovery-start %d free %d", p + 1, m->a, m->e, m->n);
                }
                m->on = 0;
            }
        }
        prev_ph[p] = ph;
    }
}

static void read_fd(const ImT *fd, int n) {
    char out[256] = "";
    int flash = 0, rival = 0, startup = -1, total = -1, last_num = -1;
    for (int i = 0; i < n; i++)
        if (starts(fd[i].s, "FRAME DATA") && (strstr(fd[i].s, "OPPONENT") || strstr(fd[i].s, "RIVAL"))) rival = 1;
    {   /* rows are "label number": the first row's number is the startup, the last row's the total */
        int nums[4], nn = 0;
        for (int i = 0; i < n && nn < 4; i++) if (is_number(fd[i].s)) nums[nn++] = i;
        if (nn >= 2) { startup = atoi(fd[nums[0]].s); total = atoi(fd[nums[nn - 1]].s); last_num = nums[nn - 1]; }
    }
    const FdDone *d = &g_fdd[rival];
    int rec = d->startup == startup && d->total == total && total > 0 ? d->rec : -1;
    for (int i = 0; i < n; i++) {
        const char *s = fd[i].s;
        if (rec >= 0 && i == last_num - 1) {                /* "Total" and its number -> "Recovery N" */
            if (out[0] && out[strlen(out) - 1] != ' ') strncat(out, ". ", sizeof out - strlen(out) - 1);
            char r[48]; snprintf(r, sizeof r, "Recovery %d", rec);
            strncat(out, r, sizeof out - strlen(out) - 1);
            uint32_t c = fd[last_num].col;
            if ((c & 0xFF) >= 0xFC && ((c >> 8) & 0xFF) >= 0xFC && ((c >> 16) & 0xFF) >= 0xFC) flash = 1;
            i = last_num;
            continue;
        }
        if (starts(s, "FRAME DATA")) {
            if (strstr(s, "OPPONENT") || strstr(s, "RIVAL")) strncat(out, "Opponent. ", sizeof out - strlen(out) - 1);
            continue;
        }
        if (is_number(s)) {
            uint32_t c = fd[i].col;   /* a new result flashes white for a moment */
            if ((c & 0xFF) >= 0xFC && ((c >> 8) & 0xFF) >= 0xFC && ((c >> 16) & 0xFF) >= 0xFC) flash = 1;
            char v[48];
            if (!strcmp(s + (s[0] == '+' || s[0] == '-'), "0")) snprintf(v, sizeof v, " 0");
            else if (s[0] == '-' && s[1]) snprintf(v, sizeof v, " minus %.40s", s + 1);
            else if (s[0] == '+') snprintf(v, sizeof v, " plus %.40s", s + 1);
            else snprintf(v, sizeof v, " %.40s", s);
            strncat(out, v, sizeof out - strlen(out) - 1);
            continue;
        }
        if (out[0] && out[strlen(out) - 1] != ' ') strncat(out, ". ", sizeof out - strlen(out) - 1);
        strncat(out, s, sizeof out - strlen(out) - 1);
    }
    strcpy(g_fd_text, out);
    if (flash && !g_fd_flash && g_fd_auto) {           /* asked for with Shift+F10: past the fight silence */
        int was = speech_force(1); speak_utf8(out, 1); speech_force(was);
    }
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
            speakf(0, "Connected to %s. F5 reads the connection. To leave, press F1 and choose End the session.", g_badge_rival);
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

const char *im_rival_name(void) { return g_badge_rival; }

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
static char g_pend[4096], g_pend_item[1400];
static DWORD g_pend_t;
static int g_pend_on, g_pend_hint, g_pend_sig;   /* g_pend_sig: g_pend is the whole screen */

static void pending_flush(void) {
    if (!g_pend_on || GetTickCount() - g_pend_t < SETTLE_MS) return;
    g_pend_on = 0;
    if (g_pend_sig && TABS_LIVE()) return;           /* tabbed after all: im_poll says its name and tab instead */
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

/* The focused item as spoken: a text box adds what it holds right after "text box" (kept out of the text compared
 * between frames, so typing doesn't re-read the whole item), and a room-code box mentions a code on the clipboard. */
static void item_with_box(uint32_t nav, const char *item, char *out, size_t cap, int clip_hint) {
    snprintf(out, cap, "%s", item);
    const ImItem *it = view_item(nav);
    const char *at = strstr(item, "text box");
    if (!it || !it->buf || !at) return;
    char s[256], d[300], clip[16], tail[320] = "";
    box_read(it, s, sizeof s);
    box_describe(s, d, sizeof d);
    if (clip_hint && (strstr(item, "code") || strstr(item, "Code") || strstr(item, "digo")) && clipboard_code(clip, sizeof clip) &&
        _stricmp(clip, s)) {
        char sp[40]; spell_text(clip, sp, sizeof sp);
        snprintf(tail, sizeof tail, s[0] ? " The clipboard has the code %s: Control A selects what is in the box, then "
                                           "Control V replaces it." : " The clipboard has the code %s: Control V pastes it.", sp);
    }
    size_t pre = (size_t)(at - item) + strlen("text box");
    snprintf(out, cap, "%.*s, %s%s%s", (int)pre, item, d, item + pre, tail);
}

/* While the focused box is typed in: each character typed or deleted, and what a paste did. */
static void box_poll(uint32_t nav) {
    static uint32_t id;
    static char last[256];
    static int known;
    const ImItem *it = nav ? view_item(nav) : NULL;
    if (!it || !it->buf) { id = 0; known = 0; return; }
    char s[256]; box_read(it, s, sizeof s);
    if (nav != id || !known) { id = nav; snprintf(last, sizeof last, "%s", s); known = 1; return; }
    if (!strcmp(s, last)) return;
    size_t a = strlen(last), b = strlen(s);
    char msg[700], c[16];
    if (b == a + 1 && !strncmp(s, last, a)) { char_name(s[a], c, sizeof c); snprintf(msg, sizeof msg, "%s", c); }
    else if (a == b + 1 && !strncmp(s, last, b)) { char_name(last[b], c, sizeof c); snprintf(msg, sizeof msg, "%s deleted", c); }
    else if (!b) {
        /* sent (a chat line) or cleared: Enter just now means sent, already heard as a new line */
        msg[0] = 0;
        if (!(GetAsyncKeyState(VK_RETURN) & 0x8000)) snprintf(msg, sizeof msg, "Box empty");
    } else {
        char sp[300], old[300]; spell_text(s, sp, sizeof sp); spell_text(last, old, sizeof old);
        if (a && b > a && !strncmp(s, last, a))
            snprintf(msg, sizeof msg, "Added to what was there. The box had %s, now it has %s. Control A then Control V "
                                      "replaces it all.", old, sp);
        else snprintf(msg, sizeof msg, "Box has %s", sp);
    }
    if (msg[0]) speak_utf8(msg, 1);
    log_line("imgui: text box %08x \"%s\" -> \"%s\"", nav, last, s);
    snprintf(last, sizeof last, "%s", s);
}

/* Joining a room (by code, from the list, quick match) and someone joining the host's room: a rising chime when it
 * worked, a low falling pair when it didn't, on top of the status line the window shows (and the mod reads).
 * Matched on the port's status texts in English, Spanish and Portuguese (mk9-lauta room.cpp, i18n.cpp). */
static int line_is(const char *s, const char *const *pats) {
    for (; *pats; pats++) if (strstr(s, *pats)) return 1;
    return 0;
}
/* "Room ZQ7XK2 does not answer" -> "Room Z Q 7 X K 2 does not answer": a room code (4 to 8 capitals and digits, with a
 * digit) spelled */
static void spell_codes(const char *s, char *out, size_t cap) {
    size_t k = 0;
    while (*s && k + 2 < cap) {
        size_t w = 0; int digit = 0, ok = 1;
        while (s[w] && s[w] != ' ' && s[w] != '.' && s[w] != ',') {
            if (isdigit((unsigned char)s[w])) digit = 1; else if (!isupper((unsigned char)s[w])) ok = 0;
            w++;
        }
        if (ok && digit && w >= 4 && w <= 8) {
            for (size_t i = 0; i < w && k + 3 < cap; i++) { if (i) out[k++] = ' '; out[k++] = s[i]; }
            s += w;
        } else {
            for (size_t i = 0; i < w && k + 2 < cap; i++) out[k++] = s[i];
            s += w;
            if (*s && k + 2 < cap) out[k++] = *s++;
        }
    }
    out[k] = 0;
}

static char g_join_msg[300];                 /* said on the next poll, after whatever this frame's reading says */
static char g_guest_line[120];               /* the guest's "In X's room" line */

static void join_sounds(const ImT *lines, int n, int had) {
    static const char *const ok[] = { "Opponent in the room", "Rival en la sala", "Rival na sala", NULL };
    static const char *const bad[] = { "does not answer", "no responde", "o responde", "already has two players",
                                       "ya tiene dos jugadores", "tem dois jogadores", "is already full", "ya est\xc3\xa1 completa",
                                       "est\xc3\xa1 completa", "The code must have", "digo tiene que tener", "digo precisa ter", NULL };
    static int was_ok, was_bad;
    static DWORD last_t;
    int now_ok = 0, now_bad = 0;
    const char *ok_line = NULL, *bad_line = NULL;
    for (int i = 0; i < n; i++) {
        const char *s = lines[i].s;
        size_t l = strlen(s);
        int guest = 0;
        /* the guest's status "In X's room" / "En la sala de X" / "Na sala de X" (the chat's "You joined X's room"
         * stays in the history and scrolls, so the status line is the one used) */
        if ((!strncmp(s, "In ", 3) && l > 10 && !strcmp(s + l - 7, "'s room")) || !strncmp(s, "En la sala de ", 14) ||
            !strncmp(s, "Na sala de ", 11)) guest = 1;
        if (guest || line_is(s, ok)) { now_ok = 1; ok_line = s; if (guest) ok_line = NULL; }
        if (guest) snprintf(g_guest_line, sizeof g_guest_line, "%s", s);
        if (line_is(s, bad)) { now_bad = 1; bad_line = s; }
    }
    DWORD t = GetTickCount();
    /* only a change on a window already showing (opening F1 on an old status is no news), one sound per 3 s */
    if (had && t - last_t > 3000) {
        if (now_ok && !was_ok) {
            tone_at(0.00f, 523, 523, 0.10f, 0.11f, 0, W_SOFT);
            tone_at(0.09f, 659, 659, 0.10f, 0.11f, 0, W_SOFT);
            tone_at(0.18f, 784, 784, 0.22f, 0.12f, 0, W_SOFT);
            /* the host's "Opponent in the room" is read with the room already: the chime alone */
            if (ok_line) g_join_msg[0] = 0;
            else if (!strncmp(g_guest_line, "In ", 3)) snprintf(g_join_msg, sizeof g_join_msg, "Joined %s", g_guest_line + 3);
            else snprintf(g_join_msg, sizeof g_join_msg, "Joined. %s", g_guest_line);
            log_line("imgui: joined a room (sound)"); last_t = t;
        } else if (now_bad && !was_bad) {
            tone_at(0.00f, 247, 233, 0.16f, 0.13f, 0, W_SOFT);
            tone_at(0.17f, 185, 165, 0.28f, 0.13f, 0, W_SOFT);
            char sp[280]; spell_codes(bad_line ? bad_line : "", sp, sizeof sp);
            snprintf(g_join_msg, sizeof g_join_msg, "Could not join. %s", sp);
            log_line("imgui: joining failed (sound)"); last_t = t;
        }
    }
    was_ok = now_ok; was_bad = now_bad;
}

void im_poll(void) {
    if (!g_ready) return;
    if (g_join_msg[0]) { speak_utf8(g_join_msg, 0); g_join_msg[0] = 0; }
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
    for (int i = 0; i < g_nview; i++) g_view[i].ord = i;
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
            g_said_tab = -1; g_said_title[0] = 0;   /* the tabs stay known: the same window reopens tabbed */
        }
        return;
    }
    tabs_scan();
    pending_flush();
    /* Ctrl+Tab: the next tab is focused and chosen (Enter), as a mouse click on it would */
    LONG req = InterlockedExchange(&g_tab_req, 0);
    if (req && TABS_LIVE()) {
        int cur = g_tab_sel >= 0 ? g_tab_sel : 0, t = (cur + (int)req + g_ntab) % g_ntab;
        uint8_t *c = p_ctx ? (uint8_t *)p_ctx() : NULL;
        if (c) {
            *(volatile uint32_t *)(c + g_off_nav) = g_tab[t].id;
            Sleep(20);
            send_key(VK_RETURN);
            g_tab_sel = t; g_said_tab = t; g_head_t = GetTickCount();
            speakf(1, "%s tab, %d of %d", g_tab[t].s, t + 1, g_ntab);
            g_said_nav = g_tab[t].id;
            char said[200];
            snprintf(said, sizeof said, "%s tab, %d of %d, open", g_tab[t].s, t + 1, g_ntab);
            fold_digits(said, g_said_item, sizeof g_said_item);
            log_line("imgui: Ctrl+Tab %+ld -> tab %d %s", (long)req, t + 1, g_tab[t].s);
            return;
        }
    }
    /* the plain lines in reading order */
    static ImT cur[IMT_MAX];
    int n = 0;
    for (int i = 0; i < g_nview; i++) if (!g_view[i].id) cur[n++] = g_view[i];
    qsort(cur, n, sizeof(ImT), by_pos);
    join_sounds(cur, n, g_nprev > 0);
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
    /* The focused button gone (Create private room, once the room opens; I'm ready, which becomes Cancel ready):
     * ImGui keeps its id, and Tab does nothing until an arrow key is pressed. After 300 ms the focus moves to the
     * labelled item now where the old one was (the renamed button), or, with nothing there, to the window's first
     * button below its title (before 1.0.8 always the first: I'm ready threw the player to the top). */
    {
        static DWORD lost_t;
        static ImItem last;                              /* the focused item's box while it was there */
        int alive = !nav;
        for (int i = 0; i < g_niview && !alive; i++) if (g_iview[i].id == nav) { alive = 1; last = g_iview[i]; }
        if (alive || !g_niview) lost_t = 0;
        else if (!lost_t) lost_t = GetTickCount();
        else if (GetTickCount() - lost_t > 300) {
            /* the two topmost labelled items: the title bar, then the first button; and the one over the old box */
            const ImItem *top[2] = { NULL, NULL }, *same = NULL;
            float same_d = 1e9f;
            for (int i = 0; i < g_niview; i++) {
                const ImItem *it = &g_iview[i];
                int texted = 0;
                for (int j = 0; j < g_nview && !texted; j++) texted = g_view[j].id == it->id;
                if (!texted) continue;
                #define ABOVE(a, b) (!(b) || (a)->y0 < (b)->y0 - 2 || (fabsf((a)->y0 - (b)->y0) <= 2 && (a)->x0 < (b)->x0))
                if (ABOVE(it, top[0])) { top[1] = top[0]; top[0] = it; }
                else if (ABOVE(it, top[1])) top[1] = it;
                #undef ABOVE
                /* the renamed button starts exactly where the old one did; something merely overlapping its box (a
                 * table's header row under a vanished Leave the room) may sit in a child window, where Tab is dead */
                if (last.id && fabsf(it->x0 - last.x0) <= 3 && fabsf(it->y0 - last.y0) <= 3) {
                    float d = fabsf(it->x0 - last.x0) + fabsf(it->y0 - last.y0);
                    if (d < same_d) { same_d = d; same = it; }
                }
            }
            const ImItem *best = same ? same : top[1];
            last.id = 0;
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
    int ti = nav ? tab_index(nav) : -1;
    if (ti >= 0)
        snprintf(item, sizeof item, "%s tab, %d of %d%s", g_tab[ti].s, ti + 1, g_ntab,
                 ti == g_tab_sel ? ", open" : ". Enter opens it");
    else if (nav) join_item(nav, item, sizeof item);
    box_poll(nav);
    char say_item[1400];
    item_with_box(nav, item, say_item, sizeof say_item, 0);
    /* a window with tabs is not read all at once (F2's Graphics tab is a page of help texts): its name and the open
     * tab, then the focused item with its help; F5 reads all of it */
    char head[400] = "";
    int tabbed = TABS_LIVE();
    if (tabbed) {
        if (strcmp(g_tab_title, g_said_title)) {
            snprintf(head, sizeof head, "%s. %s tab, %d of %d. Control Tab changes tabs, F5 reads the tab.", g_tab_title,
                     g_tab_sel >= 0 ? g_tab[g_tab_sel].s : g_tab[0].s, (g_tab_sel >= 0 ? g_tab_sel : 0) + 1, g_ntab);
            snprintf(g_said_title, sizeof g_said_title, "%s", g_tab_title);
            g_said_tab = g_tab_sel;
        } else if (g_tab_sel >= 0 && g_tab_sel != g_said_tab) {
            snprintf(head, sizeof head, "%s tab, %d of %d", g_tab[g_tab_sel].s, g_tab_sel + 1, g_ntab);
            g_said_tab = g_tab_sel;
        }
    } else g_said_title[0] = 0;
    if (head[0]) g_head_t = GetTickCount();
    int skip_item = ti >= 0 && ti == g_tab_sel && GetTickCount() - g_head_t < 2000;   /* the open tab: just named */
    int had = g_nprev > 0 || g_screen_sig[0];
    for (int i = 0; i < n; i++) strcpy(g_prev[i], folded[i]);
    g_nprev = n;
    snprintf(g_screen_sig, sizeof g_screen_sig, "%s", sig);
    if (!had || (fresh && kept * 2 < n)) {
        /* a new screen: all of it (a tabbed window: its name and tab), then the focused item (once it settles) */
        snprintf(g_pend, sizeof g_pend, "%s", tabbed ? head : sig);
        g_pend_sig = !tabbed;
        {
            char f2[1024]; fold_digits(item, f2, sizeof f2);
            if (tabbed && nav == g_said_nav && !strcmp(f2, g_said_item)) skip_item = 1;   /* said with Ctrl+Tab */
        }
        snprintf(g_pend_item, sizeof g_pend_item, "%s", skip_item ? "" : (item_with_box(nav, item, say_item, sizeof say_item, 1), say_item));
        g_pend_hint = g_loaded_said && im_name_prompt_open();   /* before that, dllmain says it */
        g_pend_t = GetTickCount(); g_pend_on = 1;
        g_said_nav = nav; fold_digits(item, g_said_item, sizeof g_said_item);
        return;
    }
    /* the focused item first (a counter ticking in its row does not make it new), then the new lines */
    char fitem[1024];
    fold_digits(item, fitem, sizeof fitem);
    if (g_pend_on) {                                   /* the screen is still settling: it will say all of it */
        if (!tabbed) { snprintf(g_pend, sizeof g_pend, "%s", sig); g_pend_sig = 1; }
        else if (head[0]) { snprintf(g_pend, sizeof g_pend, "%s", head); g_pend_sig = 0; }
        snprintf(g_pend_item, sizeof g_pend_item, "%s", skip_item ? "" : (item_with_box(nav, item, say_item, sizeof say_item, 1), say_item));
        g_said_nav = nav; strcpy(g_said_item, fitem);
        return;
    }
    int moved = nav != g_said_nav;
    /* Enter in a text box ends the typing (its hint changes) and sends it: the box isn't read again over the result */
    static DWORD enter_t;
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) enter_t = GetTickCount();
    if (!moved && strcmp(fitem, g_said_item) && GetTickCount() - enter_t < 700 && strstr(fitem, "text box") &&
        strstr(g_said_item, "text box")) strcpy(g_said_item, fitem);
    if (head[0]) speak_utf8(head, 1);                  /* another tab opened (by Enter on it, or the controller's LB/RB) */
    if (moved || strcmp(fitem, g_said_item)) {
        if (item[0] && !skip_item) { item_with_box(nav, item, say_item, sizeof say_item, 1); speak_utf8(say_item, !head[0]); }
        g_said_nav = nav; strcpy(g_said_item, fitem);
    }
    /* lines that appear as the focus moves are the window scrolling to it, not news; a tooltip (drawn after
     * every item, in its own window) still is */
    const void *nav_dl = NULL;
    for (int i = 0; i < g_nview && !nav_dl; i++) if (nav && g_view[i].id == nav) nav_dl = g_view[i].dl;
    #define OVERLAY(t) (nav_dl && (t)->dl != nav_dl)
    if (fresh) {
        /* the same screen with new lines (a chat message, someone entering the room, a room in the list, a status
         * line, the focused button's tooltip): only those, each with the rest of its row ("Room of X, X, about
         * 191 ms, waiting", not a bare "waiting"), after whatever is being said */
        char add[2048] = "";
        static unsigned char done[IMT_MAX];
        memset(done, 0, sizeof done);
        for (int i = 0; i < n; i++) {
            if (!is_new[i] || done[i]) continue;
            if (OVERLAY(&cur[i])) {                    /* a tooltip: on its own, not with the row it lies over */
                done[i] = 1;
                if (strstr(item, cur[i].s)) continue;
                if (add[0]) strncat(add, ". ", sizeof add - strlen(add) - 1);
                strncat(add, cur[i].s, sizeof add - strlen(add) - 1);
                continue;
            }
            if (moved) { done[i] = 1; continue; }
            const ImT *prev = NULL;
            for (int j = 0; j < n; j++) {
                if (fabsf(cur[j].y - cur[i].y) > 6 || cur[j].dl != cur[i].dl) continue;
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
