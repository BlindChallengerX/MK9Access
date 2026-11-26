/*
 * The game's own menus (Scaleform screens driven by NetherRealm script).
 *
 * Every menu text goes through Localize(section, key, default) at 82a235b0,
 * which returns the English text (char *). When the cursor moves, the script
 * localizes the new item's description key ("StoryDesc"); menu items are
 * records {label key*, desc key*, id, 0, flag} in the script's data, so the
 * record holding that desc key gives the label key ("Story"), and the label
 * text comes from the game's localization tables (Coalesced.eng, parsed from
 * the player's own game files) or from earlier Localize calls.
 *
 * Game thread: h_localize queues (section, key, key address, result).
 * Mod thread (menu_poll): turns queued lookups into speech.
 */
#include "access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define G_LOCALIZE 0x82a235b0u
#define G_SETTEXT  0x82a38648u        /* GFx text field set: (obj, char *text) - logged in debug mode */

/* ---------- localization table ---------- */
typedef struct { char *sec, *key, *val; } LocE;
/* The 2 Players main menu's Versus Mode (Versus2Mode, then its One VS One / Two VS Two): each side is one player.
 * Not Team Ladder (both players against the CPU) nor 3-4 player Versus (Versus3Mode/Versus4Mode: two on a side).
 * Set from the main menu item last focused. */
static int g_local_versus;
int menu_local_versus(void) { return g_local_versus; }
static void mainmenu_focus(const char *key) {
    if (!_stricmp(key, "Versus2Mode")) g_local_versus = 1;
    else if (_stricmp(key, "VersusOneOnOne") && _stricmp(key, "VersusTwoOnTwo")) g_local_versus = 0;
}
static LocE *g_loc; static int g_nloc, g_loccap;
static int *g_lochash; static int g_lochsize;

static unsigned hash_sk(const char *s, const char *k) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)tolower((unsigned char)*s)) * 16777619u;
    h = (h ^ 1) * 16777619u;
    for (; *k; k++) h = (h ^ (unsigned char)tolower((unsigned char)*k)) * 16777619u;
    return h;
}

static void loc_put(const char *s, const char *k, const char *v) {
    if (g_nloc == g_loccap) { g_loccap = g_loccap ? g_loccap * 2 : 4096; g_loc = realloc(g_loc, sizeof(LocE) * g_loccap); }
    g_loc[g_nloc].sec = _strdup(s); g_loc[g_nloc].key = _strdup(k); g_loc[g_nloc].val = _strdup(v);
    g_nloc++;
}

static void loc_index(void) {
    free(g_lochash);
    g_lochsize = 1; while (g_lochsize < g_nloc * 2 + 16) g_lochsize <<= 1;
    g_lochash = malloc(sizeof(int) * g_lochsize);
    memset(g_lochash, 0xff, sizeof(int) * g_lochsize);
    for (int i = 0; i < g_nloc; i++) {       /* later entries win (DLC and cache override) */
        unsigned h = hash_sk(g_loc[i].sec, g_loc[i].key) & (g_lochsize - 1);
        while (g_lochash[h] >= 0 && (_stricmp(g_loc[g_lochash[h]].sec, g_loc[i].sec) || _stricmp(g_loc[g_lochash[h]].key, g_loc[i].key)))
            h = (h + 1) & (g_lochsize - 1);
        g_lochash[h] = i;
    }
}

const char *loc_get(const char *s, const char *k) {
    if (!g_lochash || !s || !k) return NULL;
    unsigned h = hash_sk(s, k) & (g_lochsize - 1);
    while (g_lochash[h] >= 0) {
        LocE *e = &g_loc[g_lochash[h]];
        if (!_stricmp(e->sec, s) && !_stricmp(e->key, k)) return e->val;
        h = (h + 1) & (g_lochsize - 1);
    }
    return NULL;
}

/* The game's 8-bit texts are Windows-1252 ("automáticamente" in the Spanish tables); speech takes UTF-8. In place. */
void game_text_utf8(char *s, size_t cap) {
    static const uint16_t hi[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160,
        0x2039, 0x152, 0x8D, 0x17D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122,
        0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
    size_t n = strlen(s), extra = 0;
    for (size_t i = 0; i < n; i++) if ((uint8_t)s[i] >= 0x80) extra += (uint8_t)s[i] < 0xA0 ? 2 : 1;
    if (!extra) return;
    /* already UTF-8 (a text the mod converted before): leave it */
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, (int)n, NULL, 0) > 0) return;
    while (n && n + extra >= cap) {             /* too long: drop the tail */
        uint8_t c = (uint8_t)s[--n]; s[n] = 0;
        if (c >= 0x80) extra -= c < 0xA0 ? 2 : 1;
    }
    size_t o = n + extra;
    s[o] = 0;
    for (size_t i = n; i-- > 0;) {
        uint8_t c = (uint8_t)s[i];
        unsigned u = c < 0x80 ? c : c < 0xA0 ? hi[c - 0x80] : c;
        if (u < 0x80) s[--o] = (char)u;
        else if (u < 0x800) { s[--o] = (char)(0x80 | (u & 0x3F)); s[--o] = (char)(0xC0 | (u >> 6)); }
        else { s[--o] = (char)(0x80 | (u & 0x3F)); s[--o] = (char)(0x80 | ((u >> 6) & 0x3F)); s[--o] = (char)(0xE0 | (u >> 12)); }
    }
}

/* INI text: [Section] then Key=Value lines. */
static void parse_ini(const char *t, size_t n) {
    char sec[128] = "", line[2048];
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && t[j] != '\n') j++;
        size_t len = j - i;
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, t + i, len); line[len] = 0;
        i = j + 1;
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        if (!len || line[0] == ';') continue;
        if (line[0] == '[' && line[len - 1] == ']' && !strchr(line, '=')) {
            size_t sl = len - 2 < sizeof sec - 1 ? len - 2 : sizeof sec - 1;
            memcpy(sec, line + 1, sl); sec[sl] = 0;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || !sec[0]) continue;
        *eq = 0;
        char *v = eq + 1;
        size_t vl = strlen(v);
        if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') { v[vl - 1] = 0; v++; }
        char u[2048]; snprintf(u, sizeof u, "%s", v); game_text_utf8(u, sizeof u);
        loc_put(sec, line, u);
    }
}

static int rd32be(const uint8_t *p) { return (int)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]); }

/* Coalesced.eng (Xbox 360): u32 count, then count x (name, text), each an
 * FString: s32 length with the NUL (negative = UTF-16), then the characters. */
