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
#include <stdlib.h>

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
static int toml_set(const char *const *keys, const char *const *vals, int n) {
    wchar_t p[MAX_PATH]; toml_path(p);
    char *buf = NULL; long len = 0;
    FILE *f = _wfopen(p, L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (char *)malloc((size_t)len + 1);
        if (!buf) { fclose(f); return 0; }
        len = (long)fread(buf, 1, (size_t)len, f); buf[len] = 0; fclose(f);
    }
    size_t cap = (size_t)len + 16, o = 0;
    for (int i = 0; i < n; i++) cap += strlen(keys[i]) + strlen(vals[i]) + 16;
    char *out = (char *)malloc(cap);
    int *done = (int *)calloc((size_t)n, sizeof(int));
    if (!out || !done) { free(out); free(done); free(buf); return 0; }
    char *s = buf;
    while (s && *s) {
        char *nl = strchr(s, '\n');
        size_t ll = nl ? (size_t)(nl - s) : strlen(s);
        if (ll && s[ll - 1] == '\r') ll--;
        char *t = s; while (t < s + ll && (*t == ' ' || *t == '\t')) t++;
        int hit = -1;
        for (int i = 0; i < n && hit < 0; i++) {
            size_t kl = strlen(keys[i]);
            if ((size_t)(s + ll - t) < kl || strncmp(t, keys[i], kl)) continue;
            char *e = t + kl; while (e < s + ll && (*e == ' ' || *e == '\t')) e++;
            if (e < s + ll && *e == '=') hit = i;
        }
        if (hit >= 0) { o += (size_t)snprintf(out + o, cap - o, "%s = \"%s\"\r\n", keys[hit], vals[hit]); done[hit] = 1; }
        else { memcpy(out + o, s, ll); o += ll; out[o++] = '\r'; out[o++] = '\n'; }
        s = nl ? nl + 1 : NULL;
    }
    for (int i = 0; i < n; i++)
        if (!done[i]) o += (size_t)snprintf(out + o, cap - o, "%s = \"%s\"\r\n", keys[i], vals[i]);
    free(buf); free(done);
    f = _wfopen(p, L"wb");
    int ok = f && fwrite(out, 1, o, f) == o;
    if (f) fclose(f);
    free(out);
    return ok;
}
static int write_binds(char v[NB][320]) {
    const char *vals[NB];
    for (int i = 0; i < NB; i++) vals[i] = v[i];
    return toml_set(k_cvar, vals, NB);
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
        { "[LSH]", 15, "left bumper" }, { "[Start]", 16, "Start" }, { "[Select]", 17, "View button" }, { "[BACK]", 17, "View button" },
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

/* ---------- the game's language (Ctrl+F12) ----------
 * user_language (XGetLanguage codes, the game's Coalesced.*): 1 English, 5 Spanish, 9 Portuguese, 4 French,
 * 3 German, 6 Italian. The game reads it at start, so a change is saved in mk9.toml for the next start;
 * mk9_online_language follows for the port's online screens (Spanish, English or Portuguese only). */
typedef void *(*getflag_fn)(void *ret, const void *name);   /* std::string (std::string_view): MSVC hidden return */
static getflag_fn p_getflag;
static int g_lang = 1, g_lang_next;
static const struct { int code; const wchar_t *ext; const char *online, *said; } k_lang[] = {
    { 1, L"eng", "en", "English. The game changes language when you start it again." },
    { 5, L"spa", "es", "Español. El juego cambia de idioma cuando lo vuelvas a abrir." },
    { 9, L"por", "pt", "Português. O jogo muda de idioma quando você o abrir de novo." },
    { 4, L"fra", "en", "Français. Le jeu change de langue au prochain démarrage." },
    { 3, L"ger", "en", "Deutsch. Das Spiel wechselt die Sprache beim nächsten Start." },
    { 6, L"ita", "en", "Italiano. Il gioco cambia lingua al prossimo avvio." },
};
#define NLANG (int)(sizeof k_lang / sizeof k_lang[0])

static int lang_index(int code) { for (int i = 0; i < NLANG; i++) if (k_lang[i].code == code) return i; return -1; }
int lang_code(void) { return g_lang; }
/* the language the game starts in next time, by its own name ("English", "Español") */
void lang_next_name(char *out, size_t cap) {
    int i = lang_index(g_lang_next ? g_lang_next : g_lang);
    const char *s = i >= 0 ? k_lang[i].said : "English.";
    snprintf(out, cap, "%.*s", (int)strcspn(s, "."), s);
}
/* between the game's own choice words ("Sí o No") */
const char *lang_or(void) {
    switch (g_lang) { case 5: case 6: return " o "; case 9: case 4: return " ou "; case 3: return " oder "; default: return " or "; }
}
const wchar_t *lang_file_ext(void) { int i = lang_index(g_lang); return k_lang[i < 0 ? 0 : i].ext; }

/* A port setting as text (rex::cvar::GetFlagByName), "" when unknown. */
static void flag_str(const char *name, char *v, size_t cap) {
    v[0] = 0;
    if (!p_getflag) return;
    _Alignas(16) unsigned char ret[32] = { 0 };
    SV n = { name, strlen(name) };
    p_getflag(ret, &n);
    size_t len = *(size_t *)(ret + 16), c = *(size_t *)(ret + 24);
    const char *d = c > 15 ? *(const char **)ret : (const char *)ret;   /* MSVC std::string: short ones inline */
    if (d && len < cap) { memcpy(v, d, len); v[len] = 0; }
}

/* user_language as the port has it: rex::cvar::GetFlagByName, else mk9.toml, else English */
static int read_lang(void) {
    char v[32] = "";
    if (p_getflag) {
        _Alignas(16) unsigned char ret[32] = { 0 };
        SV n = { "user_language", 13 };
        p_getflag(ret, &n);
        size_t len = *(size_t *)(ret + 16), cap = *(size_t *)(ret + 24);
        const char *d = cap > 15 ? *(const char **)ret : (const char *)ret;   /* MSVC std::string: short ones inline */
        if (d && len < sizeof v) { memcpy(v, d, len); v[len] = 0; }        /* (a long one's heap copy is left: tiny) */
    }
    if (!v[0]) {
        wchar_t p[MAX_PATH]; toml_path(p);
        FILE *f = _wfopen(p, L"rb");
        char line[256];
        while (f && fgets(line, sizeof line, f)) {
            char *s = line; while (*s == ' ' || *s == '\t') s++;
            if (strncmp(s, "user_language", 13)) continue;
            char *e = strchr(s, '='); if (!e) continue;
            e++; while (*e == ' ' || *e == '"') e++;
            snprintf(v, sizeof v, "%d", atoi(e));
        }
        if (f) fclose(f);
    }
    int c = atoi(v);
    return lang_index(c) >= 0 ? c : 1;
}

/* Ctrl+F12: the next language (Ctrl+Shift+F12: the one before; English and Spanish are neighbours), said in that
 * language, saved in mk9.toml for the next start */
void lang_cycle(int back) {
    if (!g_lang_next) g_lang_next = g_lang;
    int i = (lang_index(g_lang_next) + (back ? NLANG - 1 : 1)) % NLANG;
    g_lang_next = k_lang[i].code;
    char code[8]; snprintf(code, sizeof code, "%d", k_lang[i].code);
    const char *keys[2] = { "user_language", "mk9_online_language" }, *vals[2] = { code, k_lang[i].online };
    int ok = toml_set(keys, vals, 2);
    log_line("language: next start %s (%s)%s", code, k_lang[i].online, ok ? "" : ", mk9.toml NOT saved");
    if (!ok) { speak(L"Could not save mk9.toml.", 1); return; }
    if (g_lang_next == g_lang) {          /* back to the one in use: nothing changes */
        char m[160]; snprintf(m, sizeof m, "%.*s.", (int)strcspn(k_lang[i].said, "."), k_lang[i].said);
        speak_utf8(m, 1);
    } else speak_utf8(k_lang[i].said, 1);
}

void keys_init(void) {
    HMODULE rt = GetModuleHandleW(L"rexruntimerd.dll");
    p_setflag = rt ? (setflag_fn)(void *)GetProcAddress(rt,
        "?SetFlagByName@cvar@rex@@YA_NV?$basic_string_view@DU?$char_traits@D@std@@@std@@0@Z") : NULL;
    p_getflag = rt ? (getflag_fn)(void *)GetProcAddress(rt,
        "?GetFlagByName@cvar@rex@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@4@@Z") : NULL;
    g_lang = read_lang();
    log_line("keys: %s layout, live switch %s; game language %d", keys_pc_on() ? "PC" : "port",
             p_setflag ? "available" : "not available", g_lang);
    char udr[MAX_PATH]; flag_str("user_data_root", udr, sizeof udr);
    log_line("saves: user_data_root = %s", udr[0] ? udr : "(none: Documents)");
}

/* ---------- PC layout: keys the original PC version leaves alone (forum report 2026-11-25) ----------
 * The port's SDL layer turns Left and Right Shift into one Shift key, and Left and Right Ctrl into one Control
 * (sdk/src/ui/sdl_virtual_key.cpp), so with the PC layout Right Shift flipped stance and Right Ctrl threw, which the
 * 2013 PC release never did. Enter and Backspace (menu accept / back there) also kicked in fights through the A and B
 * binds. A binding can't tell the sides apart, so a message hook on the game window's thread drops those key presses
 * before SDL sees them: Right Shift and Right Ctrl always, Enter and Backspace only while a fight is being played (not
 * paused, no port window). Key-ups always pass, so nothing can stay held. Port layout: nothing is touched. */
static HHOOK g_msg_hook;
static DWORD g_hook_tid;

static int pc_drop(const MSG *m) {
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) return 0;
    if (g_pc_on <= 0) return 0;
    unsigned scan = (unsigned)(m->lParam >> 16) & 0xFF, ext = (unsigned)(m->lParam >> 24) & 1;
    if (m->wParam == VK_SHIFT && scan == 0x36) return 1;            /* Right Shift */
    if (m->wParam == VK_RSHIFT || m->wParam == VK_RCONTROL) return 1;
    if (m->wParam == VK_CONTROL && ext) return 1;                   /* Right Ctrl */
    if ((m->wParam == VK_RETURN || m->wParam == VK_BACK) && fight_live()) return 1;
    return 0;
}

