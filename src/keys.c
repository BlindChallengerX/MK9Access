/*
 * Keyboard layouts. The port turns keys into Xbox buttons through its keybind_*
 * settings (mk9.toml; F2 > Controles edits them, the SDK reads them every poll).
 * Shift+F12 switches between the port's layout and the original PC Komplete
 * Edition layout (2013 Steam release: W A S D move, T U G J attacks, Left Ctrl
 * throw, Space block, Left Shift flip stance, Tab tag, 1 pause, 4 back), applied
 * at once through rex::cvar::SetFlagByName and saved in mk9.toml.
 *
 * The SDK matches a bind's modifiers exactly ("Space" is silent while Shift is
 * held) and a bare "Shift" or "Control" bind never fires. So the PC layout lists
 * each key with its Shift / Ctrl variants, and binds Shift as "Shift+Shift" and
 * Ctrl as "Ctrl+Control" (both sides: the SDK makes one Shift and one Control key).
 * The layout in use before the switch is kept in MK9Access\keys_saved.txt and
 * comes back on the next Shift+F12.
 */
#include "access.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define NB 20
static const char *k_cvar[NB] = {
    "keybind_lstick_up", "keybind_lstick_down", "keybind_lstick_left", "keybind_lstick_right",
    "keybind_dpad_up", "keybind_dpad_down", "keybind_dpad_left", "keybind_dpad_right",
    "keybind_x", "keybind_y", "keybind_a", "keybind_b",
    "keybind_right_shoulder", "keybind_right_trigger", "keybind_left_trigger", "keybind_left_shoulder",
    "keybind_start", "keybind_back", "keybind_lstick_press", "keybind_rstick_press",
};
/* the port's defaults (mk9-lauta app/mk9_pc_defaults.toml, compiled into mk9.exe) */
static const char *k_port[NB] = {
    "W,Up", "S,Down", "A,Left", "D,Right", "", "", "", "",
    "U", "I", "J", "K,Backspace", "O", "L,Space", "Semicolon", "P", "Return,Escape", "Tab", "", "",
};
/* original PC Komplete Edition, before the modifier variants are added */
static const char *k_pc[NB] = {
    "W,Up", "S,Down", "A,Left", "D,Right", "", "", "", "",
    "T", "U", "G,Return", "J,Backspace", "Control", "Space", "Shift", "Tab", "1,Escape", "4", "", "",
};

static int g_pc_on = -1;               /* -1 not read yet */
static char g_apply[NB][320];          /* values waiting for the game thread */
static volatile LONG g_apply_pending;

typedef unsigned char (*setflag_fn)(const void *name, const void *value);   /* (std::string_view, std::string_view) by pointer */
typedef struct { const char *p; size_t n; } SV;
static setflag_fn p_setflag;

static void toml_path(wchar_t *out) { swprintf(out, MAX_PATH, L"%lsmk9.toml", g_game_dir); }
static void saved_path(wchar_t *out) { swprintf(out, MAX_PATH, L"%lskeys_saved.txt", g_mod_dir); }

/* The binds in effect: mk9.toml's lines, else the port's defaults. */
static void read_binds(char v[NB][320]) {
    for (int i = 0; i < NB; i++) snprintf(v[i], 320, "%s", k_port[i]);
    wchar_t p[MAX_PATH]; toml_path(p);
    FILE *f = _wfopen(p, L"rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        for (int i = 0; i < NB; i++) {
            size_t kl = strlen(k_cvar[i]);
            if (strncmp(s, k_cvar[i], kl)) continue;
            char *e = s + kl; while (*e == ' ' || *e == '\t') e++;
            if (*e != '=') continue;
            e++; while (*e == ' ' || *e == '\t') e++;
            if (*e == '"') e++;
            size_t n = 0; while (e[n] && e[n] != '"' && e[n] != '\r' && e[n] != '\n') n++;
            snprintf(v[i], 320, "%.*s", (int)n, e);
        }
    }
    fclose(f);
}

