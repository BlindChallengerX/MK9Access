/*
 * The runtime's system dialogs (XamShowMessageBoxUI): the game's own questions
 * such as "Profile Selection. You are not signed in... Do you want to sign in
 * now?" with "Sign in" / "Continue without saving". The SDK draws them with
 * rexruntimerd.dll's own ImGui (sdk/src/kernel/xam/xam_ui.cpp MessageBoxDialog):
 * mouse only, no controller or arrow keys, and nothing imgui.c reads, so the
 * game looked frozen while it waited for an answer.
 *
 * Here:
 * - mk9.exe's import of __imp__XamShowMessageBoxUI is wrapped (as pad.c wraps
 *   XamInputGetState) to read the title, text and buttons, speak them and log
 *   which user was asked. The original still shows the dialog.
 * - The player picks with Left/Right or Up/Down (keyboard, D-pad or stick) and
 *   confirms with A, Enter, Space or J. The runtime's ImGui::ButtonEx is hooked
 *   and reports the chosen button as pressed, so the dialog closes through its
 *   own path (it writes the result and completes the request itself).
 */
#include "access.h"
#include <stdio.h>
#include <string.h>
#include "MinHook.h"

#define MB_MAX 4
#define MB_LEN 128

static guest_fn o_show;
static ULONG_PTR *g_iat_slot;

typedef char (__cdecl *buttonex_fn)(const char *label, const void *size, int flags);
static buttonex_fn o_buttonex;
static void *t_buttonex;

static char g_btn[MB_MAX][MB_LEN];      /* button labels, UTF-8 as the SDK draws them */
static volatile int g_nbtn;
static volatile int g_pending;          /* a dialog is up and unanswered */
static volatile int g_focus;            /* the player's choice */
static volatile int g_press = -1;       /* button to report pressed on the next draw */
static volatile DWORD g_shown_t, g_seen_t;

typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } XPAD;
typedef struct { DWORD packet; XPAD pad; } XSTATE;
typedef DWORD (WINAPI *xget_fn)(DWORD, XSTATE *);
static xget_fn p_xget;

/* A guest UTF-16 big-endian string as UTF-8. */
static void guest_utf8(uint32_t a, char *out, size_t cap) {
    wchar_t w[512];
    int n = 0;
    out[0] = 0;
    if (!a) return;
    for (; n < 511; n++) {
        if (!guest_ok(a + 2 * (uint32_t)n, 2)) break;
        uint16_t c = g16(a + 2 * (uint32_t)n);
        if (!c) break;
        w[n] = (wchar_t)c;
    }
    w[n] = 0;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    out[cap - 1] = 0;
}

static void say_focus(int interrupt) {
    int n = g_nbtn, f = g_focus;
    if (f < 0 || f >= n) return;
    speakf(interrupt, "%s, %d of %d", g_btn[f], f + 1, n);
}

/* XamShowMessageBoxUI(user, title, text, button_count, buttons, active_button, flags, result, overlapped) */
static void h_show(PPCContext *c, uint8_t *b) {
    uint32_t user = (uint32_t)c->r3, title = (uint32_t)c->r4, text = (uint32_t)c->r5;
    uint32_t count = (uint32_t)c->r6, list = (uint32_t)c->r7, active = (uint32_t)c->r8;
    char t[MB_LEN * 2], body[1024];
    guest_utf8(title, t, sizeof t);
    guest_utf8(text, body, sizeof body);
    int n = count > MB_MAX ? MB_MAX : (int)count;
    for (int i = 0; i < n; i++) guest_utf8(g32(list + 4 * (uint32_t)i), g_btn[i], MB_LEN);
    log_line("sysdialog: user %u asked \"%s\": \"%s\" (%u buttons, focus %u)", user, t, body, count, active);
    for (int i = 0; i < n; i++) log_line("sysdialog:   button %d \"%s\"", i, g_btn[i]);
    g_nbtn = n;
    g_focus = active < (uint32_t)n ? (int)active : 0;
    g_press = -1;
    g_seen_t = 0;
    g_shown_t = GetTickCount();
    if (n > 0) {
        g_pending = 1;
        char line[2048];
        snprintf(line, sizeof line, "%s. %s", t, body);
        speak_utf8(line, 1);
        /* The profile question (XenonSaveMsgs ContinueWithoutGamerProfile). The SDK's sign-in UI shows nothing and
         * returns the game to its main menu (xam_user.cpp); continuing may leave that player without saves. */
        if (n == 2 && strstr(body, "signed in"))
            speak_utf8("Sign in returns to the main menu. Continue without saving goes on, but progress may not be "
                       "saved this session.", 0);
        say_focus(0);
        speak_utf8(n > 1 ? "Left and right to choose, A or Enter to confirm." : "A or Enter to confirm.", 0);
    }
    CALL_ORIG(o_show, c, b);
}

