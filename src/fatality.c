/*
 * Described fatalities: the Fatality Tutorial (Training > Fatality Tutorial)
 * and live matches.
 *
 * Tutorial: when a fatality is shown, the game localizes its name ("Split
 * Decision") and the tutorial's fatalityHUD shows the required input. menu.c
 * routes every localized menu text through fatality_try(): on a known fatality
 * name we speak the name and its input through the screen reader (so the player
 * can learn it). The description plays when the game starts the fatality's
 * cinematic (the callback below), as in matches. Until 2026-11-25 it played when
 * the tutorial's two input displays (command, "yourMove") held the same input;
 * but the game re-sends both displays on every press, so the description came
 * back on a later press (Liam, Discord 2026-11-24), and it also played when the
 * input was right but the distance wrong and no fatality happened.
 *
 * Live matches, either side (the CPU's on the player too): the game looks up
 * "Fatality1/2CinemaPostLoadCallback" when the cinematic loads (h_lookup below);
 * matching the player's motion ("live-match support") is the fallback.
 *
 * The description itself is a recorded Kokoro clip (the Tekken 3 AD voice).
 * Library: MK9Access\fatalities\index.tsv, one row per fatality:
 *     <name><TAB><input in words><TAB><clip><TAB><character><TAB><raw input><TAB><description>
 * (the description text is said by the screen reader instead of the clip with "Descriptions: screen reader").
 * built by tools/gen_fatalities.py from data/fatalities.tsv. Clips live beside it.
 */
#include "access.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAMEOBJ 0x833848D4u
#define GS_FATALITY 11

#define NFAT 128
typedef struct {
    char key[96]; char input[160]; char clip[128];
    char name[96];        /* as written, spoken in a match */
    char ch[40];          /* character key, e.g. "liukang", "cyberzero" (compares with CHAR_LIUKANG_B) */
    char who[40];         /* character as written, "Liu Kang" */
    int  num;             /* 1 or 2: the game's Fatality1 / Fatality2 (order of the rows per character) */
    char mot[24];         /* motion: F B U D toward/away from the opponent, e.g. "FDF" */
    char btn;             /* final attack button '1'..'4', 'K' block, 0 unknown */
    char desc[320];       /* the description as text */
} Fat;
static Fat g_fat[NFAT];
static int g_nfat;

static char g_said[96]; static DWORD g_said_t;        /* de-dup the spoken name */
static DWORD g_play_t;                /* a description was started (callback or motion match) */
static DWORD g_tut_t;                 /* last tutorial input-display update: not a live match */

/* The game names the fatality being run (found 2026-11-22 with `trace 82a10380`):
 * the script VM looks up its functions by name with 82a10380(script, name, ...),
 * and when a fatality's cinematic loads it looks up "Fatality1CinemaPostLoadCallback"
 * or "Fatality2CinemaPostLoadCallback" (then GenericFatalityPostloadCallback),
 * about 1.4 s before the actors snap together. Same path whoever performs it, so
 * this also names the CPU's fatality on the player. Fatality1 = the first
 * finishing move in the character's move list (Scorpion: Split Decision,
 * Fatality2 = Nether-Gate, both seen). The lookup runs many times a second
 * (ai_entry, p_attack, ...): the hook only copies names that end in
 * "CinemaPostLoadCallback"; fatality_poll does the rest on the mod thread. */
#define G_SCRIPT_LOOKUP 0x82A10380u
static guest_fn o_lookup;
static char g_cb_name[64];
static volatile LONG g_cb_seq;        /* bumped per new callback name */
static LONG g_cb_seen;                /* last g_cb_seq handled by fatality_poll */
static char g_cb_fat[64];             /* the last "Fatality<N>CinemaPostLoadCallback" */
static volatile LONG g_cb_fat_seq; static LONG g_cb_fat_seen;