static void load_coalesced(void) {
    /* the game's language (eng, spa, ...: keys.c); the port keeps a patched copy with its PC texts ("ONLINE (F1)")
     * in cache\localization */
    const wchar_t *ext = lang_file_ext();
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lscache\\localization\\Coalesced.%ls", g_game_dir, ext);
    FILE *f = _wfopen(path, L"rb");
    if (!f) {
        swprintf(path, MAX_PATH, L"%lsgame_root\\Localization\\Coalesced.%ls", g_game_dir, ext);
        f = _wfopen(path, L"rb");
    }
    if (!f) { log_line("menu: no Coalesced.%ls", ext); return; }
    log_line("menu: texts from Coalesced.%ls", ext);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(sz > 0 ? (size_t)sz : 1);
    if (!d || fread(d, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(d); return; }
    fclose(f);
    size_t p = 4;
    int count = sz >= 4 ? rd32be(d) : 0;
    for (int c = 0; c < count && p + 4 <= (size_t)sz; c++) {
        int ln = rd32be(d + p); p += 4;
        if (ln < 0 || p + (size_t)ln > (size_t)sz) break;
        p += (size_t)ln;
        if (p + 4 > (size_t)sz) break;
        int tl = rd32be(d + p); p += 4;
        if (tl < 0 || p + (size_t)tl > (size_t)sz) break;
        parse_ini((const char *)d + p, tl ? (size_t)tl - 1 : 0);
        p += (size_t)tl;
    }
    free(d);
    log_line("menu: %d localized texts", g_nloc);
}

/* ---------- game thread: Localize hook ---------- */
#define LQ 256
typedef struct { char sec[64], key[96]; uint32_t keyaddr; char val[512]; DWORD t; } LocCall;
static LocCall g_lq[LQ];
static volatile LONG g_lq_w;          /* calls written (index = w % LQ) */
static LONG g_lq_r;
static CRITICAL_SECTION g_lqlock;
static guest_fn o_localize, o_settext;
static volatile int g_debug;

static void gstr(uint32_t a, char *out, size_t cap) {
    size_t n = 0;
    if (a && guest_ok(a, 1))
        for (; n < cap - 1 && guest_ok(a + n, 1); n++) { char c = (char)g8(a + n); if (!c) break; out[n] = c; }
    out[n] = 0;
}

static void h_localize(PPCContext *ctx, uint8_t *base) {
    uint32_t s = (uint32_t)ctx->r3, k = (uint32_t)ctx->r4;
    CALL_ORIG(o_localize, ctx, base);
    InterlockedIncrement(&g_inflight);
    EnterCriticalSection(&g_lqlock);
    LocCall *c = &g_lq[g_lq_w % LQ];
    gstr(s, c->sec, sizeof c->sec); gstr(k, c->key, sizeof c->key);
    c->keyaddr = k;
    gstr((uint32_t)ctx->r3, c->val, sizeof c->val);
    game_text_utf8(c->val, sizeof c->val);
    c->t = GetTickCount();
    g_lq_w++;
    LeaveCriticalSection(&g_lqlock);
    InterlockedDecrement(&g_inflight);
}

/* ---------- game thread: Flash clips ----------
 * The script points a "holder" at a clip with Resolve(holder, parent holder,
 * "option3.text.varText"), then sets its text with SetText(holder, text).
 * Highlighting is Invoke(parent, "option3.gotoAndPlay", "%s", "BuildOn").
 * So text is remembered per (parent, path), and a BuildOn names the clip in focus. */
static guest_fn o_resolve, o_invoke;
#define G_RESOLVE 0x82a399a0u
#define G_INVOKE  0x82a38050u
static CRITICAL_SECTION g_clock;
#define NHOLD 64
static struct { uint32_t holder, parent; char path[96]; } g_hold[NHOLD];
static int g_hold_next;
#define NCLIP 512
typedef struct { uint32_t parent; char path[96]; char text[256]; unsigned seq; } Clip;
static Clip g_clip[NCLIP];
static int g_clip_next;
static unsigned g_clip_seq;          /* holder addresses get reused across menus; the newest text wins */
#define FQ 64
typedef struct { uint32_t holder; char target[96]; char arg[32]; int stop; DWORD t; } FocusEv;  /* stop: gotoAndStop */
static FocusEv g_fq[FQ];
static LONG g_fq_w, g_fq_r;
static char g_krypt_zone[64];        /* Krypt: zone label just set (locationAnim.location.locationLabel) */
/* Krypt price markers koin0..koin4: screen X (0..1280) and scale (75 = the koffin locked on) */
#define NKM 8
static struct { uint32_t h; float x, scale; } g_km[NKM];
static void krypt_marker_num(uint32_t h, int is_scale, double v) {
    EnterCriticalSection(&g_clock);
    int s = -1;
    for (int i = 0; i < NKM && s < 0; i++) if (g_km[i].h == h) s = i;
    for (int i = 0; i < NKM && s < 0; i++) if (!g_km[i].h) s = i;
    if (s < 0) s = 0;
    g_km[s].h = h;
    if (is_scale) g_km[s].scale = (float)v; else g_km[s].x = (float)v;
    LeaveCriticalSection(&g_clock);
}
static int krypt_marker_get(uint32_t h, float *x, float *sc) {
    int ok = 0;
    EnterCriticalSection(&g_clock);
    for (int i = 0; i < NKM; i++) if (g_km[i].h == h) { *x = g_km[i].x; *sc = g_km[i].scale; ok = 1; break; }
    LeaveCriticalSection(&g_clock);
    return ok;
}

/* Move list range tags ([Movelist.Details]): how close a finishing move must be done.
 * Returns the spoken word, or NULL for any other text. */
static const char *ml_range_word(const char *t) {
    static const char *rng[][2] = { { "Touching", "touching" }, { "Close", "close" }, { "Sweep", "sweep" },
        { "Jump", "jump" }, { "Far", "far" }, { "Varies", "varies" }, { "Anywhere", "anywhere" }, { "Mid", "mid" } };
    for (size_t i = 0; i < sizeof rng / sizeof rng[0]; i++) if (!_stricmp(t, rng[i][0])) return rng[i][1];
    return NULL;
}
/* The move list sets the range tag of the row it moves to just before UpdateHighlight. */
static char g_ml_range[16];
static DWORD g_ml_range_t;
static int g_ml_scroll = -1;            /* move list ScrollBarUpdate: 0..100, ~3 at the top */
static char g_long_settext[300];        /* the last long SetText: a message box's whole body */
static DWORD g_long_settext_t;
static char g_lad_rounds[32], g_lad_matches[32];   /* Ladder screen: rounds / matches lost */
static int g_lad_stage;                 /* Ladder screen: "ladder_main" gotoAndPlay "stage<N>" = fight N next */

static void h_settext(PPCContext *ctx, uint8_t *base) {
    uint32_t h = (uint32_t)ctx->r3;
    char t[256]; gstr((uint32_t)ctx->r4, t, sizeof t);
    game_text_utf8(t, sizeof t);
    if (g_debug) log_line("SETTEXT obj %08x |%s|", h, t);
    {
        const char *rw = ml_range_word(t);
        if (rw) { snprintf(g_ml_range, sizeof g_ml_range, "%s", rw); g_ml_range_t = GetTickCount(); }
        /* Ladder screen stats: ladderStats.statN (value) is set just before labelN ("ROUNDS LOST:") */
        static char prev[32];
        const char *rl = loc_get("Ladder", "RoundsLost"), *ml = loc_get("Ladder", "MatchesLost");   /* any language */
        if (!strcmp(t, rl ? rl : "ROUNDS LOST:")) snprintf(g_lad_rounds, sizeof g_lad_rounds, "%s", prev);
        else if (!strcmp(t, ml ? ml : "MATCHES LOST:")) snprintf(g_lad_matches, sizeof g_lad_matches, "%s", prev);
        snprintf(prev, sizeof prev, "%.31s", t);
        if (strlen(t) >= 20) { snprintf(g_long_settext, sizeof g_long_settext, "%s", t); g_long_settext_t = GetTickCount(); }
    }
    fatality_input_settext(h, t);        /* Fatality Tutorial: detect the fatality being performed */
    InterlockedIncrement(&g_inflight);
    EnterCriticalSection(&g_clock);
    for (int i = 0; i < NHOLD; i++) {
        if (g_hold[i].holder != h) continue;
        int slot = -1;
        for (int c = 0; c < NCLIP; c++)
            if (g_clip[c].parent == g_hold[i].parent && !strcmp(g_clip[c].path, g_hold[i].path)) { slot = c; break; }
        if (slot < 0) { slot = g_clip_next; g_clip_next = (g_clip_next + 1) % NCLIP; }
        g_clip[slot].parent = g_hold[i].parent;
        memcpy(g_clip[slot].path, g_hold[i].path, sizeof g_clip[slot].path);
        memcpy(g_clip[slot].text, t, sizeof t);
        g_clip[slot].seq = ++g_clip_seq;
        size_t hl = strlen(g_hold[i].path);
        if (hl >= 14 && !strcmp(g_hold[i].path + hl - 14, ".locationLabel") && t[0])   /* Krypt: arrived in a zone */
            snprintf(g_krypt_zone, sizeof g_krypt_zone, "%.63s", t);
        break;
    }
    LeaveCriticalSection(&g_clock);
    InterlockedDecrement(&g_inflight);
    CALL_ORIG(o_settext, ctx, base);
}

static void h_resolve(PPCContext *ctx, uint8_t *base) {
    uint32_t h = (uint32_t)ctx->r3, parent = (uint32_t)ctx->r4;
    char p[96]; gstr((uint32_t)ctx->r5, p, sizeof p);
    if (g_debug) log_line("RESOLVE %08x = %08x . %s", h, parent, p);
    InterlockedIncrement(&g_inflight);
    EnterCriticalSection(&g_clock);
    int slot = -1;
    for (int i = 0; i < NHOLD; i++) if (g_hold[i].holder == h) { slot = i; break; }
    if (slot < 0) { slot = g_hold_next; g_hold_next = (g_hold_next + 1) % NHOLD; }
    g_hold[slot].holder = h; g_hold[slot].parent = parent;
    memcpy(g_hold[slot].path, p, sizeof p);
    LeaveCriticalSection(&g_clock);
    InterlockedDecrement(&g_inflight);
    mg_resolve(h, p);
    CALL_ORIG(o_resolve, ctx, base);
}

static void h_invoke(PPCContext *ctx, uint8_t *base) {
    char p[96], f[16], arg[32];
    uint32_t r6 = (uint32_t)ctx->r6;
    gstr((uint32_t)ctx->r4, p, sizeof p); gstr((uint32_t)ctx->r5, f, sizeof f);
    arg[0] = 0;
    /* script values {type, value}: a char * for %s, the number for %d */
    if (!strcmp(f, "%s")) gstr(g32(r6 + 4), arg, sizeof arg);
    else if (!strcmp(f, "%d")) snprintf(arg, sizeof arg, "%d", (int)g32(r6 + 4));
    if (!strcmp(f, "%f") && (!strcmp(p, "SetX") || !strcmp(p, "SetXScale"))) {   /* a script number = big-endian double */
        uint64_t q = ((uint64_t)g32(r6) << 32) | g32(r6 + 4); double dv; memcpy(&dv, &q, 8);
        krypt_marker_num((uint32_t)ctx->r3, p[4] == 'S', dv);
    }
    else if (!strcmp(f, "%f") && g_debug) {                 /* RE: how a number value is stored */
        uint64_t q = ((uint64_t)g32(r6 + 8) << 32) | g32(r6 + 12); double dv; memcpy(&dv, &q, 8);
        uint64_t q4 = ((uint64_t)g32(r6 + 4) << 32) | g32(r6 + 8); double dv4; memcpy(&dv4, &q4, 8);
        log_line("   %%f raw %08x %08x %08x %08x  f@4 %g  d@8 %g d@4 %g", g32(r6), g32(r6 + 4), g32(r6 + 8), g32(r6 + 12), gf(r6 + 4), dv, dv4);
    }
    if (g_debug) log_line("INVOKE %08x %s (%s) %s", (uint32_t)ctx->r3, p, f, arg);
    if (g_debug && strchr(f, ',')) {        /* several values: an array of {type, value} */
        for (int i = 0; i < 6; i++) {
            char s[96]; gstr(g32(r6 + 8 * i + 4), s, sizeof s);
            log_line("   arg%d type %08x |%s|", i, g32(r6 + 8 * i), s);
        }
    }
    size_t pl = strlen(p);
    int play = pl >= 11 && !strcmp(p + pl - 11, "gotoAndPlay"), stop = pl >= 11 && !strcmp(p + pl - 11, "gotoAndStop");
    if (!strcmp(p, "UpdateHighlight")) { stop = 2; pl = 0; }  /* move list row in focus: (%d) index */
    if (!strcmp(p, "ScrollBarUpdate") && arg[0]) g_ml_scroll = atoi(arg);
    int movie = pl > 16 && !strcmp(p + pl - 16, ".image.loadMovie");   /* card art: "img://char_sonya" */
    int wallet = !strcmp(p, "walletAnim.PlayNewAmount");                /* Krypt koin balance ("%s" "1250") */
    if (arg[0] && (play || stop || movie || wallet)) {
        InterlockedIncrement(&g_inflight);
        EnterCriticalSection(&g_clock);
        FocusEv *e = &g_fq[g_fq_w % FQ];
        e->holder = (uint32_t)ctx->r3;
        size_t tl = wallet ? 0 : movie ? pl - 16 : pl > 11 ? pl - 12 : 0;   /* path without ".gotoAndPlay" / ".image.loadMovie" */
        memcpy(e->target, p, tl); e->target[tl] = 0;
        memcpy(e->arg, arg, sizeof arg);
        e->stop = wallet ? 4 : movie ? 3 : stop;
        e->t = GetTickCount();
        g_fq_w++;
        LeaveCriticalSection(&g_clock);
        InterlockedDecrement(&g_inflight);
    }
    mg_invoke((uint32_t)ctx->r3, p, arg);
    CALL_ORIG(o_invoke, ctx, base);
}

/* SetVariable(holder, ?, "gameplay.option2.optionText.var_valueTxt", value): option values in
 * Options > Gameplay (On / Medium / 3 / 90) are set this way, not with SetText. */
#define G_SETVAR 0x82a393e0u
static guest_fn o_setvar;
static void h_setvar(PPCContext *ctx, uint8_t *base) {
    uint32_t h = (uint32_t)ctx->r3;
    char p[96], t[256];
    gstr((uint32_t)ctx->r4, p, sizeof p); gstr((uint32_t)ctx->r5, t, sizeof t);
    game_text_utf8(t, sizeof t);
    if (g_debug) log_line("SETVAR %08x %s |%s|", h, p, t);
    if (p[0]) {
        InterlockedIncrement(&g_inflight);
        EnterCriticalSection(&g_clock);
        int slot = -1;
        for (int c = 0; c < NCLIP; c++)
            if (g_clip[c].parent == h && !strcmp(g_clip[c].path, p)) { slot = c; break; }
        if (slot < 0) { slot = g_clip_next; g_clip_next = (g_clip_next + 1) % NCLIP; }
        g_clip[slot].parent = h;
        memcpy(g_clip[slot].path, p, sizeof p);
        memcpy(g_clip[slot].text, t, sizeof t);
        g_clip[slot].seq = ++g_clip_seq;
        /* a row's value changed: said by the menu thread if that row is in focus (stop 5) */
        static const char suf[] = ".optionText.var_valueTxt", suf2[] = ".preset.var_GamepadPresetToggle";
        size_t pl = strlen(p), sl = sizeof suf - 1;
        if (pl > sizeof suf2 - 1 && !strcmp(p + pl - (sizeof suf2 - 1), suf2)) sl = sizeof suf2 - 1;   /* Button Configuration preset */
        if (pl > sl && !strcmp(p + pl - sl, sl == sizeof suf - 1 ? suf : suf2) && t[0]) {
            FocusEv *e = &g_fq[g_fq_w % FQ];
            e->holder = h;
            memcpy(e->target, p, pl - sl); e->target[pl - sl] = 0;
            snprintf(e->arg, sizeof e->arg, "%.31s", t);
            e->stop = 5;
            e->t = GetTickCount();
            g_fq_w++;
        }
        LeaveCriticalSection(&g_clock);
        InterlockedDecrement(&g_inflight);
    }
    CALL_ORIG(o_setvar, ctx, base);
}

/* ClipEvent(holder, name): "FocusGain" / "FocusLost" on a menu item clip (pause menu). */
#define G_CLIPEVENT 0x82a37f08u
static guest_fn o_clipevent;
static void h_clipevent(PPCContext *ctx, uint8_t *base) {
    char n[32]; gstr((uint32_t)ctx->r4, n, sizeof n);
    if (g_debug) log_line("CLIPEVENT %08x %s", (uint32_t)ctx->r3, n);
    if (!strcmp(n, "FocusGain")) {
        InterlockedIncrement(&g_inflight);
        EnterCriticalSection(&g_clock);
        FocusEv *e = &g_fq[g_fq_w % FQ];
        e->holder = (uint32_t)ctx->r3; e->target[0] = 0;
        memcpy(e->arg, n, sizeof n); e->stop = 0; e->t = GetTickCount();
        g_fq_w++;
        LeaveCriticalSection(&g_clock);
        InterlockedDecrement(&g_inflight);
    }
    CALL_ORIG(o_clipevent, ctx, base);
}

/* ---------- mod thread ---------- */
/* Speech form of a game text: drop markup, Title Case for ALL CAPS (some
 * voices spell short all-caps words). */
static void tidy(const char *in, char *out, size_t cap) {
    size_t n = 0;
    int letters = 0, upper = 0;
    /* accented capitals in UTF-8 (Spanish, French...): C3 80-9E upper, C3 A0-BE lower (C3 97 / B7 are signs) */
#define U8LET(p) ((unsigned char)(p)[0] == 0xC3 && (unsigned char)(p)[1] >= 0x80 && (unsigned char)(p)[1] <= 0xBF && \
                  (unsigned char)(p)[1] != 0x97 && (unsigned char)(p)[1] != 0xB7)
    for (const char *p = in; *p; p++) {
        if (U8LET(p)) { letters++; if ((unsigned char)p[1] < 0xA0) upper++; p++; continue; }
        if (isalpha((unsigned char)*p)) { letters++; if (isupper((unsigned char)*p)) upper++; }
    }
    int caps = letters > 1 && upper == letters;
    int word_start = 1;
    for (const char *p = in; *p && n < cap - 1; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '<') { const char *e = strchr(p, '>'); if (e) { p = e; continue; } }
        /* a bullet or a line break ends a sentence: "Defeat Your Opponent!\n[-]Cage must..." */
        int brk = (ch == '[' && (p[1] == '-' || p[1] == '+') && p[2] == ']') ? 3 : (ch == '\\' && p[1] == 'n') ? 2 : 0;
        if (brk) {
            size_t m = n; while (m && out[m - 1] == ' ') m--;
            if (m && !strchr(".!?:", out[m - 1]) && n < cap - 2) { n = m; out[n++] = '.'; }
            if (n && out[n - 1] != ' ' && n < cap - 1) out[n++] = ' ';
            p += brk - 1; word_start = 1; continue;
        }
        if (ch == '[') {                                    /* button tokens in rule texts: "Press [RT] or [LT]" */
            static const char *btn[][2] = { { "[RT]", "right trigger" }, { "[LT]", "left trigger" },
                                            { "[RB]", "right bumper" }, { "[LB]", "left bumper" } };
            int hit = 0;
            for (unsigned k = 0; k < sizeof btn / sizeof btn[0] && !hit; k++) {
                size_t bl = strlen(btn[k][0]);
                if (strncmp(p, btn[k][0], bl)) continue;
                for (const char *w = btn[k][1]; *w && n < cap - 1; w++) out[n++] = *w;
                p += bl - 1; word_start = 0; hit = 1;
            }
            if (hit) continue;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        if (ch == ' ' && n && out[n - 1] == ' ') continue;
        if (caps && isalpha(ch) && word_start) {
            /* acronyms stay in capitals: "AI OPTIONS" -> "AI Options" */
            static const char *keep[] = { "AI", "CPU", "DLC", "HUD", "KO", "TV", "HD", "ID", "VS", "XP", "PC", "UI", NULL };
            size_t wl = 0; while (isalpha((unsigned char)p[wl])) wl++;
            int acr = 0;
            for (int k = 0; keep[k]; k++) if (strlen(keep[k]) == wl && !strncmp(p, keep[k], wl)) acr = 1;
            if (acr) { for (size_t k = 0; k < wl && n < cap - 1; k++) out[n++] = p[k]; p += wl - 1; word_start = 0; continue; }
        }
        if (U8LET(p)) {                                     /* "DESAFÍO" -> "Desafío", "SUBTÍTULOS" -> "Subtítulos" */
            if (n + 2 >= cap) break;
            unsigned char c2 = (unsigned char)p[1];
            out[n++] = (char)ch;
            out[n++] = (char)(caps && !word_start && c2 < 0xA0 ? c2 | 0x20 : c2);
            p++; word_start = 0; continue;
        }
        if (caps && isalpha(ch)) { out[n++] = (char)(word_start ? ch : tolower(ch)); word_start = 0; continue; }
        out[n++] = (char)ch;
        word_start = ch < 0x80 && !isalnum(ch) && ch != '\'';
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

void text_tidy(const char *in, char *out, size_t cap) { tidy(in, out, cap); }

static int is_name(uint32_t a) {
    if (a < 0x40000000u || !guest_ok(a, 4)) return 0;
    int n = 0;
    for (; n < 64; n++) { uint8_t c = g8(a + n); if (!c) break; if (c < 32 || c >= 127) return 0; }
    return n >= 1 && n < 64;
}

/* A menu item record at w[j]: {label key*, desc key*, id (small), 0, 0}. */
static int rec_shape(const uint32_t *w, int j) {
    if (j < 0 || j + 4 >= 0x4000) return 0;
    uint32_t lab = _byteswap_ulong(w[j]), desc = _byteswap_ulong(w[j + 1]), id = _byteswap_ulong(w[j + 2]);
    return id < 0x10000 && !w[j + 3] && !w[j + 4] && lab != desc && is_name(lab) && is_name(desc);
}

/* Menu item records near `keyaddr` (items come in lists, so a neighbouring
 * record must exist too). Returns 1 if keyaddr is a record's label, 2 if its
 * description; *other gets the record's other key, *rec the record's address. */
static int find_record(uint32_t keyaddr, uint32_t *other, uint32_t *rec) {
    uint32_t lo = (keyaddr & ~3u) - 0x40000, hi = (keyaddr & ~3u) + 0x40000;
    uint32_t want = _byteswap_ulong(keyaddr);
    for (uint32_t pg = lo >> 16; pg <= (hi - 1) >> 16; pg++) {
        uint32_t a0 = pg << 16;
        if (!guest_ok(a0, 0x10000)) continue;
        const uint32_t *w = (const uint32_t *)gptr(a0);
        for (int i = 1; i < 0x4000 - 4; i++) {
            if (w[i] != want) continue;
            int j = i - 1;                                        /* as description */
            if (rec_shape(w, j) && (rec_shape(w, j - 5) || rec_shape(w, j + 5))) {
                *other = _byteswap_ulong(w[j]); *rec = a0 + j * 4; return 2;
            }
            j = i;                                                /* as label */
            if (rec_shape(w, j) && (rec_shape(w, j - 5) || rec_shape(w, j + 5))) {
                *other = _byteswap_ulong(w[j + 1]); *rec = a0 + j * 4; return 1;
            }
        }
    }
    return 0;
}

/* ---------- input: speech for a lone lookup only follows the player's own input ---------- */
static DWORD g_input_t;
static void watch_input(void) {
    static const int vks[] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'W', 'A', 'S', 'D', 'J', 'K', 'L', 'U', 'I', 'O', 'P',
                               VK_OEM_1, VK_RETURN, VK_SPACE, VK_ESCAPE, VK_BACK, VK_TAB,
                               'T', 'G', '1', '4', VK_SHIFT, VK_CONTROL };   /* the original PC keys (keys.c) too */
    static uint8_t down[sizeof vks / sizeof vks[0]];
    static uint16_t lastbtn;
    DWORD now = GetTickCount();
    if (game_has_focus())
        for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) {
            int d = GetAsyncKeyState(vks[i]) < 0;
            if (d && !down[i]) g_input_t = now;
            down[i] = (uint8_t)d;
        }
    uint16_t b = g_pad_buttons;
    if (b & ~lastbtn) g_input_t = now;
    lastbtn = b;
    if (g_pad_lx > 16000 || g_pad_lx < -16000 || g_pad_ly > 16000 || g_pad_ly < -16000) g_input_t = now;
}