static LRESULT CALLBACK getmsg_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && lp) {
        MSG *m = (MSG *)lp;
        int down = m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN;
        /* the accessibility menu (F4, game.c) has the keyboard while it is open: the game gets no presses (key-ups
         * pass so nothing stays held); F4 itself is handled with the other mod keys */
        /* Shift / Ctrl / Alt + F8 are the mod's; ImGui would also take them as the port's F8 in Practice */
        if (down && m->wParam == VK_F8 && (GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0)) {
            m->message = WM_NULL;
        } else if (down && a11y_menu_open()) {
            if (m->wParam != VK_F4) a11y_menu_key((int)m->wParam);
            m->message = WM_NULL;
        } else if (a11y_menu_open() && (m->message == WM_CHAR || m->message == WM_SYSCHAR)) {
            m->message = WM_NULL;
        } else
        /* Ctrl+Tab in a port window with tabs (F2, F1): the mod changes the tab; ImGui would switch windows */
        if (m->message == WM_KEYDOWN && m->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0 && im_tabs_on()) {
            if (!(m->lParam & (1 << 30))) im_tab_step(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);   /* not auto-repeat */
            m->message = WM_NULL;
        } else if (pc_drop(m)) {
            static DWORD last;
            if (GetTickCount() - last > 1000) { last = GetTickCount(); log_line("keys: PC layout, key %u dropped", (unsigned)m->wParam); }
            m->message = WM_NULL;
        }
    }
    (void)wp;
    return CallNextHookEx(g_msg_hook, code, wp, lp);
}