static void h_lookup(PPCContext *ctx, uint8_t *base) {
    uint32_t a = (uint32_t)ctx->r4, scr = (uint32_t)ctx->r3;
    CALL_ORIG(o_lookup, ctx, base);
    InterlockedIncrement(&g_inflight);
    if (a >= 0x40000000u && guest_ok(a, 24)) {
        char n[64]; size_t k = 0;
        for (; k < sizeof n - 1 && guest_ok(a + k, 1); k++) {
            char c = (char)g8(a + k);
            if (c < 32 || c > 126) break;
            n[k] = c;
        }
        n[k] = 0;
        light_lookup(n, scr);
        static const char suf[] = "CinemaPostLoadCallback";
        size_t ls = sizeof suf - 1;
        /* stage fatalities: "BgndFatality1CinemaPostLoadCallbackWithAttacker" (the suffix dropped here) */
        if (k > 12 && !strcmp(n + k - 12, "WithAttacker")) { k -= 12; n[k] = 0; }
        if (k > ls && !strcmp(n + k - ls, suf)) {
            if (!_strnicmp(n, "Fatality", 8)) { memcpy(g_cb_fat, n, k + 1); InterlockedIncrement(&g_cb_fat_seq); }
            memcpy(g_cb_name, n, k + 1);
            InterlockedIncrement(&g_cb_seq);
        }
    }
    InterlockedDecrement(&g_inflight);
}



static void norm(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (; *in && n < cap - 1; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c)) out[n++] = (char)tolower(c);
    }
    out[n] = 0;
}

/* character display name -> key comparable with the game's CHAR_ names */
static void char_key(const char *display, char *out, size_t cap) {
    char n[64];
    norm(display, n, sizeof n);
    static const char *alias[][2] = { { "cybersubzero", "cyberzero" }, { "noobsaibot", "noob" }, { "sonyablade", "sonya" } };
    for (unsigned i = 0; i < sizeof alias / sizeof alias[0]; i++)
        if (!strcmp(n, alias[i][0])) { snprintf(out, cap, "%s", alias[i][1]); return; }
    snprintf(out, cap, "%.39s", n);
}

/* "CHAR_SCORPION_B" -> "scorpion" (costume suffix dropped) */
static void game_char_key(const char *gname, char *out, size_t cap) {
    const char *p = !_strnicmp(gname, "CHAR_", 5) ? gname + 5 : gname;
    size_t k = 0;
    for (; *p && *p != '_' && k < cap - 1; p++) out[k++] = (char)tolower((unsigned char)*p);
    out[k] = 0;
}

/* "Hold Block, Back, Forward, Up, Release Block, 2 (sweep)" -> mot "BFU", btn '2' */
static void parse_input(const char *raw, char *mot, size_t cap, char *btn) {
    size_t k = 0; *btn = 0;
    char buf[160]; snprintf(buf, sizeof buf, "%s", raw);
    char *paren = strchr(buf, '('); if (paren) *paren = 0;
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        while (*t == ' ') t++;
        char *e = t + strlen(t); while (e > t && e[-1] == ' ') *--e = 0;
        char d = !_stricmp(t, "Forward") ? 'F' : !_stricmp(t, "Back") ? 'B' : !_stricmp(t, "Down") ? 'D' : !_stricmp(t, "Up") ? 'U' : 0;
        if (d) { if (k < cap - 1) mot[k++] = d; }
        else if (t[0] >= '1' && t[0] <= '4' && !t[1]) *btn = t[0];
        else if (!_stricmp(t, "Block")) *btn = 'K';
    }
    mot[k] = 0;
}

static void stages_init(void);
static void pl_name(int p, char *out, size_t cap);

/* The description set: fatalities_es\ (Spanish clips and words) when the game runs in Spanish and that folder is
 * there, else fatalities\ (English). The names stay English in both: the Spanish game shows its move list in English. */
static const wchar_t *g_fdir = L"fatalities";
static int fat_es(void) { return g_fdir[10] == L'_'; }