/* ---------- speech ---------- */
static char g_said[1024];
static DWORD g_said_t;
static char g_focus_label[512];        /* label of the item in focus (title for a submenu it opens) */

/* menus open: list id = address of its first label key */
#define STACK 12
static struct { uint32_t list; char title[512]; } g_stack[STACK];
static int g_nstack;

/* What the game really returned wins over the table (DLC texts, script-built texts). */
static void cache_put(const char *s, const char *k, const char *v) {
    if (!s[0] || !k[0] || !v[0]) return;
    const char *have = loc_get(s, k);
    if (have && !strcmp(have, v)) return;
    if (have) {
        for (int i = g_nloc - 1; i >= 0; i--)
            if (g_loc[i].val == have) { free(g_loc[i].val); g_loc[i].val = _strdup(v); return; }
    }
    loc_put(s, k, v);
    loc_index();
}

static int ends_with(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !_stricmp(s + a - b, suf);
}

/* Button prompts and system texts: never spoken as a focus change. */
static int quiet_key(const char *sec, const char *key) {
    if (!_strnicmp(key, "Nav", 3) || !_strnicmp(key, "PressStart", 10)) return 1;
    if (ends_with(key, "Accept") || ends_with(key, "Back") || ends_with(key, "Cancel") || ends_with(key, "Invite")) return 1;
    if (!_stricmp(sec, "UserDataFiles")) return 1;
    /* social-network post templates ("%s just performed their first Fatality..."),
     * localized after a first fatality / trophy but never shown on screen */
    if (!_stricmp(sec, "SocialMessages")) return 1;
    /* combo counter labels: said as "Damage. Hits" on every combo, without the numbers */
    if (!_stricmp(sec, "Gameplay.Fighting") && (!_stricmp(key, "Hit") || !_stricmp(key, "Hits") || !_stricmp(key, "Damage"))) return 1;
    /* Challenge Tower chrome (button labels, column headers, scroll hints): the
     * card content is read from [TowerMission] by the focus handler instead. */
    if (!_stricmp(sec, "ChallengeTower")) return 1;
    /* Move list detail tags: the fatality range words read oddly on their own
     * ("Varies", "Jump"); drop those but keep real notes ("Can be done in the air"). */
    if (!_stricmp(sec, "Movelist.Details") && ml_range_word(key)) return 1;   /* said with the fatality row */
    return 0;
}

/* first: interrupts; then: queued after it (a description). The same first text
 * twice within 400 ms (two hooks seeing one move) is said once. */
static char g_said_then[1024];
/* "Tarkatans.." / "Ambushed!." (pieces joined, or a title ending in "!"): one mark; "..." stays */
static void fix_dots(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < cap; i++) {
        if (in[i] == '.' && o && strchr(".!?", out[o - 1]) && !(in[i + 1] == '.' || (o >= 2 && out[o - 2] == '.'))) continue;
        out[o++] = in[i];
    }
    out[o] = 0;
}
static void say_raw(const char *first, const char *then);
static void say(const char *first0, const char *then0) {
    char first[1600], then[1100];
    fix_dots(first0, first, sizeof first);
    fix_dots(then0 ? then0 : "", then, sizeof then);
    say_raw(first, then0 ? then : NULL);
}
static void say_raw(const char *first, const char *then) {
    if (!first[0]) return;
    int dup = !_stricmp(first, g_said) && GetTickCount() - g_said_t < 400;
    if (!dup) {
        snprintf(g_said, sizeof g_said, "%s", first); g_said_t = GetTickCount();
        g_said_then[0] = 0;
        speak_utf8(first, 1);
    }
    if (then && then[0] && _stricmp(first, then) && strcmp(then, g_said_then)) {
        snprintf(g_said_then, sizeof g_said_then, "%s", then);
        speak_utf8(then, 0);
    }
}

static char g_fighter_said[256];       /* character select: last fighter name said */
static DWORD g_fighter_said_t;
static DWORD g_allow_until;            /* a lone lookup right after a screen opens is its first focus */
static DWORD g_screen_said_t;
static int g_grid_cell = -1;           /* character select: cell in focus (Highlight<N>) */
static DWORD g_grid_t;
static uint32_t g_focus_holder;        /* Flash list in focus (its title is said when focus enters it) */

/* Text the script set inside clip `target` of `holder` (e.g. option3.text.varText). */
static int clip_text(uint32_t holder, const char *target, char *out, size_t cap) {
    size_t tl = strlen(target);
    int found = 0;
    EnterCriticalSection(&g_clock);
    for (int c = 0; c < NCLIP && !found; c++) {
        if (g_clip[c].parent != holder || !g_clip[c].text[0]) continue;
        if (strncmp(g_clip[c].path, target, tl)) continue;
        const char *rest = g_clip[c].path + tl;
        if (tl) { if (!*rest) goto take; if (*rest != '.') continue; rest++; }
        /* the clip's own text field (option3.text.varText), not text deeper inside a whole panel */
        if (!_strnicmp(rest, "text", 4) || !_strnicmp(rest, "label", 5) || !_strnicmp(rest, "var", 3) ||
            !_strnicmp(rest, "txt", 3) || !_strnicmp(rest, "name", 4)) {
        take:
            snprintf(out, cap, "%s", g_clip[c].text); found = 1;
        }
    }
    /* an item clip (pause menu "mc_option_1") holding one text child under any name ("menuOp").
     * Holders are reused across menus, so stale children linger: take the newest text. */
    if (!tl) {
        int best = -1;
        for (int c = 0; c < NCLIP; c++)
            if (g_clip[c].parent == holder && g_clip[c].text[0] && (best < 0 || g_clip[c].seq > g_clip[best].seq)) best = c;
        if (best >= 0) { snprintf(out, cap, "%s", g_clip[best].text); found = 1; }
    }
    LeaveCriticalSection(&g_clock);
    return found;
}

/* A title text in list `holder` ("title.text", "header.mc_title.title_text"). */
static int clip_title(uint32_t holder, char *out, size_t cap) {
    int found = 0;
    EnterCriticalSection(&g_clock);
    for (int c = 0; c < NCLIP && !found; c++) {
        if (g_clip[c].parent != holder || !g_clip[c].text[0]) continue;
        char low[96]; size_t i = 0;
        for (; g_clip[c].path[i] && i < sizeof low - 1; i++) low[i] = (char)tolower((unsigned char)g_clip[c].path[i]);
        low[i] = 0;
        if (strstr(low, "title")) { snprintf(out, cap, "%s", g_clip[c].text); found = 1; }
    }
    LeaveCriticalSection(&g_clock);
    return found;
}

/* An exact (holder, path) text the script set, e.g. a Challenge Tower card field. */
static int clip_exact(uint32_t holder, const char *path, char *out, size_t cap) {
    int found = 0;
    EnterCriticalSection(&g_clock);
    for (int c = 0; c < NCLIP; c++)
        if (g_clip[c].parent == holder && !strcmp(g_clip[c].path, path)) {
            snprintf(out, cap, "%s", g_clip[c].text); found = 1; break;
        }
    LeaveCriticalSection(&g_clock);
    return found;
}

/* clip_exact plus the text's sequence number (0 = none): a header set again means the panel reopened. */
static unsigned g_row_title_seq;
static unsigned clip_seq(uint32_t holder, const char *path, char *out, size_t cap) {
    unsigned seq = 0;
    EnterCriticalSection(&g_clock);
    for (int c = 0; c < NCLIP; c++)
        if (g_clip[c].parent == holder && !strcmp(g_clip[c].path, path) && g_clip[c].text[0]) {
            snprintf(out, cap, "%s", g_clip[c].text); seq = g_clip[c].seq; break;
        }
    LeaveCriticalSection(&g_clock);
    return seq;
}

/* ---------- Challenge Tower ----------
 * Cards are containers.containerN.challengeInfo; the current card (the nameplate
 * slot) gets Invoke "challengeInfo.gotoAndPlay" focusOn, which is the only signal
 * the tower gives as you scroll. Each card holds numberTextClip.numberText,
 * titleTextClipTop.titleText, reward.group.rewardCoins.text and a lockStatus clip
 * (gotoAndPlay locked/unlocked). Details (the card flip, InfoOn) adds
 * infoText.text (the objective) and skipAnim.skipCost.text. */
#define NTLK 8
typedef struct {
    char key[96];          /* "containers.container3.challengeInfo" */
    char lock[24];         /* lockStatus frame: "locked", "unlocked", ... */
    int nopp;              /* opponents shown ("Chars2" = 2) */
    char opp[3][40];       /* their image names ("quanchi") */
    char icon[40];         /* challenge type image ("Vs", "TestYourMight") */
} TCard;
static TCard g_tc[NTLK];
static int g_tc_n;
static TCard *tcard(const char *key) {
    for (int i = 0; i < g_tc_n; i++) if (!strcmp(g_tc[i].key, key)) return &g_tc[i];
    if (g_tc_n == NTLK) { memmove(g_tc, g_tc + 1, sizeof g_tc[0] * (NTLK - 1)); g_tc_n--; }
    TCard *c = &g_tc[g_tc_n++];
    memset(c, 0, sizeof *c);
    snprintf(c->key, sizeof c->key, "%s", key);
    return c;
}
static char g_tower_me[40];            /* playerModule: the fighter you play in the card at the slot */
static char g_tower_pfx[96];           /* the card at the slot, spoken once scrolling settles */
static uint32_t g_tower_holder;
static DWORD g_tower_t;
static DWORD g_tower_evt_t;            /* last Challenge Tower card event: the tower menu is up */
/* In the Challenge Tower (its menu was up since the main menu): only then are [TowerMission]
 * / [TowerIcons] texts a challenge's own announcements. Elsewhere (demo fights, other modes)
 * the game looks them up too, and "Damage Boost" or "Blocking Disabled" out of nowhere
 * is noise. Cleared when the main menu list is built ([MainMenu] ChallengeTower). */
static int g_tower_session;

/* "sonya" -> "Sonya Blade" (the character select name), "jax_f" -> "Jax", "zombie" -> "Zombie" */
static void tower_char_name(const char *img, char *out, size_t cap) {
    char k[40]; size_t n = 0;
    for (const char *p = img; *p && *p != '_' && n < sizeof k - 1; p++) k[n++] = (char)tolower((unsigned char)*p);
    k[n] = 0;
    out[0] = 0;
    if (!strcmp(k, "select")) { snprintf(out, cap, "a hidden fighter"); return; }
    static const char *alias[][2] = { { "cyberninja", "Cyber Ninja" }, { "genericfighterb", "Generic Fighter" },
                                      { "cyberzero", "Cyber Sub-Zero" }, { "noob", "Noob Saibot" } };
    for (unsigned i = 0; i < sizeof alias / sizeof alias[0]; i++) if (!strcmp(k, alias[i][0])) { snprintf(out, cap, "%s", alias[i][1]); return; }
    const char *t = loc_get("PlayerSelect", k);
    if (t && t[0]) { tidy(t, out, cap); return; }
    if (k[0]) { snprintf(out, cap, "%s", k); out[0] = (char)toupper((unsigned char)out[0]); }
}

/* "TestYourMight" -> "Test Your Might"; "SlotMachine" -> "Test Your Luck"; "Vs" (a fight) -> "" */
static void tower_icon_name(const char *icon, char *out, size_t cap) {
    out[0] = 0;
    if (!icon[0] || !_stricmp(icon, "Vs")) return;
    if (!_stricmp(icon, "SlotMachine")) { snprintf(out, cap, "Test Your Luck"); return; }
    size_t n = 0;
    for (const char *p = icon; *p && n < cap - 2; p++) {
        if (p != icon && isupper((unsigned char)*p) && n) out[n++] = ' ';
        out[n++] = *p;
    }
    out[n] = 0;
}

static void tower_image(const FocusEv *e) {
    const char *img = e->arg;
    if (_strnicmp(img, "img://", 6)) return;
    img += 6;
    int ch = !_strnicmp(img, "char_", 5), ic = !_strnicmp(img, "icon_", 5);
    if (!ch && !ic) return;
    img += 5;
    if (!strcmp(e->target, "playerModule.playerCharacter1")) { if (ch) snprintf(g_tower_me, sizeof g_tower_me, "%s", img); return; }
    const char *ci = strstr(e->target, "challengeInfo");
    if (!ci) return;
    char pfx[96]; size_t pl = (size_t)(ci - e->target) + sizeof "challengeInfo" - 1;
    if (pl >= sizeof pfx) return;
    memcpy(pfx, e->target, pl); pfx[pl] = 0;
    TCard *c = tcard(pfx);
    const char *rest = e->target + pl;
    if (ic && !strcmp(rest, ".challengeIcon")) snprintf(c->icon, sizeof c->icon, "%s", img);
    else if (ch && !strncmp(rest, ".opponents.character", 20)) {
        int i = atoi(rest + 20) - 1;
        if (i >= 0 && i < 3) snprintf(c->opp[i], sizeof c->opp[0], "%s", img);
    }
}

/* "Challenge 3, Demon Hunter. Sonya Blade against Quan Chi and Shang Tsung. Reward 400 koins" */
static void tower_say_card(void) {
    char path[160], num[32] = "", traw[160], tit[160] = "", rew[32] = "", msg[600];
    const char *pfx = g_tower_pfx;
    uint32_t h = g_tower_holder;
    snprintf(path, sizeof path, "%s.numberTextClip.numberText", pfx); clip_exact(h, path, num, sizeof num);
    snprintf(path, sizeof path, "%s.titleTextClipTop.titleText", pfx);
    if (clip_exact(h, path, traw, sizeof traw)) tidy(traw, tit, sizeof tit);
    snprintf(path, sizeof path, "%s.reward.group.rewardCoins.text", pfx); clip_exact(h, path, rew, sizeof rew);
    TCard *c = tcard(pfx);
    snprintf(msg, sizeof msg, "Challenge %s, %s", num[0] ? num : "?", tit);
    if (!_stricmp(c->lock, "locked")) strncat(msg, ", locked", sizeof msg - strlen(msg) - 1);
    else if (c->lock[0] && _stricmp(c->lock, "unlocked")) {           /* another state, e.g. completed */
        char st[32]; tidy(c->lock, st, sizeof st);
        strncat(msg, ", ", sizeof msg - strlen(msg) - 1); strncat(msg, st, sizeof msg - strlen(msg) - 1);
    }
    char kind[48]; tower_icon_name(c->icon, kind, sizeof kind);
    char me[64] = ""; if (g_tower_me[0]) tower_char_name(g_tower_me, me, sizeof me);
    char vs[200] = "";
    for (int i = 0; i < c->nopp && i < 3; i++) {
        char nm[64]; tower_char_name(c->opp[i], nm, sizeof nm);
        if (!nm[0]) continue;
        if (vs[0]) strncat(vs, i == c->nopp - 1 ? " and " : ", ", sizeof vs - strlen(vs) - 1);
        strncat(vs, nm, sizeof vs - strlen(vs) - 1);
    }
    char who[300] = "";
    if (me[0] && vs[0]) snprintf(who, sizeof who, "%s against %s", me, vs);
    else if (me[0]) snprintf(who, sizeof who, "As %s", me);
    else if (vs[0]) snprintf(who, sizeof who, "Against %s", vs);
    if (kind[0] && _stricmp(kind, tit)) { strncat(msg, ". ", sizeof msg - strlen(msg) - 1); strncat(msg, kind, sizeof msg - strlen(msg) - 1); }
    if (who[0]) { strncat(msg, ". ", sizeof msg - strlen(msg) - 1); strncat(msg, who, sizeof msg - strlen(msg) - 1); }
    if (rew[0]) {
        strncat(msg, ". Reward ", sizeof msg - strlen(msg) - 1); strncat(msg, rew, sizeof msg - strlen(msg) - 1);
        strncat(msg, " koins", sizeof msg - strlen(msg) - 1);
    }
    say(msg, NULL);
}