/* Render thread: each frame the dialog draws its buttons. */
static char __cdecl h_buttonex(const char *label, const void *size, int flags) {
    InterlockedIncrement(&g_inflight);
    char r = o_buttonex(label, size, flags);
    if (g_pending && label) {
        for (int i = 0; i < g_nbtn; i++) {
            if (strcmp(label, g_btn[i])) continue;
            g_seen_t = GetTickCount();
            if (g_press == i) {
                g_press = -1;
                g_pending = 0;
                r = 1;
                log_line("sysdialog: answered \"%s\"", g_btn[i]);
            } else if (r) {                       /* clicked with the mouse */
                g_pending = 0;
                log_line("sysdialog: clicked \"%s\"", g_btn[i]);
            }
            break;
        }
    }
    InterlockedDecrement(&g_inflight);
    return r;
}

int sysdialog_recent(void) { return g_pending || (g_shown_t && GetTickCount() - g_shown_t < 2000); }

/* Mod thread: the player's choice. */
void sysdialog_poll(void) {
    static int kdown[7];
    static WORD pad_prev;
    if (!g_pending) { pad_prev = 0xFFFF; memset(kdown, 0, sizeof kdown); return; }
    DWORD now = GetTickCount();
    /* closed some other way, or never drawn (no ImGui: the SDK answers by itself) */
    if ((g_seen_t && now - g_seen_t > 1500) || (!g_seen_t && now - g_shown_t > 5000)) {
        g_pending = 0;
        log_line("sysdialog: closed");
        return;
    }
    if (g_press >= 0) return;                     /* waiting for the next draw */
    int move = 0, ok = 0;
    if (game_has_focus()) {
        static const int keys[7] = { VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_RETURN, VK_SPACE, 'J' };
        for (int i = 0; i < 7; i++) {
            int d = GetAsyncKeyState(keys[i]) < 0;
            if (d && !kdown[i]) { if (i < 2) move = -1; else if (i < 4) move = 1; else ok = 1; }
            kdown[i] = d;
        }
    }
    if (p_xget) {
        WORD pad = 0;
        for (DWORD u = 0; u < 4; u++) {
            XSTATE s;
            if (p_xget(u, &s)) continue;
            pad |= s.pad.buttons;
            if (s.pad.lx < -16000 || s.pad.ly > 16000) pad |= 0x4;   /* stick as D-pad left */
            if (s.pad.lx > 16000 || s.pad.ly < -16000) pad |= 0x8;   /* and right */
        }
        if (pad_prev != 0xFFFF) {                 /* buttons already held when the dialog opened don't count */
            WORD edge = pad & ~pad_prev;
            if (edge & (0x1 | 0x4)) move = -1;    /* D-pad up, left */
            if (edge & (0x2 | 0x8)) move = 1;     /* D-pad down, right */
            if (edge & 0x1000) ok = 1;            /* A */
        }
        pad_prev = pad;
    }
    if (move && g_nbtn > 1) {
        g_focus = (g_focus + move + g_nbtn) % g_nbtn;
        say_focus(1);
    }
    if (ok) {
        speakf(1, "%s", g_btn[g_focus]);
        g_press = g_focus;
    }
}

void sysdialog_init(void) {
    HMODULE rt = GetModuleHandleW(L"rexruntimerd.dll");
    t_buttonex = rt ? (void *)GetProcAddress(rt, "?ButtonEx@ImGui@@YA_NPEBDAEBUImVec2@@H@Z") : NULL;
    if (!t_buttonex || MH_CreateHook(t_buttonex, (void *)h_buttonex, (void **)&o_buttonex) != MH_OK ||
        MH_EnableHook(t_buttonex) != MH_OK) {
        log_line("sysdialog: ImGui::ButtonEx not hooked; system dialogs will not be answered");
        t_buttonex = NULL;
        return;
    }
    HMODULE xi = LoadLibraryW(L"xinput1_4.dll");
    if (!xi) xi = LoadLibraryW(L"xinput9_1_0.dll");
    p_xget = xi ? (xget_fn)GetProcAddress(xi, "XInputGetState") : NULL;

    HMODULE exe = GetModuleHandleW(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)((uint8_t *)exe + dd.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((char *)exe + imp->Name, "rexruntimerd.dll")) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)((uint8_t *)exe + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)((uint8_t *)exe + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)((uint8_t *)exe + names->u1.AddressOfData);
            if (strcmp((char *)n->Name, "__imp__XamShowMessageBoxUI")) continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            o_show = (guest_fn)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)h_show;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            g_iat_slot = &iat->u1.Function;
            log_line("sysdialog: XamShowMessageBoxUI wrapped%s", p_xget ? "" : " (no XInput: keyboard only)");
            return;
        }
    }
    log_line("sysdialog: XamShowMessageBoxUI import not found");
}

void sysdialog_shutdown(void) {
    g_pending = 0;
    if (g_iat_slot) {
        DWORD old;
        VirtualProtect(g_iat_slot, sizeof(void *), PAGE_READWRITE, &old);
        *g_iat_slot = (ULONG_PTR)o_show;
        VirtualProtect(g_iat_slot, sizeof(void *), old, &old);
        g_iat_slot = NULL;
    }
    if (t_buttonex) { MH_DisableHook(t_buttonex); MH_RemoveHook(t_buttonex); t_buttonex = NULL; }
}