void fatality_init(void) {
    g_nfat = 0;
    wchar_t path[MAX_PATH];
    FILE *f = NULL;
    if (lang_code() == 5) {
        swprintf(path, MAX_PATH, L"%lsfatalities_es\\index.tsv", g_mod_dir);
        if ((f = _wfopen(path, L"rb"))) g_fdir = L"fatalities_es";
    }
    if (!f) { swprintf(path, MAX_PATH, L"%lsfatalities\\index.tsv", g_mod_dir); f = _wfopen(path, L"rb"); }
    if (!f) { log_line("fatality: no index.tsv"); return; }
    char line[512];
    while (g_nfat < NFAT && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *t1 = strchr(line, '\t'); if (!t1) continue; *t1 = 0;
        char *t2 = strchr(t1 + 1, '\t'); if (!t2) continue; *t2 = 0;
        const char *name = line, *input = t1 + 1, *clip = t2 + 1;
        char *t3 = strchr(t2 + 1, '\t'), *t4 = NULL, *t5 = NULL;   /* optional: character, raw input, description */
        if (t3) { *t3 = 0; t4 = strchr(t3 + 1, '\t'); if (t4) { *t4 = 0; t5 = strchr(t4 + 1, '\t'); if (t5) *t5 = 0; } }
        if (!_stricmp(name, "name") || !name[0] || !clip[0]) continue;
        Fat *F = &g_fat[g_nfat];
        memset(F, 0, sizeof *F);
        norm(name, F->key, sizeof F->key);
        snprintf(F->name, sizeof F->name, "%.95s", name);
        snprintf(F->input, sizeof F->input, "%s", input);
        snprintf(F->clip, sizeof F->clip, "%s", clip);
        if (t3) {
            char_key(t3 + 1, F->ch, sizeof F->ch);
            snprintf(F->who, sizeof F->who, "%.39s", t3 + 1);
            for (int j = 0; j < g_nfat; j++) if (!strcmp(g_fat[j].ch, F->ch)) F->num++;
            F->num++;
        }
        if (t4) parse_input(t4 + 1, F->mot, sizeof F->mot, &F->btn);
        if (t5) snprintf(F->desc, sizeof F->desc, "%s", t5 + 1);
        if (F->key[0]) g_nfat++;
    }
    fclose(f);
    int withmot = 0;
    for (int i = 0; i < g_nfat; i++) if (g_fat[i].ch[0] && g_fat[i].mot[0]) withmot++;
    log_line("fatality: %d entries, %d with character + motion (%ls)", g_nfat, withmot, g_fdir);
    stages_init();
    guest_hook(G_SCRIPT_LOOKUP, (void *)h_lookup, (void **)&o_lookup);
}

/* Stage fatalities: the stage's own script looks up "BgndFatality1CinemaPostLoadCallback" (found 2026-11-25 in the
 * unpacked BGND_*.xxx packages: Pit, PitDay, DeadPool, LivingForest, Subway, Street, Hell).
 * The stage comes from story_stage() (the BGND_ package the port opened). Names here; the description clip and
 * text from fatalities\stages.tsv: <stage key><TAB><clip><TAB><description> (tools/gen_fatalities.py). */
static const char *k_stage_names[][3] = {   /* the game's Arena Select names, English and Spanish ([PlayerSelect]) */
    { "Pit", "The Pit", "El Foso" }, { "PitDay", "The Pit", "El Foso" }, { "DeadPool", "The Dead Pool", "La Charca de los Muertos" },
    { "LivingForest", "The Living Forest", "El Bosque Viviente" }, { "Subway", "Subway", "Paso subterráneo" },
    { "Street", "The Street", "La Calle" }, { "Hell", "Hell", "Infierno" },
};
static const Fat *g_pend; static DWORD g_pend_t;      /* description started just after the spoken name */
static DWORD g_named_t;               /* a fatality was named from the callback */
#define NSTAGE 16
static Fat g_stagefat[NSTAGE]; static int g_nstagefat;