static int hold_path(uint32_t h, char *out, size_t cap) {
    int found = 0;
    EnterCriticalSection(&g_clock);
    for (int i = 0; i < NHOLD; i++) if (g_hold[i].holder == h) { snprintf(out, cap, "%s", g_hold[i].path); found = 1; break; }
    LeaveCriticalSection(&g_clock);
    return found;
}

/* ---------- Krypt ----------
 * Moving stops the camera at a koffin: navAnim gotoAndPlay "On_2LockedIn", its label
 * in navAnim.purchaseInfo.label ("DL 9" = Deadlands 9; "Nekropolis" at the gate).
 * About 1.4 s later the price markers koin0..koin4 build (SetX/SetY on screen, the
 * price in koinN.price.amount); koin0 is the koffin locked on (largest, scale 75).
 * The balance comes as walletAnim.PlayNewAmount("1250"), the zone name in
 * locationAnim.location.locationLabel on arrival. */
static DWORD g_krypt_lock_t;
static char g_krypt_said[128]; static DWORD g_krypt_said_t;    /* last koffin said ("Bloodmarsh 1") */
static int g_krypt_locked;           /* the camera is on a koffin (its price is g_krypt_price) */
static int g_krypt_price = -1, g_krypt_balance = -1, g_krypt_price_said;
static DWORD g_krypt_active_t;       /* last Krypt event: F4 belongs to the Krypt while recent */
static DWORD g_krypt_ping_t;         /* when to play the other koffins' pings */
static DWORD g_krypt_select_t;       /* last Select press in the Krypt (a purchase may follow) */
static int g_krypt_pings_on = 1;
static int g_krypt_map, g_krypt_prize;  /* the map (I) / a prize view is open: arrows don't walk */
/* koin balance (walletAnim.PlayNewAmount): in the Krypt on entry and after a purchase, and after fights too */
static int g_wallet_pending, g_wallet_val, g_wallet_base, g_wallet_last = -1; static DWORD g_wallet_t;
typedef struct { int valid, price; float x, scale; } KNb;
static KNb g_knb[4];                 /* koin1..koin4: other koffins in view */

/* The other koffins in view, left to right: one soft ping each. pan = side of the
 * view (equal power, width 0.8), pitch step = near (660 Hz) or far (440 Hz). */
static void krypt_pings(void) {
    int ord[4], n = 0;
    for (int i = 0; i < 4; i++) if (g_knb[i].valid) ord[n++] = i;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (g_knb[ord[b]].x < g_knb[ord[a]].x) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
    float delay = 0;
    for (int k = 0; k < n; k++) {
        const KNb *nb = &g_knb[ord[k]];
        float side = (nb->x - 640.0f) / 560.0f; if (side < -1) side = -1; if (side > 1) side = 1;
        int is_near = nb->scale >= 35.0f;
        float f = is_near ? 660.0f : 440.0f;
        tone_at(delay, f, f, 0.07f, is_near ? 0.125f : 0.085f, 0.8f * side, W_SOFT);
        delay += 0.09f;
    }
}

static void krypt_side(float x, char *out, size_t cap) {
    snprintf(out, cap, "%s", x < 470 ? "left" : x > 810 ? "right" : "ahead");
}

/* F4 in the Krypt: the other koffins in view; Shift+F4: pings on/off. 0 = not in the Krypt. */
int krypt_key(int vk, int shift) {
    if (vk != VK_F4 || !g_krypt_active_t || GetTickCount() - g_krypt_active_t > 600000) return 0;
    if (shift) {
        g_krypt_pings_on = !g_krypt_pings_on;
        speak_utf8(g_krypt_pings_on ? "Koffin pings on" : "Koffin pings off", 1);
        return 1;
    }
    char msg[400] = "";
    int ord[4], n = 0;
    for (int i = 0; i < 4; i++) if (g_knb[i].valid) ord[n++] = i;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (g_knb[ord[b]].x < g_knb[ord[a]].x) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
    for (int k = 0; k < n; k++) {
        const KNb *nb = &g_knb[ord[k]];
        char sd[16], one[96]; krypt_side(nb->x, sd, sizeof sd);
        snprintf(one, sizeof one, "%s%s, %s, %d koins", k ? ". " : "", sd, nb->scale >= 35.0f ? "near" : "far", nb->price);
        strncat(msg, one, sizeof msg - strlen(msg) - 1);
    }
    if (!n) snprintf(msg, sizeof msg, g_krypt_locked ? "No other koffins in view" : "Not at a koffin");
    if (g_krypt_balance >= 0) { char b[48]; snprintf(b, sizeof b, ". You have %d koins", g_krypt_balance); strncat(msg, b, sizeof msg - strlen(msg) - 1); }
    speak_utf8(msg, 1);
    return 1;
}
static void krypt_label(const char *raw, char *out, size_t cap) {
    static const char *zone[][2] = { { "DL", "Deadlands" }, { "BM", "Bloodmarsh" }, { "MD", "Meadow of Despair" },
                                     { "HI", "Hollow of Infestation" } };
    char code[8] = ""; int num = 0;
    if (sscanf(raw, "%7[A-Za-z] %d", code, &num) == 2) {
        for (unsigned i = 0; i < sizeof zone / sizeof zone[0]; i++)
            if (!_stricmp(code, zone[i][0])) { snprintf(out, cap, "%s %d", zone[i][1], num); return; }
        char sp[24] = ""; size_t k = 0;                    /* an unknown zone code: spell it */
        for (const char *c = code; *c && k < sizeof sp - 2; c++) { if (k) sp[k++] = ' '; sp[k++] = *c; }
        sp[k] = 0;
        snprintf(out, cap, "%s %d", sp, num);
        return;
    }
    if (!_stricmp(raw, "Nekropolis")) { snprintf(out, cap, "Nekropolis gate"); return; }
    tidy(raw, out, cap);
}

static uint32_t hold_parent(uint32_t h) {
    uint32_t p = 0;
    EnterCriticalSection(&g_clock);
    for (int i = 0; i < NHOLD; i++) if (g_hold[i].holder == h) { p = g_hold[i].parent; break; }
    LeaveCriticalSection(&g_clock);
    return p;
}

/* The balance (walletAnim.PlayNewAmount). Entering the Krypt it comes just before the zone name: said after it.
 * In the Krypt (after a purchase) it is said at once. Elsewhere (fight results) the game shows the old balance and
 * then counts up to the new one: only the settled new total is said, and nothing when it did not change. */
static void wallet_poll(int zone_now) {
    if (!g_wallet_pending) return;
    DWORD age = GetTickCount() - g_wallet_t;
    int in_krypt = g_krypt_active_t && GetTickCount() - g_krypt_active_t < 600000;
    char m[48];
    if (zone_now || (in_krypt && age > 300)) {
        snprintf(m, sizeof m, "%d koins", g_wallet_val); speak_utf8(m, 0);
    } else if (age > 1500) {
        if (in_krypt || g_wallet_val != g_wallet_base) { snprintf(m, sizeof m, "%d koins", g_wallet_val); speak_utf8(m, 0); }
    } else return;
    g_wallet_last = g_wallet_val; g_wallet_pending = 0;
}

/* Krypt walking. The camera's target position (two guest floats, x then y, at +0x1F4 in a Krypt camera object)
 * jumps to the next spot within one frame of the press, while the camera glides there for about a second. A change =
 * footsteps (2, or 3 for a long move); a direction press that moved nothing, said nothing and opened nothing = a dull
 * bump. The object starts with its class pointer 0x820228a0 (the game's fixed code image; one such object in the
 * Krypt). It sat at 0x40e2cb70 in every run measured; if not, the heap is scanned for the class pointer. */
#define KCAM_VT 0x820228a0u
#define KCAM_POS 0x1F4u
static uint32_t g_kcam = 0x40e2cb70u;
static int g_snd_step[4] = { -1, -1, -1, -1 }, g_snd_bump = -1, g_snd_loaded;
static uint32_t kcam_scan(void) {
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t a = g_guest_base + 0x40000000u, end = g_guest_base + 0x50000000u;
    const uint32_t want = _byteswap_ulong(KCAM_VT);
    while (a < end && VirtualQuery((void *)a, &mbi, sizeof mbi)) {
        uintptr_t rb = (uintptr_t)mbi.BaseAddress, re = rb + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD))
            for (const uint32_t *p = (const uint32_t *)((rb + 3) & ~(uintptr_t)3); (uintptr_t)(p + 1) <= re && (uintptr_t)p < end; p++)
                if (*p == want) return (uint32_t)((uintptr_t)p - g_guest_base);
        if (re <= a) break;
        a = re;
    }
    return 0;
}
static int krypt_cam(float *x, float *y) {
    static DWORD last_scan;
    if (!guest_ok(g_kcam, KCAM_POS + 8) || g32(g_kcam) != KCAM_VT) {
        if (GetTickCount() - last_scan < 3000) return 0;
        last_scan = GetTickCount();
        uint32_t f = kcam_scan();
        if (!f) return 0;
        log_line("krypt: camera object at %08x", f);
        g_kcam = f;
    }
    *x = gf(g_kcam + KCAM_POS); *y = gf(g_kcam + KCAM_POS + 4);
    return isfinite(*x) && isfinite(*y) && fabsf(*x) < 1e6f && fabsf(*y) < 1e6f;
}
static void krypt_steps(float dist) {
    static int foot, pick;
    int n = dist < 450 ? 2 : 3;
    for (int i = 0; i < n; i++) {
        pick = (pick + 1 + rand() % 3) % 4;
        float rate = 0.94f + (rand() % 9) * 0.01f;
        sfx_play(g_snd_step[pick], 0.31f * i, 0.17f, foot ? 0.12f : -0.12f, rate);
        foot = !foot;
    }
}
static void krypt_motion(void) {
    static int have, prev_dir, warned;
    static float lx, ly, bx, by;
    static DWORD moved_t, bump_t; static LONG bump_said;
    DWORD now = GetTickCount();
    if (!g_krypt_active_t || now - g_krypt_active_t > 600000) { have = 0; bump_t = 0; g_krypt_map = g_krypt_prize = 0; return; }
    if (!g_snd_loaded) {
        g_snd_loaded = 1;
        static const wchar_t *steps[4] = { L"krypt_step1.wav", L"krypt_step2.wav", L"krypt_step3.wav", L"krypt_step4.wav" };
        for (int i = 0; i < 4; i++) g_snd_step[i] = sfx_load(steps[i]);
        g_snd_bump = sfx_load(L"krypt_bump.wav");
    }
    float x, y;
    if (!krypt_cam(&x, &y)) {
        if (!warned) { warned = 1; log_line("krypt: camera position not found (no walking sounds)"); }
        have = 0; return;
    }
    if (have && (fabsf(x - lx) > 1 || fabsf(y - ly) > 1)) {
        float d = hypotf(x - lx, y - ly);
        if (!g_krypt_map && !g_krypt_prize && d < 3000) krypt_steps(d);   /* a fast travel jumps much further */
        moved_t = now; bump_t = 0;
    }
    lx = x; ly = y; have = 1;
    int dir = (game_has_focus() && (GetAsyncKeyState(VK_LEFT) < 0 || GetAsyncKeyState(VK_RIGHT) < 0 || GetAsyncKeyState(VK_UP) < 0 ||
                                    GetAsyncKeyState(VK_DOWN) < 0 || GetAsyncKeyState('W') < 0 || GetAsyncKeyState('A') < 0 ||
                                    GetAsyncKeyState('S') < 0 || GetAsyncKeyState('D') < 0)) ||
              (g_pad_buttons & 0x000F) || abs(g_pad_lx) > 20000 || abs(g_pad_ly) > 20000;
    /* a press during the glide (about a second) is ignored by the game: no bump for it */
    if (dir && !prev_dir && !g_krypt_map && !g_krypt_prize && now - moved_t > 1200) {
        bump_t = now + 350; bump_said = g_speak_count; bx = x; by = y;
    }
    prev_dir = dir;
    if (bump_t && (LONG)(now - bump_t) >= 0) {
        if (x == bx && y == by && g_speak_count == bump_said && !g_krypt_map && !g_krypt_prize)
            sfx_play(g_snd_bump, 0, 0.2f, 0, 1.0f);
        bump_t = 0;
    }
}

/* slider values: "audioVideo.slider4.slider" gotoAndStop N -> value of audioVideo.slider4 */
#define NVAL 64
static struct { uint32_t holder; char target[96]; int v; } g_val[NVAL];
static int g_val_next;
static int *val_slot(uint32_t holder, const char *target, int make) {
    for (int i = 0; i < NVAL; i++) if (g_val[i].holder == holder && !strcmp(g_val[i].target, target)) return &g_val[i].v;
    if (!make) return NULL;
    int i = g_val_next; g_val_next = (g_val_next + 1) % NVAL;
    g_val[i].holder = holder; snprintf(g_val[i].target, sizeof g_val[i].target, "%s", target);
    return &g_val[i].v;
}
static uint32_t g_focus_h; static char g_focus_target[96];   /* the Flash item in focus */
static DWORD g_focus_said_t;          /* when an option row / Button Configuration item was announced */

static int focus_arg(const char *a) {
    return !_stricmp(a, "BuildOn") || !_stricmp(a, "HighlightOn") || !_stricmp(a, "Highlight") ||
           !_stricmp(a, "FocusOn") || !_stricmp(a, "On") || !_stricmp(a, "Selected_On");
}

/* ---------- the in-game move list (pause > Move List) ----------
 * Opening it (or switching page with J) localizes every row of [movelist] in
 * order: a heading, a blank or a note ("(Uses three bars of super)"), then
 * name / input pairs ("Doom Slice", "[Towards][+][BP]").
 * The row in focus: every move sends Invoke "ScrollBarUpdate" (%d) =
 * round(100 * row index / number of rows) (Split Decision 44 of 54 -> 81,
 * Babality 52/54 -> 96, Front Punch 2/64 -> 3), so the nearest move name to
 * that point is the row, with no history to go wrong. (The old way, counting
 * the window index of "UpdateHighlight", drifted past the end of the list on
 * extra Down presses at the bottom and lost its place on page switches: the
 * game keeps the scroll position when J switches pages.) At the ends nothing
 * scrolls and the last row is said again. */
#define MLR 400
static char g_ml[MLR][96];
/* Per page ("<fighter>|<first heading>"): the range tag seen for each finishing-move row
 * (the game sets it only when the row is moved to, not on a page switch). */
static struct { char key[128]; char rng[MLR][16]; } g_ml_mem[8];
static int g_ml_slot;
#define g_ml_rng (g_ml_mem[g_ml_slot].rng)
static int g_nml;
static char g_ml_section[96];
static char g_ml_who[96];               /* fighter name, remembered for the super moves page */
static int g_ml_pending = -1;           /* a highlight came: say the row once the frame's events are in */
static DWORD g_ml_due;                  /* ... not before then (page opening) */
static int g_ml_prev_h = -1;
static DWORD g_ml_prev_t;

/* "BASIC ATTACKS": capitals, followed by a blank row or a note ("X-RAY ATTACK",
 * "(Uses three bars of super)") */