/* mk9.toml with these keys' lines replaced (or added at the end), every other line kept (as the port's SaveToConfig). */
static int write_binds(char v[NB][320]) {
    wchar_t p[MAX_PATH]; toml_path(p);
    char *buf = NULL; long len = 0;
    FILE *f = _wfopen(p, L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (char *)malloc((size_t)len + 1);
        if (!buf) { fclose(f); return 0; }
        len = (long)fread(buf, 1, (size_t)len, f); buf[len] = 0; fclose(f);
    }
    size_t cap = (size_t)len + NB * 400 + 16, o = 0;
    char *out = (char *)malloc(cap);
    if (!out) { free(buf); return 0; }
    int done[NB] = { 0 };
    char *s = buf;
    while (s && *s) {
        char *nl = strchr(s, '\n');
        size_t ll = nl ? (size_t)(nl - s) : strlen(s);
        if (ll && s[ll - 1] == '\r') ll--;
        char *t = s; while (t < s + ll && (*t == ' ' || *t == '\t')) t++;
        int hit = -1;
        for (int i = 0; i < NB && hit < 0; i++) {
            size_t kl = strlen(k_cvar[i]);
            if ((size_t)(s + ll - t) < kl || strncmp(t, k_cvar[i], kl)) continue;
            char *e = t + kl; while (e < s + ll && (*e == ' ' || *e == '\t')) e++;
            if (e < s + ll && *e == '=') hit = i;
        }
        if (hit >= 0) { o += (size_t)snprintf(out + o, cap - o, "%s = \"%s\"\r\n", k_cvar[hit], v[hit]); done[hit] = 1; }
        else { memcpy(out + o, s, ll); o += ll; out[o++] = '\r'; out[o++] = '\n'; }
        s = nl ? nl + 1 : NULL;
    }
    for (int i = 0; i < NB; i++)
        if (!done[i]) o += (size_t)snprintf(out + o, cap - o, "%s = \"%s\"\r\n", k_cvar[i], v[i]);
    free(buf);
    f = _wfopen(p, L"wb");
    int ok = f && fwrite(out, 1, o, f) == o;
    if (f) fclose(f);
    free(out);
    return ok;
}

/* "T" -> "T,Shift+T,Ctrl+T,Ctrl+Shift+T"; "Shift" -> "Shift+Shift,Ctrl+Shift+Shift"; "Control" -> "Ctrl+Control,Ctrl+Shift+Control". */
static void with_modifiers(const char *in, char *out, size_t cap) {
    char tmp[128]; snprintf(tmp, sizeof tmp, "%s", in);
    size_t o = 0; out[0] = 0;
    for (char *k = strtok(tmp, ","); k; k = strtok(NULL, ",")) {
        const char *fmt = !strcmp(k, "Shift") ? "%sShift+Shift,Ctrl+Shift+Shift"
                        : !strcmp(k, "Control") ? "%sCtrl+Control,Ctrl+Shift+Control"
                        : NULL;
        if (fmt) o += (size_t)snprintf(out + o, cap - o, fmt, o ? "," : "");
        else o += (size_t)snprintf(out + o, cap - o, "%s%s,Shift+%s,Ctrl+%s,Ctrl+Shift+%s", o ? "," : "", k, k, k, k);
        if (o >= cap) { out[cap - 1] = 0; return; }
    }
}