static void stages_init(void) {
    g_nstagefat = 0;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls%ls\\stages.tsv", g_mod_dir, g_fdir);
    FILE *f = _wfopen(path, L"rb");
    if (!f) { log_line("fatality: no stages.tsv"); return; }
    char line[512];
    while (g_nstagefat < NSTAGE && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *t1 = strchr(line, '\t'); if (!t1) continue; *t1 = 0;
        char *t2 = strchr(t1 + 1, '\t'); if (t2) *t2 = 0;
        Fat *F = &g_stagefat[g_nstagefat];
        memset(F, 0, sizeof *F);
        snprintf(F->ch, sizeof F->ch, "%.39s", line);                 /* the stage key */
        snprintf(F->clip, sizeof F->clip, "%.127s", t1 + 1);
        if (t2) snprintf(F->desc, sizeof F->desc, "%.319s", t2 + 1);
        g_nstagefat++;
    }
    fclose(f);
    log_line("fatality: %d stage fatality descriptions", g_nstagefat);
}

static void fat_describe(const Fat *F);
static void stage_fatality(int atk) {
    char st[32]; story_stage(st, sizeof st);
    const char *nm = NULL;
    for (unsigned i = 0; i < sizeof k_stage_names / sizeof k_stage_names[0]; i++)
        if (!_stricmp(st, k_stage_names[i][0])) nm = k_stage_names[i][fat_es() ? 2 : 1];
    const Fat *F = NULL;
    for (int i = 0; i < g_nstagefat; i++) if (!_stricmp(g_stagefat[i].ch, st)) F = &g_stagefat[i];
    char who[48] = "", gname[40], ch[40], msg[160];
    pl_name(atk, gname, sizeof gname); game_char_key(gname, ch, sizeof ch);
    for (int i = 0; i < g_nfat; i++) if (!strcmp(g_fat[i].ch, ch)) { snprintf(who, sizeof who, "%s", g_fat[i].who); break; }
    snprintf(msg, sizeof msg, "%s%s%s%s%s", atk && who[0] ? who : "", atk && who[0] ? ": " : "",
             fat_es() ? "Fatality de escenario" : "Stage Fatality", nm ? ", " : "", nm ? nm : "");
    log_line("fatality named: stage fatality on %s by %s (player %d) -> %s", st[0] ? st : "?", gname, atk + 1, F ? F->clip : "none");
    { int was = speech_force(1); speak_utf8(msg, 1); speech_force(was); }
    if (F && (F->clip[0] || F->desc[0])) { g_pend = F; g_pend_t = GetTickCount() + (atk ? 2200 : 1700); }
    g_play_t = g_named_t = GetTickCount();
}

static void fat_describe(const Fat *F) {
    wchar_t cp[MAX_PATH]; swprintf(cp, MAX_PATH, L"%ls%ls\\%hs", g_mod_dir, g_fdir, F->clip);
    desc_play(cp, F->desc);
}

static int in_fatality(void) {
    uint32_t o = g32(GAMEOBJ);
    return o >= 0x30000000u && o < 0xA0000000u && (int)g32(o + 4) == GS_FATALITY;
}

/* ---------- live-match support ----------
 * A match does not name the fatality. What the game does show (measured
 * 2026-11-18 in Ladder fights):
 *  - state 8 (fighting) -> 11 at FINISH HIM, not when a fatality fires. The
 *    state stays 11 through Finish Him and the fatality.
 *  - when a fatality starts, the cinematic puts both fighters' actors at exactly
 *    the same position (+0xE0); while they can move they never overlap.
 * (The actor's +0x2c is a spawn counter - 3/4, 5/6, 7/8 in successive fights -
 *  not a move id, so it can't name the fatality.)
 * So: in state 11 after Finish Him, the first moment both positions are equal
 * and stay so = a fatality began. Which one: the player's motion (pad.c keeps
 * each controller's recent directions and attack buttons), compared with the
 * attacker's two fatalities, sides turned into Forward/Back from the side the
 * attacker faced. A CPU fatality has no motion and stays unnamed. */