static int ml_heading(int i) {
    if (i < 0 || i + 1 >= g_nml || !g_ml[i][0] || g_ml[i][0] == '[') return 0;
    if (g_ml[i + 1][0] && g_ml[i + 1][0] != '(') return 0;
    for (const char *p = g_ml[i]; *p; p++) if (islower((unsigned char)*p)) return 0;
    return 1;
}

static int ml_selectable(int i) {   /* a move name: not an input, note, blank or heading */
    return i >= 0 && i < g_nml && g_ml[i][0] && g_ml[i][0] != '[' && g_ml[i][0] != '(' && !ml_heading(i);
}

static int ml_row_at_scroll(void) { /* the move at the scroll bar's point, or -1 */
    if (g_ml_scroll < 0 || g_nml <= 0) return -1;
    double x = g_ml_scroll * g_nml / 100.0, bd = 1e9;
    int best = -1;
    for (int i = 0; i < g_nml; i++)
        if (ml_selectable(i) && fabs(i - x) < bd) { bd = fabs(i - x); best = i; }
    return best;
}

/* UpdateHighlight: the window index, 0 then the row's index in the same frame when a
 * page opens or J switches it. Then the old page's row would be said and cut off by the
 * new page's title a few ms later: wait a moment, the new page's rows cancel it. */
static void ml_highlight(int h, DWORD t) {
    if (g_ml_prev_h == 0 && h != 0 && t - g_ml_prev_t < 20) g_ml_due = GetTickCount() + 100;
    g_ml_prev_h = h; g_ml_prev_t = t;
    g_ml_pending = 1;
}

static void ml_row(int i, int with_section);

/* The page's slot (range tags), kept per fighter and page. */
static int ml_mem_slot(const char *key) {
    static int next;
    for (int k = 0; k < 8; k++) if (!strcmp(g_ml_mem[k].key, key)) return k;
    int k = next; next = (next + 1) % 8;
    memset(&g_ml_mem[k], 0, sizeof g_ml_mem[k]);
    snprintf(g_ml_mem[k].key, sizeof g_ml_mem[k].key, "%s", key);
    return k;
}

/* Say the row the list settled on this frame (skipping a heading sends 2-3 highlights);
 * the range tag set this frame belongs to it. */
static void ml_say_pending(void) {
    if (g_ml_pending < 0 || (LONG)(GetTickCount() - g_ml_due) < 0) return;
    g_ml_pending = -1;
    int i = ml_row_at_scroll();
    if (i < 0) return;
    if (g_ml_range_t && GetTickCount() - g_ml_range_t < 300)
        snprintf(g_ml_rng[i], sizeof g_ml_rng[0], "%s", g_ml_range);
    g_ml_range_t = 0;
    ml_row(i, 0);
}

/* The words for row i: "<Section>. <Move>: <input>[. Distance: <range>]"; the section
 * only when it changed (or with_section). */
static void ml_row_text(int i, int with_section, char *msg, size_t cap) {
    msg[0] = 0;
    if (i < 0 || i >= g_nml) return;
    char sec[128] = "";
    int h = i;
    while (h >= 0 && !ml_heading(h)) h--;
    if (h >= 0 && (with_section || strcmp(g_ml[h], g_ml_section))) {
        tidy(g_ml[h], sec, sizeof sec);
        snprintf(g_ml_section, sizeof g_ml_section, "%s", g_ml[h]);
        if (g_ml[h + 1][0] == '(') {        /* "X-Ray Attack, uses three bars of super" */
            char note[96];
            snprintf(note, sizeof note, "%s", g_ml[h + 1] + 1);
            char *e = strrchr(note, ')'); if (e) *e = 0;
            if (note[0]) note[0] = (char)tolower((unsigned char)note[0]);
            size_t l = strlen(sec);
            snprintf(sec + l, sizeof sec - l, ", %s", note);
        }
    }
    if (ml_heading(i)) snprintf(msg, cap, "%s", sec);
    else {
        char spoken[300] = "";
        if (i + 1 < g_nml && g_ml[i + 1][0] == '[') ml_spoken(g_ml[i + 1], spoken, sizeof spoken);
        snprintf(msg, cap, "%s%s%s%s%s", sec, sec[0] ? ". " : "", g_ml[i], spoken[0] ? ": " : "", spoken);
        /* finishing moves: how close to stand ("Distance: jump") */
        if (h >= 0 && !_strnicmp(g_ml[h], "FINISHING", 9) && g_ml_rng[i][0]) {
            size_t l = strlen(msg);
            snprintf(msg + l, cap - l, ". Distance: %s", g_ml_rng[i]);
        }
    }
}

static void ml_row(int i, int with_section) {
    char msg[700];
    ml_row_text(i, with_section, msg, sizeof msg);
    say(msg, NULL);
}

/* ---------- Test Your Luck: the slot machine (Fight menu) ----------
 * The result is set before the spin: slot.textAnim.text<N>.conditionText.text per reel (1 = the
 * opponent, then the rule changes), each with arrowLeft / arrowRight On or Off = which fighter it is
 * for (left = player 1, you). Then "slot <N>reels", the intro, "BuildOn" (the Spin button is up),
 * slot.triggerButton "trigger" (spin pressed), slot.reels.reelAnim_<N> "stopScroll" one by one about
 * 2.5 s apart, "BuildOff". Each reel is said as it stops, with the game's description of the rule. */
static uint32_t g_slot_h;
static int g_slot_n, g_slot_prompted;
static char g_slot_arrow[8][2];          /* [reel][0 left, 1 right] = On */
static DWORD g_slot_t;

static int slot_desc(const char *name, char *out, size_t cap) {   /* "Blocking Disabled" -> "Cannot block." */
    for (int i = 0; i < g_nloc; i++) {
        if (_stricmp(g_loc[i].sec, "TowerMission") || _strnicmp(g_loc[i].key, "SLOT_MACHINE_", 13)) continue;
        const char *m = strstr(g_loc[i].key, "_MODIFIER_");
        if (!m || !isdigit((unsigned char)m[10]) || _stricmp(g_loc[i].val, name)) continue;
        char dk[96];
        snprintf(dk, sizeof dk, "%.*s_MODIFIER_DESCRIPTION_%s", (int)(m - g_loc[i].key), g_loc[i].key, m + 10);
        const char *d = loc_get("TowerIcons", dk);
        if (!d) d = loc_get("TowerMission", dk);
        if (d) { tidy(d, out, cap); return 1; }
    }
    return 0;
}

static int slot_event(FocusEv *e) {
    const char *t = e->target;
    if (!strcmp(t, "slot") && strstr(e->arg, "reels")) {
        g_slot_h = e->holder; g_slot_n = atoi(e->arg); g_slot_prompted = 0; g_slot_t = GetTickCount();
        return 1;
    }
    int n;
    /* the arrows are set in the same frame just before "slot <N>reels" */
    if (sscanf(t, "slot.textAnim.text%d.", &n) == 1 && n >= 1 && n <= 7 && strstr(t, "arrow")) {
        g_slot_arrow[n][strstr(t, "arrowRight") ? 1 : 0] = !_stricmp(e->arg, "On");
        return 1;
    }
    if (!g_slot_h || e->holder != g_slot_h || GetTickCount() - g_slot_t > 120000) return 0;
    if (!t[0] && !_stricmp(e->arg, "BuildOn") && !g_slot_prompted) {
        g_slot_prompted = 1;
        char m[160];
        snprintf(m, sizeof m, "Test Your Luck. Slot machine with %d reels: your opponent and %d rule changes. Press Select to spin.",
                 g_slot_n, g_slot_n - 1);
        say(m, NULL);
        return 1;
    }
    if (!strcmp(t, "slot.triggerButton") && !_stricmp(e->arg, "trigger")) { say("Spinning", NULL); return 1; }
    if (sscanf(t, "slot.reels.reelAnim_%d", &n) == 1 && !_stricmp(e->arg, "stopScroll") && n >= 1 && n <= 7) {
        char path[64], raw[128], name[128], d[256] = "", m[512];
        snprintf(path, sizeof path, "slot.textAnim.text%d.conditionText", n);
        if (!clip_text(e->holder, path, raw, sizeof raw)) return 1;
        tidy(raw, name, sizeof name);
        if (n == 1) snprintf(m, sizeof m, "Opponent: %s.", name);
        else {
            slot_desc(name, d, sizeof d);
            int l = g_slot_arrow[n][0], r = g_slot_arrow[n][1];
            const char *who = l && r ? "Both fighters" : l ? "You" : r ? "Your opponent" : "";
            size_t dl = strlen(d);
            while (dl && strchr(". ", d[dl - 1])) d[--dl] = 0;      /* "Cannot block." -> one full stop below */
            snprintf(m, sizeof m, "%s%s%s.%s%s%s", name, d[0] ? ": " : "", d, who[0] ? " " : "", who, who[0] ? "." : "");
        }
        speak_utf8(m, 0);                           /* queued: the next reel stops while this is said */
        if (n == g_slot_n) { g_slot_h = 0; }
        return 1;
    }
    if (!t[0] && !_stricmp(e->arg, "BuildOff")) { g_slot_h = 0; return 1; }
    return 0;
}

/* ---------- Options > Controller (Button Configuration) ----------
 * Panel btnConfig: header_.var_titletxt, presetSlider (value preset.var_GamepadPresetToggle, SetVariable), rows
 * btn0..btn7 (label.var_text "FRONT PUNCH", buttonAnim.var_valueBtn "[X]"), saveConfig. Select on the preset
 * starts editing: on each row the button pressed is given to that action (the action that had it takes the old
 * one), btnN.selectionFlash plays Trigger, all eight values are set again and focus moves to the next row. */
static char g_btn_tok[10][16];
static char g_btn_assign[300];
static DWORD g_btn_assign_t;
static unsigned g_btn_hint_seq;
static int btn_row(const char *t, const char **rest) {
    if (strncmp(t, "btnConfig.btn", 13) || !isdigit((unsigned char)t[13])) return -1;
    int n = t[13] - '0';
    if (isdigit((unsigned char)t[14])) return -1;
    *rest = t + 14;
    return n;
}
static void btn_said(int n, uint32_t h, char *out, size_t cap) {   /* "Front Punch, T, X button" */
    char p[64], lab[128] = "", la[128], tok[16] = "", key[160] = "";
    snprintf(p, sizeof p, "btnConfig.btn%d.label.var_text", n); clip_exact(h, p, lab, sizeof lab);
    snprintf(p, sizeof p, "btnConfig.btn%d.buttonAnim.var_valueBtn", n); clip_exact(h, p, tok, sizeof tok);
    tidy(lab, la, sizeof la);
    if (!keys_for_button(tok, key, sizeof key)) snprintf(key, sizeof key, "%s", tok);
    snprintf(out, cap, key[0] ? "%s, %s" : "%s", la, key);
    if (n >= 0 && n < 10) snprintf(g_btn_tok[n], sizeof g_btn_tok[n], "%s", tok);
}
static void btn_header(uint32_t h, char *title, size_t cap, unsigned *seq_out) {
    char t[128] = "";
    unsigned seq = clip_seq(h, "btnConfig.header_.var_titletxt", t, sizeof t);
    title[0] = 0;
    if (seq && seq != g_row_title_seq) { tidy(t, title, cap); g_row_title_seq = seq; }
    *seq_out = seq;
}
static void btn_say(const char *msg) {   /* after an assignment: queued behind it */
    if (g_btn_assign[0] && GetTickCount() - g_btn_assign_t < 500) say(g_btn_assign, msg);
    else say(msg, NULL);
    g_btn_assign[0] = 0;
}
static int btncfg_event(FocusEv *e) {
    const char *rest;
    int n = btn_row(e->target, &rest);
    char msg[600], title[128];
    unsigned seq;
    if (n >= 0 && !strcmp(rest, ".selectionFlash") && !_stricmp(e->arg, "Trigger")) {   /* a button given to row n */
        char old[16], a[300], b[300] = "";
        snprintf(old, sizeof old, "%s", g_btn_tok[n]);
        btn_said(n, e->holder, a, sizeof a);
        for (int m = 0; m < 8; m++) {
            char p[64], tok[16] = "";
            if (m == n) continue;
            snprintf(p, sizeof p, "btnConfig.btn%d.buttonAnim.var_valueBtn", m); clip_exact(e->holder, p, tok, sizeof tok);
            if (old[0] && strcmp(old, g_btn_tok[n]) && !strcmp(tok, old)) { btn_said(m, e->holder, b, sizeof b); break; }
        }
        snprintf(g_btn_assign, sizeof g_btn_assign, b[0] ? "%s. %s" : "%s", a, b);
        g_btn_assign_t = GetTickCount();
        return 1;
    }
    if (!focus_arg(e->arg)) return 0;
    if (n >= 0 && !*rest) {                                    /* a row in focus (editing) */
        char row[300];
        btn_said(n, e->holder, row, sizeof row);
        btn_header(e->holder, title, sizeof title, &seq);
        if (seq != g_btn_hint_seq) {                           /* editing just started: how it works, once */
            g_btn_hint_seq = seq;
            snprintf(msg, sizeof msg, "%s%sPress the key you want for each action. %s", title, title[0] ? ". " : "", row);
        } else snprintf(msg, sizeof msg, "%s", row);
        btn_say(msg);
        g_focus_h = e->holder; snprintf(g_focus_target, sizeof g_focus_target, "%s", e->target);
        g_focus_holder = e->holder; g_focus_said_t = GetTickCount();
        return 1;
    }
    if (!strcmp(e->target, "btnConfig.presetSlider")) {
        char lab[128] = "Controller preset", raw[128], val[32] = "";
        if (clip_exact(e->holder, "btnConfig.presetSlider.presetLabel.var_PRESET", raw, sizeof raw)) tidy(raw, lab, sizeof lab);
        clip_exact(e->holder, "btnConfig.presetSlider.preset.var_GamepadPresetToggle", val, sizeof val);
        btn_header(e->holder, title, sizeof title, &seq);
        if (title[0]) snprintf(msg, sizeof msg, "%s. %s, %s. Select edits it.", title, lab, val);
        else snprintf(msg, sizeof msg, "%s, %s", lab, val);
        say(msg, NULL);
        g_focus_h = e->holder; snprintf(g_focus_target, sizeof g_focus_target, "%s", e->target);
        g_focus_holder = e->holder; g_focus_said_t = GetTickCount();
        return 1;
    }
    if (!strcmp(e->target, "btnConfig.saveConfig")) {
        char raw[128] = "", s[128];
        clip_exact(e->holder, "btnConfig.saveConfig.text.var_text", raw, sizeof raw);
        tidy(raw[0] ? raw : "Save configurations", s, sizeof s);
        btn_say(s);
        g_focus_h = e->holder; snprintf(g_focus_target, sizeof g_focus_target, "%s", e->target);
        g_focus_holder = e->holder; g_focus_said_t = GetTickCount();
        return 1;
    }
    return 0;
}

/* Pause menu > Controller Preset (forum report 2026-11-24: not read). Select opens a number selector beside the item
 * (mc_option_N.SubToggle.PresetSelector, value by SetVariable optionText.var_valueTxt "1".."n"); its arrows build on
 * when it opens and off when it closes (Select keeps the value, Back puts the old one back first). A new value waits
 * 80 ms so the open/close announcement can take its place. */
