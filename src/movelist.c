/*
 * Move lists: MK9Access\movelists\<fighter package>.txt, extracted from the
 * game's own packages by tools/movelists.py. Lines: "# SECTION",
 * "Name | input tokens | notes", "  note: ...". Input tokens are the game's
 * ([FP] front punch, [Away] back...), spoken here in words.
 * Which fighter's list to read is set by the character readers (not done yet).
 */
#include "access.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ML_MAX 128
static char g_lines[ML_MAX][256];
static int g_n, g_pos;
static char g_name[64];

static const struct { const char *tok, *say; } k_tok[] = {
    { "[FP]", "front punch" }, { "[BP]", "back punch" }, { "[FK]", "front kick" }, { "[BK]", "back kick" },
    { "[Away]", "back" }, { "[Towards]", "forward" }, { "[Up]", "up" }, { "[Down]", "down" },
    { "[BLOCK]", "block" }, { "[THROW]", "throw" }, { "[TAG]", "tag" }, { "[L2]", "flip stance" },
    { "[+]", "plus" }, { "[,]", "," },
};

/* "[Down][Away][BP][+][BLOCK]" -> "down, back, back punch plus block" */
void ml_spoken(const char *in, char *out, size_t cap) {
    out[0] = 0;
    const char *p = in;
    int need_sep = 0;
    while (*p) {
        if (*p == '*' || *p == ' ') { p++; continue; }
        int hit = 0;
        for (unsigned i = 0; i < sizeof k_tok / sizeof k_tok[0]; i++) {
            size_t n = strlen(k_tok[i].tok);
            if (strncmp(p, k_tok[i].tok, n)) continue;
            const char *s = k_tok[i].say;
            if (!strcmp(s, ",")) { strncat(out, ",", cap - strlen(out) - 1); need_sep = 0; }
            else if (!strcmp(s, "plus")) { strncat(out, " plus", cap - strlen(out) - 1); need_sep = 0; }
            else {
                if (need_sep) strncat(out, ",", cap - strlen(out) - 1);
                if (out[0]) strncat(out, " ", cap - strlen(out) - 1);
                strncat(out, s, cap - strlen(out) - 1);
                need_sep = 1;
            }
            p += n; hit = 1; break;
        }
        if (!hit) {                 /* text inside the input, like "(Hold)": a word before the next button */
            const char *q = p; while (*q && *q != '[') q++;
            char word[64]; size_t n = 0;
            for (const char *c = p; c < q && n < sizeof word - 1; c++) if (*c != '(' && *c != ')') word[n++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
            word[n] = 0;
            if (n) { if (out[0]) strncat(out, " ", cap - strlen(out) - 1); strncat(out, word, cap - strlen(out) - 1); }
            p = q; need_sep = 0;
        }
    }
}

int ml_load(const char *fighter) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsmovelists\%hs.txt", g_mod_dir, fighter);
    FILE *f = _wfopen(path, L"r");
    if (!f) { log_line("movelist: no file for %s", fighter); return 0; }
    g_n = 0; g_pos = -1;
    while (g_n < ML_MAX && fgets(g_lines[g_n], sizeof g_lines[0], f)) {
        g_lines[g_n][strcspn(g_lines[g_n], "\r\n")] = 0;
        if (g_lines[g_n][0]) g_n++;
    }
    fclose(f);
    snprintf(g_name, sizeof g_name, "%s", fighter);
    return g_n;
}

static void say_line(int i) {
    char *l = g_lines[i], out[640];
    if (l[0] == '#') { snprintf(out, sizeof out, "%s", l + 2); speak_utf8(out, 1); return; }
    if (!strncmp(l, "  note: ", 8)) { speak_utf8(l + 8, 1); return; }
    char name[128] = "", input[192] = "", notes[192] = "";
    char *a = strstr(l, " | ");
    if (!a) { speak_utf8(l, 1); return; }
    snprintf(name, sizeof name, "%.*s", (int)(a - l), l);
    char *b = strstr(a + 3, " | ");
    if (b) { snprintf(input, sizeof input, "%.*s", (int)(b - a - 3), a + 3); snprintf(notes, sizeof notes, "%s", b + 3); }
    else snprintf(input, sizeof input, "%s", a + 3);
    char spoken[256];
    ml_spoken(input, spoken, sizeof spoken);
    snprintf(out, sizeof out, "%s: %s%s%s", name, spoken[0] ? spoken : "no input listed", notes[0] ? ". " : "", notes);
    speak_utf8(out, 1);
}

/* dir: +1 next move, -1 previous, +2 next section, -2 previous section, 0 repeat */
void ml_step(int dir) {
    if (!g_n) { speak(L"No move list loaded", 1); return; }
    int i = g_pos < 0 ? 0 : g_pos;
    if (dir == 1 && g_pos >= 0) i++;
    else if (dir == -1) i--;
    else if (dir == 2 || dir == -2) {
        int s = dir > 0 ? 1 : -1;
        for (i += s; i >= 0 && i < g_n && g_lines[i][0] != '#'; i += s) {}
    }
    if (i < 0) i = 0;
    if (i >= g_n) i = g_n - 1;
    g_pos = i;
    say_line(i);
}