#define GAMEINFO 0x833C6C54u
#define PSTRIDE  0xC8Cu
#define P_OBJ    0xDF4u
#define P_NAME   0xB6Cu
#define O_HEALTH 0x78C0u
#define A_X      0xE0u
static int g_prev_state = -1;
static int g_finish;                      /* in Finish Him (8 -> 11), cinematic not yet started */
static int g_cine;                        /* the fatality cinematic has started */
static int g_snap_n;                      /* polls in a row with both fighters at one position */
static int g_faces_right[2] = { 1, 0 };   /* side each fighter faced while they could move */

static int vptr(uint32_t a) { return a >= 0x30000000u && a < 0xA0000000u; }
static uint32_t pl_obj(int p) { uint32_t o = g32(GAMEINFO + (uint32_t)p * PSTRIDE + P_OBJ); return vptr(o) ? o : 0; }
static void pl_name(int p, char *out, size_t cap) {
    uint32_t a = GAMEINFO + (uint32_t)p * PSTRIDE + P_NAME; size_t i = 0;
    for (; i < cap - 1; i++) { char c = (char)g8(a + i); if (!c || c < 32 || c > 126) break; out[i] = c; }
    out[i] = 0;
}
static float pl_hp(int p) { uint32_t o = pl_obj(p); return o ? gf(o + O_HEALTH) : -1; }
static uint32_t pl_actor(int p) { uint32_t o = pl_obj(p); if (!o) return 0; uint32_t a = g32(o + 8); return vptr(a) ? a : 0; }

/* Finish Him is on and the fatality has not started: fight.c keeps the distance hum. */
int fatality_finish_him(void) { return g_finish && !g_cine; }

/* Score a fatality against a motion (sides already turned into F/B):
 * base 3 = the directions end with the fatality's motion,
 * base 2 = its motion appears in order among the last few directions, 0 = no.
 * Returns base*100 + button match*10 + motion length (a longer exact match is
 * more specific: Stryker's D F D F over F D F). */
static int motion_score(const Fat *F, const char *toks) {
    char dirs[64]; int nd = 0; char last_btn = 0;
    for (const char *p = toks; *p && nd < 63; p++) {
        if (*p >= '1' && *p <= '4') last_btn = *p;
        else dirs[nd++] = *p;
    }
    dirs[nd] = 0;
    int m = (int)strlen(F->mot);
    if (!m || nd < m) return 0;
    int base = 0;
    if (!strcmp(dirs + nd - m, F->mot)) base = 3;
    else {
        int from = nd - (m + 3); if (from < 0) from = 0;
        int j = 0;
        for (int i = from; i < nd && j < m; i++) if (dirs[i] == F->mot[j]) j++;
        if (j == m) base = 2;
    }
    if (!base) return 0;
    return base * 100 + (F->btn && F->btn == last_btn ? 10 : 0) + m;
}

static void match_fatality(void) {
    int atk = pl_hp(0) >= pl_hp(1) ? 0 : 1;            /* the winner performs it */
    char gname[40]; pl_name(atk, gname, sizeof gname);
    char ch[40]; game_char_key(gname, ch, sizeof ch);
    const Fat *best = NULL; int best_sc = 0, best_user = -1; char shown[64] = "";
    DWORD since = GetTickCount() - 5000;
    for (int u = 0; u < 4; u++) {
        char raw[64];
        if (!pad_motion(u, since, raw, sizeof raw)) continue;
        char toks[64]; int k = 0;
        for (const char *p = raw; *p && k < 63; p++) {
            char c = *p;
            if (c == 'L') c = g_faces_right[atk] ? 'B' : 'F';
            else if (c == 'R') c = g_faces_right[atk] ? 'F' : 'B';
            toks[k++] = c;
        }
        toks[k] = 0;
        if (!shown[0]) snprintf(shown, sizeof shown, "%s", toks);
        for (int i = 0; i < g_nfat; i++) {
            if (strcmp(g_fat[i].ch, ch)) continue;
            int sc = motion_score(&g_fat[i], toks);
            if (sc > best_sc) { best_sc = sc; best = &g_fat[i]; best_user = u; snprintf(shown, sizeof shown, "%s", toks); }
        }
    }
    log_line("fatality match: %s, motion %s (pad %d) -> %s score %d",
             gname, shown[0] ? shown : "-", best_user, best ? best->clip : "none", best_sc);
    if (!best) return;                                 /* the CPU's, a stage fatality, or unknown */
    { int was = speech_force(1); speak_utf8(best->name, 1); speech_force(was); }   /* fatality names speak even mid-fight */
    g_pend = best;
    g_pend_t = GetTickCount() + 1100;                  /* let the name finish first */
    g_play_t = GetTickCount();
}