static char g_preset_val[32];
static DWORD g_preset_t;
static int g_preset_open, g_preset_pending;
static int preset_event(FocusEv *e) {
    if (e->stop == 5 && ends_with(e->target, ".SubToggle.PresetSelector")) {
        snprintf(g_preset_val, sizeof g_preset_val, "%s", e->arg);
        g_preset_pending = g_preset_open; g_preset_t = GetTickCount();
        return 1;
    }
    if (ends_with(e->target, "PresetSelector.arrowLeft")) {
        char m[200];
        if (!_stricmp(e->arg, "BuildOn") && !g_preset_open) {
            g_preset_open = 1;
            snprintf(m, sizeof m, "Controller preset %s. Left and Right change it, Select keeps it, Back cancels.", g_preset_val);
            say(m, NULL);
        } else if (!_stricmp(e->arg, "BuildOff") && g_preset_open) {
            g_preset_open = 0;
            snprintf(m, sizeof m, "Controller preset %s", g_preset_val);
            say(m, NULL);
        }
        g_preset_pending = 0;
        return 1;
    }
    return ends_with(e->target, "PresetSelector.arrowRight") || ends_with(e->target, "PresetSelector");
}
static void preset_poll(void) {
    if (!g_preset_pending || GetTickCount() - g_preset_t < 80) return;
    g_preset_pending = 0;
    char m[48]; snprintf(m, sizeof m, "Preset %s", g_preset_val);
    say(m, NULL);
}

/* Main menu with 2 players or more: without a device for player 2 the game greys out Versus Mode, Team Ladder and
 * Online (navTextN.navTextBase gotoAndStop "disable") and the cursor skips them, so only Extras and Options seemed
 * left (forum 2026-11-25). Say why, once the switch has settled. */
static DWORD g_np_t;                    /* the player count just changed */
static int g_np_count;
static DWORD g_dis_t; static int g_dis_n;  /* the last burst of greyed-out items (they come before the count's lookup) */

static void on_focus_event(FocusEv *e) {
    if (g_debug) log_line("FOCUS %08x %s %s%s", e->holder, e->target, e->arg, e->stop ? " (stop)" : "");
    if (e->stop && !strcmp(e->target, "navTextBase") && !_stricmp(e->arg, "disable")) {
        if (GetTickCount() - g_dis_t > 400) g_dis_n = 0;
        g_dis_n++; g_dis_t = GetTickCount();
    }
    size_t tl0 = strlen(e->target);
    if (e->stop == 2) { ml_highlight(atoi(e->arg), e->t); return; }   /* said once the move's events are in */
    if (preset_event(e)) return;
    if (e->stop == 5) {                       /* an option row's new value (Options > Gameplay), after Left/Right */
        /* not the value set while the screen opens: the row's own announcement has it */
        if (e->holder == g_focus_h && !strcmp(e->target, g_focus_target) && GetTickCount() - g_input_t < 1000 &&
            GetTickCount() - g_focus_said_t > 300) {
            char v[64]; tidy(e->arg, v, sizeof v); say(v, NULL);
        }
        return;
    }
    if (slot_event(e)) return;
    /* challenge / minigame result (Challenge Tower, Test Your Might, Sight, Strike) */
    if (!strcmp(e->target, "practiceHUD") && (!_stricmp(e->arg, "Success") || !_stricmp(e->arg, "Failure"))) {
        fight_hold(); say(e->arg, NULL); mg_sight(4); return;   /* hold first: the result speaks past the fight silence */
    }
    if (!strcmp(e->target, "SightNavGrp")) {          /* Test Your Sight: Begin pressed / the pick starts */
        if (!_stricmp(e->arg, "BuildOff")) mg_sight(2);
        return;
    }
    if (!strcmp(e->target, "ladder_main") && !_strnicmp(e->arg, "stage", 5)) { g_lad_stage = atoi(e->arg + 5); return; }
    if (e->stop == 4) {                                          /* koin balance: resolved in menu_poll (wallet_poll) */
        int v = atoi(e->arg);
        if (!g_wallet_pending) { g_wallet_base = g_wallet_last >= 0 ? g_wallet_last : v; g_wallet_pending = 1; }
        g_wallet_val = v; g_wallet_t = GetTickCount();
        g_krypt_balance = v;
        if (g_krypt_prize) g_krypt_prize = 0;                    /* closing a prize view shows the new balance */
        return;
    }
    if (!e->target[0] && !_stricmp(e->arg, "On_2LockedIn")) {   /* Krypt: the camera stopped at a koffin */
        char raw[96] = "", nm[128];
        uint32_t par = hold_parent(e->holder);
        if (par && clip_exact(par, "navAnim.purchaseInfo.label", raw, sizeof raw) && raw[0]) {
            krypt_label(raw, nm, sizeof nm);
            say(nm, NULL);
            snprintf(g_krypt_said, sizeof g_krypt_said, "%s", nm); g_krypt_said_t = GetTickCount();
            g_krypt_lock_t = GetTickCount();
            g_krypt_locked = 1; g_krypt_price = -1; g_krypt_price_said = 0; g_krypt_active_t = GetTickCount();
            memset(g_knb, 0, sizeof g_knb);
        }
        return;
    }
    if (!e->target[0] && !_stricmp(e->arg, "LockedIn_2On")) { g_krypt_locked = 0; g_krypt_ping_t = 0; return; }   /* moving on */
    if (!_stricmp(e->target, "mapAnim") && !_strnicmp(e->arg, "BuildOn_Zone", 12)) { g_krypt_locked = 0; g_krypt_map = 1; g_krypt_active_t = GetTickCount(); }
    if (!_stricmp(e->target, "mapAnim") && !_strnicmp(e->arg, "BuildOff_Zone", 13)) g_krypt_map = 0;
    if (!_stricmp(e->target, "UnlockAnim") && !_stricmp(e->arg, "BuildOn")) { g_krypt_prize = 1; g_krypt_locked = 0; }
    if (!e->target[0] && !_stricmp(e->arg, "BuildOn") && g_krypt_lock_t && GetTickCount() - g_krypt_lock_t < 4000) {
        char hp[32] = "";
        if (hold_path(e->holder, hp, sizeof hp) && !strncmp(hp, "koin", 4) && hp[4] >= '0' && hp[4] <= '4' && !hp[5]) {
            int k = hp[4] - '0';
            char path[32], price[32] = "", m[64];
            uint32_t par = hold_parent(e->holder);
            snprintf(path, sizeof path, "koin%d.price.amount", k);
            if (!par || !clip_exact(par, path, price, sizeof price) || !price[0]) return;
            if (k == 0) {                                  /* the price of the koffin locked on */
                if (!g_krypt_price_said) { snprintf(m, sizeof m, "%s koins", price); speak_utf8(m, 0); g_krypt_price_said = 1; }
                g_krypt_price = atoi(price);
                g_krypt_ping_t = GetTickCount() + 900;     /* the other koffins' pings follow the price */
            } else {
                float x = 640, sc = 0;
                krypt_marker_get(e->holder, &x, &sc);
                g_knb[k - 1].valid = 1; g_knb[k - 1].price = atoi(price); g_knb[k - 1].x = x; g_knb[k - 1].scale = sc;
            }
            return;
        }
    }
    if (e->stop == 3) {                                          /* card art (Challenge Tower) */
        if (strstr(e->target, "challengeInfo") || !strncmp(e->target, "playerModule", 12)) { g_tower_evt_t = GetTickCount(); g_tower_session = 1; mg_reset(); tower_image(e); }
        return;
    }
    if (e->stop) {
        if (tl0 > 7 && !strcmp(e->target + tl0 - 7, ".slider")) {
            char item[96]; memcpy(item, e->target, tl0 - 7); item[tl0 - 7] = 0;
            int v = atoi(e->arg);
            int known = val_slot(e->holder, item, 0) != NULL;
            int *slot = val_slot(e->holder, item, 1);
            int changed = known && *slot != v;
            *slot = v;
            int focused = e->holder == g_focus_h && !strcmp(item, g_focus_target);
            if (focused && !known && GetTickCount() - g_said_t < 500) {
                /* the screen just opened on this item: its label and first value together */
                char m[600]; snprintf(m, sizeof m, "%s, %d", g_focus_label, v); say(m, NULL);
            } else if (changed && focused && GetTickCount() - g_input_t < 1000) {
                /* a change of the item in focus right after the player's input: the new value */
                char n[16]; snprintf(n, sizeof n, "%d", v); say(n, NULL);
            }
        }
        return;
    }
    const char *h = strstr(e->target, "Highlight");
    if (h && !_stricmp(e->arg, "GainFocus")) {               /* character select grid */
        g_grid_cell = atoi(h + 9) - 1; g_grid_t = GetTickCount();   /* Highlight1 = top left */
        return;
    }
    if (!_stricmp(e->arg, "ActiveToDisable") || !_stricmp(e->arg, "Activate")) g_focus_holder = 0;
    /* Krypt map (fast travel): mapAnim.map gotoAndPlay "Zone_5_BuildOn" on the zone picked;
     * its info.info1/2/3.text = name, "1 / 121" koffins opened, "0.83%". */
    if (!e->target[0] && !_strnicmp(e->arg, "Zone_", 5) && strstr(e->arg, "BuildOn")) {
        char nm[128] = "", cnt[32] = "", pc[32] = "", name[128], msg[300];
        clip_exact(e->holder, "info.info1.text", nm, sizeof nm);
        clip_exact(e->holder, "info.info2.text", cnt, sizeof cnt);
        clip_exact(e->holder, "info.info3.text", pc, sizeof pc);
        tidy(nm, name, sizeof name);
        if (!name[0]) return;
        int got = 0, all = 0;
        if (sscanf(cnt, "%d / %d", &got, &all) == 2 && all > 0) {
            char *pct = strchr(pc, '%'); if (pct) *pct = 0;
            char *dot = strchr(pc, '.');
            if (dot) { size_t l = strlen(pc); while (l > (size_t)(dot - pc) && (pc[l - 1] == '0' || pc[l - 1] == '.')) pc[--l] = 0; }
            snprintf(msg, sizeof msg, "%s. %d of %d koffins opened%s%s%s", name, got, all,
                     pc[0] ? ", " : "", pc, pc[0] ? " percent" : "");
        } else snprintf(msg, sizeof msg, "%s", name);
        say(msg, NULL);
        snprintf(g_focus_label, sizeof g_focus_label, "%s", name);   /* the zone name's own lookup follows: said already */
        return;
    }
    /* Challenge Tower: containers.containerN.challengeInfo and its lockStatus child */
    if (strstr(e->target, "challengeInfo")) {
        char pfx[96]; snprintf(pfx, sizeof pfx, "%s", e->target);
        char *ci = strstr(pfx, "challengeInfo");
        ci[sizeof "challengeInfo" - 1] = 0;                        /* keep up to and including challengeInfo */
        size_t tl = strlen(e->target);
        g_tower_evt_t = GetTickCount(); g_tower_session = 1;
        if (tl > 11 && !strcmp(e->target + tl - 11, ".lockStatus")) {
            snprintf(tcard(pfx)->lock, sizeof g_tc[0].lock, "%s", e->arg);
            return;
        }
        if (tl > 10 && !strcmp(e->target + tl - 10, ".opponents")) {   /* "Chars2": two opponents shown */
            if (!_strnicmp(e->arg, "Chars", 5)) tcard(pfx)->nopp = atoi(e->arg + 5);
            return;
        }
        char path[160], msg[500];
        if (!_stricmp(e->arg, "focusOn")) {                        /* the card now at the nameplate slot */
            /* the open and the fast scroll pass every card through this slot, so hold
             * the latest and speak it once the scrolling settles (menu_poll). */
            snprintf(g_tower_pfx, sizeof g_tower_pfx, "%s", pfx);
            g_tower_holder = e->holder;
            g_tower_t = GetTickCount();
            return;
        }
        if (!_stricmp(e->arg, "InfoOn")) {                         /* the card flipped to show the objective */
            char oraw[400], tmp[400], obj[400] = "", sk[32] = "";
            snprintf(path, sizeof path, "%s.infoText.text", pfx);
            if (clip_exact(e->holder, path, oraw, sizeof oraw)) {
                size_t o = 0;                                      /* drop the [-] / [+] bullet markers */
                for (size_t i = 0; oraw[i] && o < sizeof tmp - 1; i++) {
                    if (oraw[i] == '[' && (oraw[i + 1] == '-' || oraw[i + 1] == '+') && oraw[i + 2] == ']') {
                        i += 2; if (o && tmp[o - 1] != ' ') tmp[o++] = ' '; continue;
                    }
                    tmp[o++] = oraw[i];
                }
                tmp[o] = 0; tidy(tmp, obj, sizeof obj);
                size_t ol = strlen(obj);                           /* avoid "seconds.. Skip cost" */
                while (ol && (obj[ol - 1] == '.' || obj[ol - 1] == ' ')) obj[--ol] = 0;
            }
            snprintf(path, sizeof path, "%s.skipAnim.skipCost.text", pfx); clip_exact(e->holder, path, sk, sizeof sk);
            if (obj[0]) {
                if (sk[0]) snprintf(msg, sizeof msg, "%s. Skip cost %s koins", obj, sk);
                else snprintf(msg, sizeof msg, "%s", obj);
                say(msg, NULL);
            }
            return;
        }
        return;                                                    /* other challengeInfo events: ignore */
    }
    if (!strncmp(e->target, "btnConfig.", 10) && btncfg_event(e)) return;
    /* Option rows (Practice Options, AI Options): the row clip gains focus with
     * an empty sub-path (Invoke "gotoAndPlay" HighlightOn on the row itself). The
     * label and value are children set when the panel opened:
     * optionLabel.label ("DAMAGE INFO:") and optionText.valueTxt ("ON").
     * Options > Gameplay: the row is a sub-path of the panel ("gameplay.option2" HighlightOn),
     * label gameplay.option2.optionLabel.varLabel (SetText), value
     * gameplay.option2.optionText.var_valueTxt (SetVariable, h_setvar). */
    if (focus_arg(e->arg)) {
        char lab[256], val[128], la[256], va[128], msg[700], lp[128], vp[128];
        if (e->target[0]) {
            snprintf(lp, sizeof lp, "%s.optionLabel", e->target);
            snprintf(vp, sizeof vp, "%s.optionText", e->target);
        } else {
            snprintf(lp, sizeof lp, "optionLabel.label");
            snprintf(vp, sizeof vp, "optionText.valueTxt");
        }
        if (clip_text(e->holder, lp, lab, sizeof lab)) {
            tidy(lab, la, sizeof la);
            size_t ll = strlen(la);                                   /* drop the label's trailing colon */
            while (ll && (la[ll - 1] == ':' || la[ll - 1] == ' ')) la[--ll] = 0;
            char title[256] = "";
            if (e->target[0]) {                                       /* panel just opened: its header first */
                char hp[128], t[256] = "";
                const char *dot = strchr(e->target, '.');
                int n = dot ? (int)(dot - e->target) : (int)strlen(e->target);
                snprintf(hp, sizeof hp, "%.*s.header.var_titletxt", n, e->target);
                unsigned seq = clip_seq(e->holder, hp, t, sizeof t);
                if (seq && seq != g_row_title_seq) { tidy(t, title, sizeof title); g_row_title_seq = seq; }
            }
            if (clip_text(e->holder, vp, val, sizeof val)) {
                tidy(val, va, sizeof va);
                snprintf(msg, sizeof msg, title[0] ? "%s. %s, %s" : "%s%s, %s", title, la, va);
            } else snprintf(msg, sizeof msg, title[0] ? "%s. %s" : "%s%s", title, la);
            say(msg, NULL);
            g_focus_h = e->holder; snprintf(g_focus_target, sizeof g_focus_target, "%s", e->target);
            g_focus_said_t = GetTickCount();
            snprintf(g_focus_label, sizeof g_focus_label, "%s", la);
            return;
        }
    }
    int gain = !strcmp(e->arg, "FocusGain");
    if (!gain && (!focus_arg(e->arg) || !e->target[0])) return;
    /* the button prompt bar ("ps.nav", "nav") lights up too: not a focus */
    size_t tl = strlen(e->target);
    if (!_strnicmp(e->target, "nav", 3) || (tl >= 4 && !_stricmp(e->target + tl - 4, ".nav"))) return;
    /* In a challenge the banner shows the title alone; the [TowerMission] lookups right
     * after give title + objective, which the tower reader says. Saying the banner too
     * was a cut-off "Training: Special Tech" 50 ms before the full line. */
    if (g_tower_session && (!_strnicmp(e->target, "UpperCenterTextAnim", 19) || !_strnicmp(e->target, "LeftTextAnim", 12) ||
                            !_strnicmp(e->target, "RightTextAnim", 13))) return;   /* the dialogue box: name + line follow */
    char raw[256], a[512], title[256] = "", msg[800];
    if (!clip_text(e->holder, e->target, raw, sizeof raw)) return;
    tidy(raw, a, sizeof a);
    /* the list: the holder itself (option3 inside it), or for a focused item clip its parent */
    uint32_t list = gain ? hold_parent(e->holder) : e->holder;
    if (list && list != g_focus_holder) {                    /* focus entered this list: its title first */
        char t[256];
        if (clip_title(list, t, sizeof t)) tidy(t, title, sizeof title);
        g_focus_holder = list;
    }
    g_focus_h = e->holder; snprintf(g_focus_target, sizeof g_focus_target, "%s", e->target);
    int *v = val_slot(e->holder, e->target, 0);
    char val[16] = "";
    if (v) snprintf(val, sizeof val, ", %d", *v);
    snprintf(msg, sizeof msg, title[0] ? "%s. %s%s" : "%s%s%s", title, a, val);
    say(msg, NULL);
    snprintf(g_focus_label, sizeof g_focus_label, "%s", a);
}