int keys_pc_on(void) {
    if (g_pc_on < 0) { wchar_t p[MAX_PATH]; saved_path(p); g_pc_on = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
    return g_pc_on;
}

/* One key as the player hears it: the character this keyboard has at a punctuation key's place (Ñ on a Spanish
 * keyboard), a name for the rest. "Shift+Shift" is Shift, "Ctrl+Control" Control; other modifier variants are skipped. */
static int key_spoken(const char *tok, char *out, size_t cap) {
    if (!strcmp(tok, "Shift+Shift")) { snprintf(out, cap, "Shift"); return 1; }
    if (!strcmp(tok, "Ctrl+Control")) { snprintf(out, cap, "Control"); return 1; }
    if (strchr(tok, '+') && tok[0] && tok[strlen(tok) - 1] != '+') return 0;
    static const struct { const char *k, *s; UINT scan; } names[] = {
        { "Return", "Enter", 0 }, { "Escape", "Escape", 0 }, { "Space", "Space", 0 }, { "Backspace", "Backspace", 0 },
        { "Tab", "Tab", 0 }, { "Up", "up arrow", 0 }, { "Down", "down arrow", 0 }, { "Left", "left arrow", 0 },
        { "Right", "right arrow", 0 }, { "Shift", "Shift", 0 }, { "Control", "Control", 0 }, { "Alt", "Alt", 0 },
        { "Delete", "Delete", 0 }, { "Insert", "Insert", 0 }, { "Home", "Home", 0 }, { "End", "End", 0 },
        { "PageUp", "Page Up", 0 }, { "PageDown", "Page Down", 0 }, { "NumpadPlus", "numpad plus", 0 },
        { "NumpadMinus", "numpad minus", 0 }, { "NumpadStar", "numpad star", 0 }, { "NumpadSlash", "numpad slash", 0 },
        { "NumpadEnter", "numpad Enter", 0 },
        { "Minus", "minus", 0x0C }, { "Plus", "equals", 0x0D }, { "LBracket", "left bracket", 0x1A },
        { "RBracket", "right bracket", 0x1B }, { "Semicolon", "semicolon", 0x27 }, { "Quote", "apostrophe", 0x28 },
        { "Backtick", "grave", 0x29 }, { "Backslash", "backslash", 0x2B }, { "Comma", "comma", 0x33 },
        { "Period", "period", 0x34 }, { "Slash", "slash", 0x35 },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(tok, names[i].k)) continue;
        if (names[i].scan) {   /* a letter there on this keyboard (Ñ, Ç): say it */
            UINT vk = MapVirtualKeyW(names[i].scan, MAPVK_VSC_TO_VK);
            UINT ch = vk ? MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR) & 0x7FFF : 0;
            if (ch > 127 && IsCharAlphaW((WCHAR)ch)) {
                WCHAR w[2] = { (WCHAR)ch, 0 }; CharUpperW(w);
                char u[8]; int n = WideCharToMultiByte(CP_UTF8, 0, w, 1, u, sizeof u - 1, NULL, NULL); u[n > 0 ? n : 0] = 0;
                snprintf(out, cap, "%s", u); return 1;
            }
        }
        snprintf(out, cap, "%s", names[i].s); return 1;
    }
    if (!strncmp(tok, "Numpad", 6) && tok[6] && !tok[7]) { snprintf(out, cap, "numpad %c", tok[6]); return 1; }
    snprintf(out, cap, "%s", tok);
    return 1;
}

/* A bind's keys in words: "T", "G or Enter", "" for none. */
static void bind_spoken(const char *bind, char *out, size_t cap) {
    char tmp[320]; snprintf(tmp, sizeof tmp, "%s", bind);
    size_t o = 0; out[0] = 0;
    for (char *k = strtok(tmp, ","); k; k = strtok(NULL, ",")) {
        while (*k == ' ') k++;
        char w[48];
        if (!*k || !key_spoken(k, w, sizeof w)) continue;
        o += (size_t)snprintf(out + o, cap - o, "%s%s", o ? " or " : "", w);
        if (o >= cap) { out[cap - 1] = 0; return; }
    }
}

/* Button Configuration (Options > Controller) shows buttons as [X], [RTRIG]...: the key that presses it. */
int keys_for_button(const char *token, char *out, size_t cap) {
    static const struct { const char *t; int i; const char *pad; } map[] = {
        { "[X]", 8, "X" }, { "[Y]", 9, "Y" }, { "[A]", 10, "A" }, { "[B]", 11, "B" },
        { "[RSH]", 12, "right bumper" }, { "[RTRIG]", 13, "right trigger" }, { "[LTRIG]", 14, "left trigger" },
        { "[LSH]", 15, "left bumper" }, { "[Start]", 16, "Start" }, { "[Select]", 17, "Back" }, { "[BACK]", 17, "Back" },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (_stricmp(token, map[i].t)) continue;
        char v[NB][320], k[160];
        read_binds(v);
        bind_spoken(v[map[i].i], k, sizeof k);
        const char *btn = strlen(map[i].pad) == 1 ? " button" : "";   /* "X button", "right trigger" */
        if (k[0]) snprintf(out, cap, "%s, %s%s", k, map[i].pad, btn);
        else snprintf(out, cap, "%s%s", map[i].pad, btn);
        return 1;
    }
    return 0;
}