/* The game looked up "<Fatality N>CinemaPostLoadCallback": name it and play its
 * description, for either side. Player 1's own fatality is said by name only;
 * the other side's gets the character first ("Stryker: Time Served"). */
static void named_fatality(const char *cb) {
    int n = 0;
    if (!_strnicmp(cb, "Fatality", 8) && cb[8] >= '1' && cb[8] <= '9' && !strcmp(cb + 9, "CinemaPostLoadCallback"))
        n = cb[8] - '0';
    int atk = pl_hp(0) >= pl_hp(1) ? 0 : 1;            /* the winner performs it */
    char gname[40]; pl_name(atk, gname, sizeof gname);
    char ch[40]; game_char_key(gname, ch, sizeof ch);
    if (!_stricmp(cb, "BgndFatality1CinemaPostLoadCallback")) { stage_fatality(atk); return; }
    if (!_stricmp(cb, "BabalityCinemaPostLoadCallback")) {   /* name only, no clip */
        char who[48] = "", msg[96];
        for (int i = 0; i < g_nfat; i++) if (!strcmp(g_fat[i].ch, ch)) { snprintf(who, sizeof who, "%s", g_fat[i].who); break; }
        if (atk == 0 || !who[0]) snprintf(msg, sizeof msg, "Babality");
        else snprintf(msg, sizeof msg, "%s: Babality", who);
        log_line("fatality named: %s by %s (player %d)", cb, gname, atk + 1);
        { int was = speech_force(1); speak_utf8(msg, 1); speech_force(was); }
        g_play_t = g_named_t = GetTickCount();
        return;
    }
    /* Fatality3 = the Klassic Fatality (Spine Rip, Toasty!, Yummy!): the third row in index.tsv */
    const Fat *F = NULL;
    for (int i = 0; n && i < g_nfat; i++) if (g_fat[i].num == n && !strcmp(g_fat[i].ch, ch)) { F = &g_fat[i]; break; }
    log_line("fatality named: %s by %s (player %d) -> %s", cb, gname, atk + 1, F ? F->clip : "none");
    if (!F) return;                                    /* a stage fatality (callback name unknown) or a character not in the library */
    char msg[160];
    if (atk == 0) snprintf(msg, sizeof msg, "%s", F->name);
    else snprintf(msg, sizeof msg, "%s: %s", F->who, F->name);
    { int was = speech_force(1); speak_utf8(msg, 1); speech_force(was); }
    g_pend = F;
    g_pend_t = GetTickCount() + (atk == 0 ? 1100 : 1700);   /* let the name finish first */
    g_play_t = g_named_t = GetTickCount();
}