static void on_group(LocCall *g, int n) {
    /* a fight or challenge loading: the cues may come back (fight.c holds them after a challenge result) */
    for (int i = 0; i < n; i++) if (!_stricmp(g[i].key, "LOADING")) { fight_resume(); break; }
    DWORD now = GetTickCount();
    uint32_t first_label = 0, focus_label = 0;
    int nlabels = 0, focus = -1, picker = 0;
    for (int i = 0; i < n; i++) {
        if (g_debug) log_line("LOC [%s] %s (%08x) -> %s", g[i].sec, g[i].key, g[i].keyaddr, g[i].val);
        cache_put(g[i].sec, g[i].key, g[i].val);
        uint32_t other, rec;
        int kind = find_record(g[i].keyaddr, &other, &rec);
        if (kind == 1) { nlabels++; if (!first_label) first_label = g[i].keyaddr; }
        else if (kind == 2) { focus = i; focus_label = other; }
        if (!_stricmp(g[i].sec, "PlayerSelect") && !_strnicmp(g[i].key, "Nav", 3)) picker = 1;
    }
    /* the move list: keep its rows (from the first [movelist] lookup on) */
    int mlfirst = -1, mlcount = 0;
    for (int i = 0; i < n; i++) if (!strcmp(g[i].sec, "movelist")) { if (mlfirst < 0) mlfirst = i; mlcount++; }
    /* Fatality Tutorial: a fatality name shown on its own -> speak its name and input.
     * Skip when the move list is open: it lists fatality names in Finishing Moves, and
     * the move-list reader handles those (otherwise this hijacks them in state 11). */
    if (mlcount < 4)
        for (int i = 0; i < n; i++) if (fatality_try(g[i].val)) return;
    if (mlcount >= 4) {
        g_nml = 0;
        for (int i = mlfirst; i < n && g_nml < MLR; i++)
            if (!strcmp(g[i].sec, "movelist")) snprintf(g_ml[g_nml++], sizeof g_ml[0], "%s", g[i].val);
        char who[96] = "", title[160];
        for (int i = 0; i < mlfirst; i++) if (!_stricmp(g[i].sec, "PlayerSelect")) tidy(g[i].val, who, sizeof who);
        if (who[0]) snprintf(g_ml_who, sizeof g_ml_who, "%s", who);   /* the super moves page omits the name */
        else snprintf(who, sizeof who, "%s", g_ml_who);
        const char *kind = "move list";                              /* the specials page opens on "SPECIAL ATTACKS" */
        for (int i = 0; i < g_nml; i++)
            if (ml_heading(i)) { if (!_strnicmp(g_ml[i], "SPECIAL", 7)) kind = "super moves"; break; }
        snprintf(title, sizeof title, "%s%s%s", who, who[0] ? " " : "", kind);
        say(title, NULL);
        g_ml_section[0] = 0;
        g_ml_pending = -1;
        g_said_t = 0;                        /* the first row follows the title */
        /* Which page and where: the game returns to a page's place on a page switch
         * (scroll bar not at the top); opened from the pause menu it is at the top. */
        char key[128], first_head[96] = "";
        for (int i = 0; i < g_nml; i++) if (ml_heading(i)) { snprintf(first_head, sizeof first_head, "%s", g_ml[i]); break; }
        snprintf(key, sizeof key, "%s|%s", who, first_head);
        g_ml_slot = ml_mem_slot(key);
        g_ml_due = 0;
        int row = ml_row_at_scroll();       /* the top when opened; the kept place after J */
        if (row < 0) { row = 0; while (row < g_nml && !ml_selectable(row)) row++; }
        if (row < g_nml) {
            char m[700];
            ml_row_text(row, 1, m, sizeof m);
            speak_utf8(m, 0);
        }
        return;
    }
    /* Krypt: leaving it (the main menu is built) ends Krypt mode; prize names after a
     * purchase ([Krypt] SHORT<n>), the action at a koffin (Claim Prize, Enter), the welcome. */
    for (int i = 0; i < n; i++) if (!_stricmp(g[i].sec, "MainMenu")) { g_krypt_active_t = 0; g_krypt_locked = 0; break; }
    /* the main menu list itself (not [MainMenu] LOADING, which every challenge load uses) */
    for (int i = 0; i < n; i++) if (!_stricmp(g[i].sec, "MainMenu") && !_stricmp(g[i].key, "ChallengeTower")) { g_tower_session = 0; mg_reset(); break; }
    {   /* main menu header (Up from the first item): the 1 Player / 2 Players switch, Left/Right change it.
         * Focus = a lone [MainMenu] NumPLayersToggleDesc; a switch = NumPlayersN + the rebuilt menu. */
        static char players[24] = "1 player";
        int np = -1, desc = -1;
        for (int i = 0; i < n; i++) {
            if (_stricmp(g[i].sec, "MainMenu")) continue;
            if (!_strnicmp(g[i].key, "NumPlayers", 10) && isdigit((unsigned char)g[i].key[10])) np = i;
            else if (!_stricmp(g[i].key, "NumPLayersToggleDesc")) desc = i;
        }
        if (np >= 0) {
            char d = g[np].key[10];
            if (d == '1') snprintf(players, sizeof players, "1 player"); else snprintf(players, sizeof players, "%c players", d);
            g_np_t = now; g_np_count = d - '0';
        }
        if (np >= 0 && desc >= 0) { say(players, NULL); return; }
        if (np < 0 && desc >= 0 && n <= 2) { say(players, "Change the number of players with Left and Right."); return; }
    }
    if (g_krypt_active_t) {
        char text[1200] = ""; int welcome = 0;
        for (int i = 0; i < n; i++) {
            if (_stricmp(g[i].sec, "Krypt")) continue;
            char t[700];
            if (!_strnicmp(g[i].key, "SHORT", 5) && isdigit((unsigned char)g[i].key[5]) && GetTickCount() - g_krypt_select_t < 15000) {
                tidy(g[i].val, t, sizeof t);
                snprintf(text, sizeof text, "Unlocked: %s. Select closes it.", t);
            } else if (!_stricmp(g[i].key, "NavClaim")) speak_utf8("Claim prize", 0);
            else if (!_stricmp(g[i].key, "NavEnter")) speak_utf8("Enter", 0);
            else if (!_strnicmp(g[i].key, "Welcome", 7)) {
                tidy(g[i].val, t, sizeof t);
                if (text[0]) strncat(text, " ", sizeof text - strlen(text) - 1);
                strncat(text, t, sizeof text - strlen(text) - 1);
                welcome = 1;
            }
        }
        if (welcome) strncat(text, " Close.", sizeof text - strlen(text) - 1);
        if (text[0]) { say(text, NULL); return; }
    }
    /* Test Your Luck: the slot result ([TowerMission] <NAME>_NAME + SLOT_MACHINE_<kind>_MODIFIER_<n>) is looked
     * up as the slot machine appears, before the spin: not said here (that spoiled the spin); each reel is
     * said as it stops (slot_event). Other slot lines during the fight ("25 Health Restored") are said. */
    {
        int slot = 0, mods = 0;
        char other[400] = "";
        for (int i = 0; i < n; i++) {
            if (_stricmp(g[i].sec, "TowerMission") || _strnicmp(g[i].key, "SLOT_MACHINE_", 13)) continue;
            slot++;
            const char *m = strstr(g[i].key, "_MODIFIER_");
            if (m && isdigit((unsigned char)m[10])) { mods++; continue; }
            if (strstr(g[i].key, "DESCRIPTION") || !_stricmp(g[i].key, "SLOT_MACHINE_TITLE")) continue;
            char t[300]; tidy(g[i].val, t, sizeof t);
            if (t[0] && !strstr(other, t)) { size_t l = strlen(other); snprintf(other + l, sizeof other - l, "%s%s", l ? " " : "", t); }
        }
        if (slot) { if (!mods && other[0]) say(other, NULL); return; }
    }
    /* Ladder screen between fights: [PlayerSelect] the player then the ten opponents in order, [Ladder]
     * DifficultyLevel / RoundsLost / MatchesLost (values from SetText), "ladder_main" stage<N> = fight N. */
    {
        int lad = 0;
        for (int i = 0; i < n; i++) if (!_stricmp(g[i].sec, "Ladder") && !_stricmp(g[i].key, "RoundsLost")) lad = 1;
        if (lad) {
            char names[16][64]; int nn = 0;
            for (int i = 0; i < n && nn < 16; i++) if (!_stricmp(g[i].sec, "PlayerSelect")) tidy(g[i].val, names[nn++], sizeof names[0]);
            char m[400];
            int tot = nn - 1, st = g_lad_stage ? g_lad_stage : 1;   /* before fight 1 the stage comes after the texts */
            g_lad_stage = 0;
            if (tot >= 1 && st >= 1 && st <= tot) snprintf(m, sizeof m, "Ladder, fight %d of %d: %s.", st, tot, names[st]);
            else snprintf(m, sizeof m, "Ladder.");
            if (g_lad_rounds[0] || g_lad_matches[0]) {
                size_t l = strlen(m);
                snprintf(m + l, sizeof m - l, " Rounds lost %s, matches lost %s.", g_lad_rounds[0] ? g_lad_rounds : "0", g_lad_matches[0] ? g_lad_matches : "0");
            }
            say(m, NULL);
            g_lad_rounds[0] = g_lad_matches[0] = 0;
            return;
        }
    }
    /* Test Your Might / Strike: the material to break ([TestYourMight] MaterialPine = "Pine Boards"),
     * looked up as the meter appears. Test Your Sight: the Begin prompt arms the head tracking. */
    for (int i = 0; i < n; i++) {
        if ((!_stricmp(g[i].sec, "TestYourMight") || !_stricmp(g[i].sec, "TestYourStrike")) && !_strnicmp(g[i].key, "Material", 8)) {
            char t[128], m[200]; tidy(g[i].val, t, sizeof t);
            snprintf(m, sizeof m, "Break: %s", t); int was = speech_force(1); say(m, NULL); speech_force(was); return;
        }
        if (!_stricmp(g[i].sec, "TowerMission") && !_stricmp(g[i].key, "TEST_YOUR_SIGHT_NAV_BEGIN")) mg_sight(1);
        if (!_stricmp(g[i].sec, "TowerMission") && !_stricmp(g[i].key, "TEST_YOUR_SIGHT_NAV_SELECT")) mg_sight(3);
        if (n == 1 && !_stricmp(g[i].sec, "TowerMission") && !_stricmp(g[i].key, "CHALLENGE_NAME")) return;   /* "Challenge": the result says it */
    }
    /* Fatality Tutorial intro card (practiceHUD.messageBox): [TutorialMissions] <NAME> + <NAME>_MSG (the tagline),
     * then a second lookup of the two start choices, FATALITY_LEFT_LABEL on [X] and FATALITY_RIGHT_LABEL on [A]
     * (seen 2026-11-25 with menudebug). Was silent: the player had to guess which key starts it. */
    {
        static char tag[300]; static DWORD tag_t;
        int left = -1, right = -1;
        for (int i = 0; i < n; i++) {
            if (_stricmp(g[i].sec, "TutorialMissions")) continue;
            if (ends_with(g[i].key, "_MSG")) {
                char nm[64] = "", t[240];
                size_t kl = strlen(g[i].key) - 4;
                for (int j = 0; j < n; j++)       /* the name: the lookup keyed <NAME> */
                    if (!_stricmp(g[j].sec, "TutorialMissions") && strlen(g[j].key) == kl &&
                        !_strnicmp(g[j].key, g[i].key, kl)) tidy(g[j].val, nm, sizeof nm);
                for (char *p = nm, prev = ' '; *p; prev = *p++)   /* "SUB-ZERO" -> "Sub-Zero" */
                    *p = (char)(isalpha((unsigned char)prev) ? tolower((unsigned char)*p) : toupper((unsigned char)*p));
                tidy(g[i].val, t, sizeof t);
                snprintf(tag, sizeof tag, "%s%s%s", nm, nm[0] ? ". " : "", t);
                tag_t = now;
            }
            if (!_stricmp(g[i].key, "FATALITY_LEFT_LABEL")) left = i;
            if (!_stricmp(g[i].key, "FATALITY_RIGHT_LABEL")) right = i;
        }
        if (left >= 0 && right >= 0) {
            char kl[96], kr[96], l[96], r[96], msg[700];
            if (!keys_for_button("[X]", kl, sizeof kl)) snprintf(kl, sizeof kl, "X button");
            if (!keys_for_button("[A]", kr, sizeof kr)) snprintf(kr, sizeof kr, "A button");
            tidy(g[left].val, l, sizeof l); tidy(g[right].val, r, sizeof r);
            int fresh = now - tag_t < 5000 && tag[0];
            snprintf(msg, sizeof msg, "%s%s%s: %s. %s: %s.", fresh ? tag : "", fresh ? " " : "", l, kl, r, kr);
            int was = speech_force(1); say(msg, NULL); speech_force(was);   /* a fight is loaded behind the card */
            return;
        }
    }
    /* In a challenge (not the tower menu): [TowerMission] texts are the challenge's own
     * announcements - the intro (title + objective, *_INITIAL_DETAILS), narrator lines
     * (<NAME> + line), the move to land in training ([TowerMission] move name +
     * [TutorialMissions] its input), the rules on the pause screen - and [TowerIcons]
     * the condition badges ("Blocking Disabled"). They come seconds after the player's
     * last key, so the lone-lookup rule below would drop them. In the tower menu card
     * titles come in batches of ~6 *_TITLE lookups and the card reader speaks them. */
    {
        int tm = 0, titles = 0, icons = 0, tut = 0;
        for (int i = 0; i < n; i++) {
            if (!_stricmp(g[i].sec, "TowerMission")) { tm++; if (ends_with(g[i].key, "_TITLE")) titles++; }
            else if (!_stricmp(g[i].sec, "TowerIcons")) icons++;
            else if (!_stricmp(g[i].sec, "TutorialMissions")) tut++;
        }
        int in_menu = now - g_tower_evt_t < 3000 || titles >= 3;
        /* condition icons ([TowerIcons] alone, "Ground Pound") come half a second after the loading screen's
         * objective, while the card re-read during loading still makes it look like the tower menu */
        if (icons && !tm) in_menu = 0;
        if ((tm || icons) && !g_tower_session && !in_menu) return;   /* not in the Challenge Tower: not ours to say */
        if ((tm || icons) && !in_menu) {
            char text[1400] = "", inp[300] = "";
            for (int i = 0; i < n; i++) {
                char t[700] = "";
                if (!_stricmp(g[i].sec, "TutorialMissions")) {
                    if (g[i].val[0] == '[' && !inp[0]) ml_spoken(g[i].val, inp, sizeof inp);
                    continue;
                }
                if (_stricmp(g[i].sec, "TowerMission") && _stricmp(g[i].sec, "TowerIcons")) continue;
                tidy(g[i].val, t, sizeof t);
                if (!t[0] || strstr(text, t)) continue;
                if (text[0]) {
                    size_t l = strlen(text);
                    strncat(text, strchr(".!?", text[l - 1]) ? " " : ". ", sizeof text - l - 1);
                }
                strncat(text, t, sizeof text - strlen(text) - 1);
            }
            if (inp[0] && text[0]) { strncat(text, ": ", sizeof text - strlen(text) - 1); strncat(text, inp, sizeof text - strlen(text) - 1); }
            static char last[1400]; static DWORD last_t, busy_until;
            if (text[0] && !(!strcmp(text, last) && now - last_t < 8000)) {
                snprintf(last, sizeof last, "%s", text);
                /* The loading screen's objective and conditions, then the narrator lines, come seconds apart
                 * on their own: each interrupting the last cut long conditions off (user, 2026-11-20). Queue
                 * behind a message that is probably still being spoken (~55 ms a character), unless the
                 * player pressed something since (Continue on a narrator line = skip to the next). */
                int queue = (LONG)(busy_until - now) > 0 && g_input_t < last_t;
                /* a challenge's objective and the move to land are needed mid-fight: they get past the fight silence */
                int was = speech_force(1);
                if (queue) { char fx[1400]; fix_dots(text, fx, sizeof fx); speak_utf8(fx, 0); } else say(text, NULL);
                speech_force(was);
                last_t = now;
                busy_until = (queue && (LONG)(busy_until - now) > 0 ? busy_until : now) + (DWORD)(strlen(text) * 55);
                g_focus_holder = 0;
            }
            return;
        }
        if (!in_menu && tut && tut == n) return;        /* the training move display refreshing its input */
    }
    if (focus >= 0) {
        char labkey[96], a[512], b[1024], title[512] = "", msg[1100];
        gstr(focus_label, labkey, sizeof labkey);
        if (!_stricmp(g[focus].sec, "MainMenu")) mainmenu_focus(labkey);
        const char *label = loc_get(g[focus].sec, labkey);
        if (!label) { if (g_debug) log_line("menu: no text for [%s] %s", g[focus].sec, labkey); return; }
        tidy(label, a, sizeof a); tidy(g[focus].val, b, sizeof b);
        if (nlabels >= 2) {                  /* a list was built: a new menu, or back to an open one */
            int at = -1;
            for (int s = 0; s < g_nstack; s++) if (g_stack[s].list == first_label) at = s;
            char firstkey[32]; gstr(first_label, firstkey, sizeof firstkey);
            if (at < 0 && !_stricmp(g[focus].sec, "MainMenu") && !_stricmp(firstkey, "Fight")) {
                /* the main menu (its list starts with Fight), however we got here */
                g_nstack = 0;
            }
            if (at >= 0) g_nstack = at + 1;
            else {
                if (!g_nstack && !_stricmp(g[focus].sec, "MainMenu") && !_stricmp(firstkey, "Fight")) snprintf(title, sizeof title, "Main menu");
                else snprintf(title, sizeof title, "%s", g_focus_label);
                if (g_nstack == STACK) { memmove(g_stack, g_stack + 1, sizeof g_stack[0] * (STACK - 1)); g_nstack--; }
                g_stack[g_nstack].list = first_label;
                snprintf(g_stack[g_nstack].title, sizeof g_stack[0].title, "%s", title);
                g_nstack++;
            }
        }
        snprintf(msg, sizeof msg, title[0] ? "%s. %s" : "%s%s", title, a);
        say(msg, b);
        snprintf(g_focus_label, sizeof g_focus_label, "%s", a);
        return;
    }
    /* end of a round: [PlayerSelect] <name> then [Gameplay.Fighting] Wins (" WINS!") */
    for (int i = 0; i < n; i++) {
        if (_stricmp(g[i].sec, "Gameplay.Fighting") || _stricmp(g[i].key, "Wins")) continue;
        char who[96] = "", m[160];
        /* a match between players names the player who won (forum request 2026-11-23), not the character */
        if (!fight_winner_name(who, sizeof who))
            for (int j = 0; j < n; j++) if (!_stricmp(g[j].sec, "PlayerSelect")) tidy(g[j].val, who, sizeof who);
        snprintf(m, sizeof m, "%s%swins", who, who[0] ? " " : "");
        fight_round_over();
        say(m, NULL);
        return;
    }
    /* a message box: its body, then the choices ("Yes or No") */
    int body = -1;
    for (int i = 0; i < n; i++) {  /* a sentence, not a button label like [MsgBox] CancelUC or "QUIT TO THE MAIN MENU" */
        int lower = 0;
        for (const char *c = g[i].val; *c && !lower; c++) lower = islower((unsigned char)*c);
        if ((strstr(g[i].sec, "Msg") || ends_with(g[i].key, "Body") || ends_with(g[i].key, "Message")) && strlen(g[i].val) >= 20 && lower)
            body = i;
    }
    if (body >= 0 && sysdialog_recent()) return;   /* a system dialog: sysdialog.c reads it with its choices */
    if (body >= 0) {
        char b[1024], choices[256] = "";
        g_krypt_locked = 0;                  /* a question is up: Select answers it */
        tidy(g[body].val, b, sizeof b);
        /* a body built from pieces around a number ([MsgBox] ConfirmSkipMissionStart "The cost to skip is",
         * 150, ...End "koins.
Are you sure ..."): the text field got the whole sentence just before */
        if (GetTickCount() - g_long_settext_t < 500) {
            char st[600]; tidy(g_long_settext, st, sizeof st);
            char head[24]; snprintf(head, sizeof head, "%.20s", b);
            if (strlen(st) > strlen(b) && strstr(st, head)) snprintf(b, sizeof b, "%s", st);
        }
        for (int i = 0; i < n; i++) {
            if (i == body || _stricmp(g[i].sec, "Options")) continue;
            if (!_strnicmp(g[i].key, "KOTH", 4)) continue;   /* Button Configuration's King of the Hill legend */
            char t[64]; tidy(g[i].val, t, sizeof t);
            if (!t[0] || strlen(t) > 20) continue;
            /* a legend redrawn with the dialog (King of the Hill's "Invite Friends", 8 times) is not a choice */
            int reps = 0;
            for (int j = 0; j < n; j++) if (!_stricmp(g[j].sec, "Options") && !strcmp(g[j].val, g[i].val)) reps++;
            if (reps > 1) continue;
            if (choices[0]) strncat(choices, lang_or(), sizeof choices - strlen(choices) - 1);   /* "Sí o No" */
            strncat(choices, t, sizeof choices - strlen(choices) - 1);
        }
        say(b, choices);
        g_focus_holder = 0;
        return;
    }
    if (n >= 3) {                            /* a screen was built */
        if (picker && now - g_screen_said_t > 3000) {
            /* the fighter under the cursor: a lower-case name key ("scorpion") of [PlayerSelect] */
            char first[128] = "", msg[200];
            for (int i = 0; i < n; i++)
                if (!_stricmp(g[i].sec, "PlayerSelect") && islower((unsigned char)g[i].key[0]) && g[i].val[0])
                    tidy(g[i].val, first, sizeof first);
            snprintf(msg, sizeof msg, first[0] ? "Choose your fighter. %s" : "Choose your fighter", first);
            say(msg, NULL);
            if (first[0]) { snprintf(g_fighter_said, sizeof g_fighter_said, "%s", first); g_fighter_said_t = now; }
            g_screen_said_t = now;
            g_nstack = 0;
        }
        g_allow_until = now + 1500;
        return;
    }
    if (n == 1 && !_strnicmp(g[0].key, "PressStart", 10)) {   /* the title screen */
        say("Press Start", NULL); g_nstack = 0; return;
    }
    if (now - g_input_t > 1500 && now > g_allow_until) return;
    g_allow_until = 0;
    char text[1024] = "";
    int fighter = 0;
    for (int i = 0; i < n; i++) {
        /* descriptions are said with their item only */
        if (quiet_key(g[i].sec, g[i].key) || !g[i].val[0] || ends_with(g[i].key, "Desc")) continue;
        char t[512]; tidy(g[i].val, t, sizeof t);
        if (!t[0] || strstr(text, t)) continue;
        if (text[0]) strncat(text, ". ", sizeof text - strlen(text) - 1);
        strncat(text, t, sizeof text - strlen(text) - 1);
        if (!_stricmp(g[i].sec, "PlayerSelect")) fighter = 1;
    }
    if (!text[0]) return;
    /* the same text again (the game re-reads the name when a fighter is picked) is no news */
    if (!_stricmp(text, g_focus_label) && now - g_said_t < 5000) return;
    /* picking a fighter re-reads the name without moving the cursor: already said */
    if (fighter && !_stricmp(text, g_fighter_said) && g_grid_t < g_fighter_said_t) return;
    char pos[64] = "";
    if (fighter && g_grid_cell >= 0 && now - g_grid_t < 1000)
        snprintf(pos, sizeof pos, "row %d, column %d", g_grid_cell / 4 + 1, g_grid_cell % 4 + 1);
    if (fighter) { snprintf(g_fighter_said, sizeof g_fighter_said, "%s", text); g_fighter_said_t = now; }
    say(text, pos);
    snprintf(g_focus_label, sizeof g_focus_label, "%s", text);
    g_focus_holder = 0;
}