/* The layout in words, said after a switch. */
static void describe(char v[NB][320], const char *name, char *out, size_t cap) {
    char k[NB][160];
    for (int i = 0; i < NB; i++) bind_spoken(v[i], k[i], sizeof k[i]);
    size_t o = (size_t)snprintf(out, cap, "%s. ", name);
    if (!strcmp(k[0], "W or up arrow") && !strcmp(k[1], "S or down arrow") && !strcmp(k[2], "A or left arrow") &&
        !strcmp(k[3], "D or right arrow"))
        o += (size_t)snprintf(out + o, cap - o, "Move with W A S D or the arrows. ");
    else
        o += (size_t)snprintf(out + o, cap - o, "Up %s, down %s, back %s, forward %s. ", k[0], k[1], k[2], k[3]);
    static const struct { int i; const char *what; } act[] = {
        { 8, "Front punch" }, { 9, "Back punch" }, { 10, "Front kick" }, { 11, "Back kick" }, { 12, "Throw" },
        { 13, "Block" }, { 14, "Flip stance" }, { 15, "Tag" }, { 16, "Pause" },
    };
    for (size_t i = 0; i < sizeof act / sizeof act[0] && o < cap; i++) {
        if (!k[act[i].i][0]) continue;
        char first[64]; snprintf(first, sizeof first, "%s", k[act[i].i]);   /* the fight key; menu extras come last */
        char *orw = act[i].i != 16 ? strstr(first, " or ") : NULL;
        if (orw) *orw = 0;
        o += (size_t)snprintf(out + o, cap - o, "%s %s. ", act[i].what, first);
    }
    if (o < cap) snprintf(out + o, cap - o, "In menus %s accepts, %s goes back. Shift F12 switches layouts.", k[10], k[11]);
}

/* Shift+F12 */
void keys_toggle(void) {
    char cur[NB][320], next[NB][320], msg[1200];
    wchar_t sp[MAX_PATH]; saved_path(sp);
    read_binds(cur);
    if (!keys_pc_on()) {
        FILE *f = _wfopen(sp, L"wb");                       /* keep the layout in use for the way back */
        if (!f) { speak(L"Could not save the current keys.", 1); return; }
        for (int i = 0; i < NB; i++) fprintf(f, "%s=%s\n", k_cvar[i], cur[i]);
        fclose(f);
        for (int i = 0; i < NB; i++) with_modifiers(k_pc[i], next[i], sizeof next[i]);
        g_pc_on = 1;
        describe(next, "Original PC keys", msg, sizeof msg);
    } else {
        for (int i = 0; i < NB; i++) snprintf(next[i], sizeof next[i], "%s", k_port[i]);
        FILE *f = _wfopen(sp, L"rb");
        if (f) {
            char line[512];
            while (fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = 0;
                for (int i = 0; i < NB; i++) if (!strcmp(line, k_cvar[i])) snprintf(next[i], sizeof next[i], "%s", eq + 1);
            }
            fclose(f);
        }
        DeleteFileW(sp);
        g_pc_on = 0;
        describe(next, "Port keys", msg, sizeof msg);
    }
    int saved = write_binds(next);
    memcpy(g_apply, next, sizeof g_apply);
    InterlockedExchange(&g_apply_pending, 1);
    log_line("keys: %s layout%s", g_pc_on ? "PC" : "port", saved ? ", saved to mk9.toml" : ", mk9.toml NOT saved");
    speak_utf8(msg, 1);
    if (!p_setflag) speak(L"Restart the game to use them.", 0);
}

/* On the game thread, before it reads the pad (the SDK reads the binds there). */
void keys_game_thread(void) {
    if (!g_apply_pending || !InterlockedExchange(&g_apply_pending, 0)) return;
    if (!p_setflag) return;
    for (int i = 0; i < NB; i++) {
        SV n = { k_cvar[i], strlen(k_cvar[i]) }, v = { g_apply[i], strlen(g_apply[i]) };
        if (!p_setflag(&n, &v)) log_line("keys: %s not set", k_cvar[i]);
    }
}

void keys_init(void) {
    HMODULE rt = GetModuleHandleW(L"rexruntimerd.dll");
    p_setflag = rt ? (setflag_fn)(void *)GetProcAddress(rt,
        "?SetFlagByName@cvar@rex@@YA_NV?$basic_string_view@DU?$char_traits@D@std@@@std@@0@Z") : NULL;
    log_line("keys: %s layout, live switch %s", keys_pc_on() ? "PC" : "port", p_setflag ? "available" : "not available");
}