void fatality_poll(void) {
    uint32_t go = g32(GAMEOBJ);
    int s = vptr(go) ? (int)g32(go + 4) : -1;
    DWORD now = GetTickCount();
    uint32_t a0 = pl_actor(0), a1 = pl_actor(1);
    float x0 = a0 ? gf(a0 + A_X) : 0, x1 = a1 ? gf(a1 + A_X) : 0;
    if (s == 8) {
        g_finish = 0; g_cine = 0; g_snap_n = 0;
    }
    else if (s == 11 && g_prev_state == 8) {
        g_finish = 1; g_cine = 0; g_snap_n = 0;
        log_line("fatality: finish him");
    } else if (s != 11) { g_finish = 0; g_cine = 0; }
    LONG seq = g_cb_seq, fseq = g_cb_fat_seq;
    if (seq != g_cb_seen) {
        g_cb_seen = seq;
        char cb[64];
        if (fseq != g_cb_fat_seen) memcpy(cb, g_cb_fat, sizeof cb);   /* a fatality's callback wins over others */
        else memcpy(cb, g_cb_name, sizeof cb);
        g_cb_fat_seen = fseq;
        cb[63] = 0;
        /* The fatality's cinematic is loading: Finish Him is over. Some cinematics never put both actors on one
         * position until their very end (Freddy's Tell 'Em Freddy Sent Ya: at the Continue menu), so without this
         * the Continue? menu after them was held back as live-fight speech. */
        if (s == GS_FATALITY && (!_strnicmp(cb, "Fatality", 8) || !_strnicmp(cb, "BgndFatality", 12) ||
                                 !_strnicmp(cb, "Babality", 8)) && g_finish && !g_cine) {
            g_cine = 1;
            log_line("fatality: cinematic loading (%s)", cb);
        }
        /* Matches, the Challenge Tower and the Fatality Tutorial alike; one per fatality */
        if (s == GS_FATALITY && now - g_play_t > 5000) named_fatality(cb);
        else log_line("fatality callback %s (state %d, not named)", cb, s);
    }
    if ((s == 8 || (s == 11 && g_finish && !g_cine)) && a0 && a1) {
        if (x0 == x1) g_snap_n++;
        else { g_snap_n = 0; g_faces_right[0] = x1 > x0; g_faces_right[1] = x0 > x1; }
        /* the cinematic took over (stable for ~50 ms, so a jump over the opponent can't pass) */
        if (s == 11 && g_snap_n >= 3) {
            g_cine = 1;
            log_line("fatality: cinematic started (x %.2f / %.2f)", x0, x1);
            /* fallback when the game's callback name was not seen */
            if (now - g_tut_t > 30000 && now - g_play_t > 8000 && now - g_named_t > 5000) match_fatality();
        }
    }
    if (g_pend && (LONG)(now - g_pend_t) >= 0) {
        fat_describe(g_pend);
        g_pend = NULL;
    }
    g_prev_state = s;
}

/* A fatality name just localized: speak its name and input, remember its clip.
 * Returns 1 so the caller skips its own plain name read. */
int fatality_try(const char *text) {
    if (g_nfat <= 0 || !text || !text[0] || !in_fatality()) return 0;
    char nk[96];
    norm(text, nk, sizeof nk);
    if (!nk[0]) return 0;
    for (int i = 0; i < g_nfat; i++) {
        if (strcmp(g_fat[i].key, nk)) continue;
        if (!(!strcmp(nk, g_said) && GetTickCount() - g_said_t < 4000)) {   /* not just said */
            snprintf(g_said, sizeof g_said, "%s", nk); g_said_t = GetTickCount();
            char msg[320];
            if (g_fat[i].input[0]) snprintf(msg, sizeof msg, "%s. %s.", text, g_fat[i].input);
            else snprintf(msg, sizeof msg, "%s.", text);
            { int was = speech_force(1); speak_utf8(msg, 1); speech_force(was); }
        }
        return 1;
    }
    return 0;
}

/* The tutorial's input displays (fatalityHUD command / yourMove): bracket strings
 * like "[Towards][Down][Towards][BP]". Only noted: a tutorial is on, so the motion
 * fallback stays out of it (the game's callback describes the fatality). */
static int bracket_tokens(const char *s) {       /* count of "[...]" tokens; 0 if not a bracket string */
    if (s[0] != '[') return 0;
    int toks = 0, depth = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '[') { if (depth) return 0; depth = 1; }
        else if (*p == ']') { if (!depth) return 0; depth = 0; toks++; }
        else if (!isalnum((unsigned char)*p) && *p != '+') return 0;
    }
    return depth ? 0 : toks;
}

void fatality_input_settext(uint32_t obj, const char *val) {
    (void)obj;
    if (val && in_fatality() && bracket_tokens(val) >= 2) g_tut_t = GetTickCount();
}