/* Lookups made within 30 ms of each other belong together (one screen build or one move). */
void menu_poll(void) {
    static LocCall grp[LQ];
    watch_input();
    if (g_np_t && GetTickCount() - g_np_t > 500) {
        int disabled = (LONG)(g_dis_t - g_np_t) > -1000 && (LONG)(g_dis_t - g_np_t) < 1000 ? g_dis_n : 0;
        if (g_np_count >= 2 && disabled)
            speak(L"Versus Mode, Team Ladder and Online wait for a second player. Connect a second controller, or open F2, "
                  L"Controls tab, and tick Two players on the same keyboard: player 2 then uses the arrows and the numpad.", 0);
        log_line("menu: %d players, %d menu items greyed out", g_np_count, disabled);
        g_np_t = 0;
    }
    for (;;) {                               /* Flash focus events first: they come before the lookups of the same move */
        FocusEv e;
        EnterCriticalSection(&g_clock);
        if (g_fq_w - g_fq_r > FQ) g_fq_r = g_fq_w - FQ;
        int have = g_fq_r != g_fq_w;
        if (have) e = g_fq[g_fq_r++ % FQ];
        LeaveCriticalSection(&g_clock);
        if (!have) break;
        on_focus_event(&e);
    }
    preset_poll();
    ml_say_pending();
    if (g_tower_pfx[0] && GetTickCount() - g_tower_t >= 500) { tower_say_card(); g_tower_pfx[0] = 0; }
    if (g_krypt_ping_t && (LONG)(GetTickCount() - g_krypt_ping_t) >= 0) {
        g_krypt_ping_t = 0;
        if (g_krypt_pings_on && g_krypt_locked) krypt_pings();
    }
    {   /* Select on a koffin you can't afford does nothing on screen: say so */
        static int prev_sel;
        /* Select = the A button (J, or G / Enter with the original PC keys); Back = B (K, or J) */
        int pc = keys_pc_on();
        int sel = (game_has_focus() && GetAsyncKeyState(pc ? 'G' : 'J') < 0) || (g_pad_buttons & 0x1000);
        /* Back opens the exit question: Select there is not a purchase */
        if ((game_has_focus() && GetAsyncKeyState(pc ? 'J' : 'K') < 0) || (g_pad_buttons & 0x2000)) g_krypt_locked = 0;
        if (sel && !prev_sel && g_krypt_locked && GetTickCount() - g_krypt_active_t < 600000) {
            g_krypt_select_t = GetTickCount();
            if (g_krypt_price > 0 && g_krypt_balance >= 0 && g_krypt_price > g_krypt_balance) {
                char m[128]; snprintf(m, sizeof m, "Not enough koins. It costs %d, you have %d.", g_krypt_price, g_krypt_balance);
                say(m, NULL);
            }
        }
        prev_sel = sel;
    }
    {
        char z[64] = "";
        EnterCriticalSection(&g_clock);
        if (g_krypt_zone[0]) { snprintf(z, sizeof z, "%s", g_krypt_zone); g_krypt_zone[0] = 0; }
        LeaveCriticalSection(&g_clock);
        if (z[0]) {
            char t[96]; tidy(z, t, sizeof t);
            /* after a fast travel the first koffin ("Bloodmarsh 1") is said just before: it names the zone already */
            if (!(GetTickCount() - g_krypt_said_t < 3000 && strstr(g_krypt_said, t))) say(t, NULL);
        }
        if (z[0]) g_krypt_active_t = GetTickCount();
        wallet_poll(z[0] != 0);
        krypt_motion();
    }
    for (;;) {
        EnterCriticalSection(&g_lqlock);
        if (g_lq_w - g_lq_r > LQ) g_lq_r = g_lq_w - LQ;
        LONG avail = g_lq_w - g_lq_r;
        DWORD last_t = avail ? g_lq[(g_lq_w - 1) % LQ].t : 0;
        if (!avail || GetTickCount() - last_t < 40) { LeaveCriticalSection(&g_lqlock); return; }
        int n = 0;
        DWORD prev = g_lq[g_lq_r % LQ].t;
        while (g_lq_r != g_lq_w && n < LQ) {
            LocCall *c = &g_lq[g_lq_r % LQ];
            if (n && c->t - prev > 30) break;
            prev = c->t; grp[n++] = *c; g_lq_r++;
        }
        LeaveCriticalSection(&g_lqlock);
        on_group(grp, n);
    }
}

void menu_debug(int on) { g_debug = on; log_line("menu debug %s", on ? "on" : "off"); }

void menu_init(void) {
    InitializeCriticalSection(&g_lqlock);
    InitializeCriticalSection(&g_clock);
    load_coalesced();
    loc_index();
    guest_hook(G_LOCALIZE, (void *)h_localize, (void **)&o_localize);
    guest_hook(G_SETTEXT, (void *)h_settext, (void **)&o_settext);
    guest_hook(G_RESOLVE, (void *)h_resolve, (void **)&o_resolve);
    guest_hook(G_INVOKE, (void *)h_invoke, (void **)&o_invoke);
    guest_hook(G_CLIPEVENT, (void *)h_clipevent, (void **)&o_clipevent);
    guest_hook(G_SETVAR, (void *)h_setvar, (void **)&o_setvar);
}

void menu_shutdown(void) {}