typedef struct { HWND best; LONG area; } WinFind;
static BOOL CALLBACK find_game_win(HWND h, LPARAM l) {
    WinFind *w = (WinFind *)l;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r; GetWindowRect(h, &r);
    LONG a = (r.right - r.left) * (r.bottom - r.top);
    if (a > w->area) { w->area = a; w->best = h; }
    return TRUE;
}

/* F2 > Controls "Two players on the same keyboard" (mnk_second_player) gives player 2 the arrows and the numpad and
 * takes the arrows from player 1's binds; unticking it does not give them back (port 1.4.4, seen 2026-11-25: W A S D
 * only, and the bot's arrow presses no longer moved the menus). When it goes off, the arrows go back to player 1. */
static int has_token(const char *bind, const char *tok) {
    size_t n = strlen(tok);
    for (const char *p = bind; *p; ) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == n && !_strnicmp(p, tok, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static void second_player_watch(void) {
    static DWORD last;
    static int was = -1;
    if (!p_getflag || !p_setflag || GetTickCount() - last < 500) return;
    last = GetTickCount();
    char v[16];
    flag_str("mnk_second_player", v, sizeof v);
    int on = !strcmp(v, "true");
    if (was == 1 && !on) {
        static const char *const arrow[4] = { "Up", "Down", "Left", "Right" };
        char b[NB][320];
        for (int i = 0; i < NB; i++) flag_str(k_cvar[i], b[i], sizeof b[i]);
        int changed = 0;
        for (int i = 0; i < 4; i++) {
            if (has_token(b[i], arrow[i])) continue;
            size_t l = strlen(b[i]);
            snprintf(b[i] + l, sizeof b[i] - l, "%s%s", l ? "," : "", arrow[i]);
            changed = 1;
        }
        if (changed) {
            memcpy(g_apply, b, sizeof g_apply);
            InterlockedExchange(&g_apply_pending, 1);
            write_binds(b);
            speak(L"Player 1 has the arrow keys back.", 0);
            log_line("keys: second keyboard player off, arrows back to player 1");
        }
    }
    was = on;
}

/* Every second until the game window exists: hook its thread's message queue. */
/* Back from an online session (forum report 2026-11-25: "after the connection drops the controller is dead until I
 * restart"). A session's end - dropped, or left on purpose - always restarts the game (session.cpp Back) with
 * --mk9_online_show=true, and that copy opens the F1 online window by itself; while it is open the port gives the game
 * a neutral pad and no keys (mk9_overlay_input.cpp GameMayRead). The new window can also come up behind another one,
 * and then SDL drops the controller's events altogether, so even Back+Y can't close F1. Here: for the first 20 s the
 * window is brought to the front when it is not, and the player is told once how to get back to the game. */
static void online_return_poll(void) {
    static int state = -1;                  /* -1 unchecked, 0 nothing to do, 1 watching, 2 done */
    static DWORD t0, last;
    if (state == 0 || state == 2) return;
    if (state < 0) {
        const wchar_t *cl = GetCommandLineW();
        state = cl && wcsstr(cl, L"--mk9_online_show=true") ? 1 : 0;
        t0 = GetTickCount();
        if (state) log_line("online: started again after a session");
        return;
    }
    DWORD now = GetTickCount();
    if (now - last < 500) return;
    last = now;
    WinFind w = { NULL, 0 };
    EnumWindows(find_game_win, (LPARAM)&w);
    if (!w.best) { if (now - t0 > 60000) state = 2; return; }
    HWND fg = GetForegroundWindow();
    if (fg != w.best && now - t0 < 20000) {
        DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0, me = GetCurrentThreadId();
        if (fg_tid && fg_tid != me) AttachThreadInput(me, fg_tid, TRUE);
        ShowWindow(w.best, SW_RESTORE);
        BringWindowToTop(w.best);
        BOOL ok = SetForegroundWindow(w.best);
        if (fg_tid && fg_tid != me) AttachThreadInput(me, fg_tid, FALSE);
        log_line("online: game window not in front, raised (%d)", (int)ok);
        return;
    }
    if (now - t0 < 2500) return;            /* let the lobby window's own reading start first */
    speak(L"Online session over. The online window is open, and the game ignores the controller until it closes: "
          L"press F1, or the View button and Y on the controller.", 0);
    log_line("online: back in the lobby, %s", fg == w.best ? "window in front" : "window NOT in front");
    state = 2;
}

void keys_poll(void) {
    static DWORD last;
    second_player_watch();
    online_return_poll();
    if (g_msg_hook || GetTickCount() - last < 1000) return;
    last = GetTickCount();
    keys_pc_on();
    WinFind w = { NULL, 0 };
    EnumWindows(find_game_win, (LPARAM)&w);
    if (!w.best) return;
    g_hook_tid = GetWindowThreadProcessId(w.best, NULL);
    g_msg_hook = SetWindowsHookExW(WH_GETMESSAGE, getmsg_proc, NULL, g_hook_tid);
    log_line("keys: message hook on the window thread %lu %s", g_hook_tid, g_msg_hook ? "on" : "FAILED");
}

void keys_shutdown(void) { if (g_msg_hook) { UnhookWindowsHookEx(g_msg_hook); g_msg_hook = NULL; } }

/* Practice, plain F8: the port changes player 1's fighter while recording (mk9_practice_record.cpp, the cvar
 * mk9_practice_record_guard: stand -> crouch -> none -> stand) on its next frame; said once it has. */
static DWORD WINAPI guard_thread(LPVOID unused) {
    (void)unused;
    Sleep(150);
    char v[32]; flag_str("mk9_practice_record_guard", v, sizeof v);
    const char *s = !strcmp(v, "stand") ? "blocks standing" : !strcmp(v, "crouch") ? "blocks crouching" : "stands still";
    char msg[96]; snprintf(msg, sizeof msg, "While recording, your fighter %s", s);
    int was = speech_force(1); speak_utf8(msg, 1); speech_force(was);
    return 0;
}
void practice_guard_say(void) { HANDLE t = CreateThread(NULL, 0, guard_thread, NULL, 0, NULL); if (t) CloseHandle(t); }
