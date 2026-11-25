/*
 * Fights: the opponent distance hum, health tones and the fight hotkeys.
 *
 * Game data (found by Lauta for the port's online play, docs/online/engine-map.md
 * in mk9-lauta; checked in Practice 2026-11-16):
 *   match info 0x833C6C54, one block of 0xC8C bytes per player:
 *     +0xB6C character name ("CHAR_SCORPION"), +0xDF4 the fighter object
 *   fighter object: +0x78C0 health (float, 1 full), +8 its actor
 *   actor: +0xE0 position along the fight (grows to the right of the screen),
 *          +0xE4 height (0 on the ground)
 *   game state: [0x833848D4]+4, 8 = fighting (3 main menu, 7 character select,
 *     9 game over, 11 Finish Him + fatality, 12 ladder)
 *   paused: u32 0x833DD100 is 1 while the pause menu is up (found 2026-11-16)
 * The camera keeps the fighters at most 600 apart; standing in contact is
 * about 89.
 *
 * Distance cue, chosen with F8 (saved in MK9Access\settings.txt): clicks (default since 2026-11-25), the hum, or off.
 * Sound channels, one meaning each (spatial-audio-kit rules):
 *   click rate          = distance (a parking sensor: 2.2 ticks/s far .. 11.8 touching, geometric)
 *   click pitch         = the opponent is in the air (a fourth up, with hysteresis)
 *   hum pitch and level = distance (110 Hz far .. 330 Hz touching, as SC2 Access)
 *   hum tremolo         = the opponent is in the air
 *   pan (both)          = the opponent's side (equal power, width 0.8)
 *   short note pairs    = a fighter's health passed half / a quarter
 *                         (mine fall and sit in the middle, theirs rise from their side)
 */
#include "access.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GAMEINFO 0x833C6C54u
#define PSTRIDE  0xC8Cu
#define P_NAME   0xB6Cu
#define P_OBJ    0xDF4u
#define O_HEALTH 0x78C0u
#define A_X      0xE0u
#define A_Z      0xE4u
#define GAMEOBJ  0x833848D4u
#define GS_PLAY  8
#define GS_FATALITY 11
#define PAUSED   0x833DD100u

#define D_FAR   600.0f
#define D_NEAR  89.0f

/* Which fighter is the player's. Offline it is player 1. In an online session the game is started again with
 * --mk9_net_role=host|guest (mk9-lauta session.cpp; a direct test session: --mk9_net_host / --mk9_net_join): the
 * host's pad is player 1 and the guest's player 2 on both machines (netplay.h), so the guest is player 2. */
static int me_slot(void) {
    static int slot = -1;
    if (slot < 0) {
        const wchar_t *cl = GetCommandLineW();
        slot = cl && (wcsstr(cl, L"--mk9_net_role=guest") || wcsstr(cl, L"--mk9_net_join=")) ? 1 : 0;
        if (slot) log_line("fight: online guest, player 2 is mine");
    }
    return slot;
}

enum { CUE_OFF, CUE_CLICKS, CUE_HUM, CUE_N };
static int s_cue = CUE_CLICKS, s_hp_tones = 1, s_vol = 2, s_fight_speech = 0;
static const float k_vol[4] = { 0.5f, 0.71f, 1.0f, 1.41f };    /* about -6, -3, 0, +3 dB */
static volatile int s_live;             /* both fighters in play (fight_live adds pause, port window, result menu) */
static volatile int s_hold;             /* a challenge result menu is up (fight_hold) */
static int s_air;                       /* opponent airborne, with hysteresis */

/* MK9Access\settings.txt: "distance_cue=clicks" etc. The updater overlays the zip, which has no settings file. */
static void settings_path(wchar_t *p) { swprintf(p, MAX_PATH, L"%lssettings.txt", g_mod_dir); }
static void settings_save(void) {
    static const char *cue[CUE_N] = { "off", "clicks", "hum" };
    wchar_t p[MAX_PATH]; settings_path(p);
    FILE *f = _wfopen(p, L"w");
    if (!f) { log_line("fight: settings not saved"); return; }
    fprintf(f, "distance_cue=%s\ndistance_volume=%d\nhealth_tones=%d\nfight_speech=%d\n", cue[s_cue], s_vol + 1, s_hp_tones, s_fight_speech);
    fclose(f);
}
static void settings_load(void) {
    static int done;
    if (done) return;
    done = 1;
    wchar_t p[MAX_PATH]; settings_path(p);
    FILE *f = _wfopen(p, L"r");
    if (!f) return;
    char line[128], v[64];
    int n;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "distance_cue=%63s", v) == 1) s_cue = !strcmp(v, "off") ? CUE_OFF : !strcmp(v, "hum") ? CUE_HUM : CUE_CLICKS;
        else if (sscanf(line, "distance_volume=%d", &n) == 1 && n >= 1 && n <= 4) s_vol = n - 1;
        else if (sscanf(line, "health_tones=%d", &n) == 1) s_hp_tones = n != 0;
        else if (sscanf(line, "fight_speech=%d", &n) == 1) s_fight_speech = n != 0;
    }
    fclose(f);
    log_line("fight: settings cue %d volume %d health tones %d fight speech %d", s_cue, s_vol + 1, s_hp_tones, s_fight_speech);
}

/* Read from any thread before speaking. The pause menu, the port overlay and a challenge result speak at once,
 * without waiting for the next poll. */
int fight_live(void) { return s_live && !s_hold && !g32(PAUSED) && !im_menu_on(); }
int fight_quiet(void) { return !s_fight_speech && fight_live(); }

static void cues_off(void) { hum_set(0, 110, 0, 0, W_SOFT); hum_trem(0, 0); click_set(0, 0, 0, 700); }
static float s_hp[2] = { -1, -1 };
static int s_inplay;

typedef struct { float x, z, hp; } Fighter;

static int game_state(void) {
    uint32_t o = g32(GAMEOBJ);
    return o >= 0x30000000u && o < 0xA0000000u ? (int)g32(o + 4) : -1;
}

static int valid_ptr(uint32_t a) { return a >= 0x30000000u && a < 0xA0000000u; }

static int read_fighter(int p, Fighter *f) {
    uint32_t obj = g32(GAMEINFO + p * PSTRIDE + P_OBJ);
    if (!valid_ptr(obj)) return 0;
    uint32_t act = g32(obj + 8);
    if (!valid_ptr(act)) return 0;
    f->hp = gf(obj + O_HEALTH); f->x = gf(act + A_X); f->z = gf(act + A_Z);
    return isfinite(f->hp) && isfinite(f->x) && isfinite(f->z) && f->hp >= -0.1f && f->hp <= 1.5f;
}

/* "Sub-Zero": the game's own text for the character select key of CHAR_SUBZERO. */
static void fighter_name(int p, char *out, size_t cap) {
    char raw[48] = "";
    uint32_t a = GAMEINFO + p * PSTRIDE + P_NAME;
    for (int i = 0; i < 47; i++) { char c = (char)g8(a + i); if (!c || c < 32 || c > 126) break; raw[i] = c; raw[i + 1] = 0; }
    const char *key = !strncmp(raw, "CHAR_", 5) ? raw + 5 : raw;
    const char *t = loc_get("PlayerSelect", key);
    if (t) { text_tidy(t, out, cap); return; }
    size_t n = 0;                                      /* not in the table: Title Case the key */
    for (const char *c = key; *c && n < cap - 1; c++) out[n++] = (char)(c == key ? *c : tolower((unsigned char)*c));
    out[n] = 0;
    if (!out[0]) snprintf(out, cap, p ? "Opponent" : "You");
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* This player's online name: the port starts a session with --mk9_online_name=<name> (session.cpp; the whole
 * argument is quoted when the name has a space), saved before that in online_name.txt beside mk9.exe. */
static void own_online_name(char *out, size_t cap) {
    out[0] = 0;
    const wchar_t *cl = GetCommandLineW(), *a = cl ? wcsstr(cl, L"--mk9_online_name=") : NULL;
    if (a) {
        int quoted = a > cl && a[-1] == L'"';
        a += wcslen(L"--mk9_online_name=");
        wchar_t w[96]; int n = 0;
        while (a[n] && n < 95 && (quoted ? a[n] != L'"' : a[n] != L' ')) { w[n] = a[n]; n++; }
        w[n] = 0;
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    }
    if (!out[0]) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%lsonline_name.txt", g_game_dir);
        FILE *f = _wfopen(path, L"rb");
        if (f) { if (!fgets(out, (int)cap, f)) out[0] = 0; fclose(f); }
        if (!strncmp(out, "\xef\xbb\xbf", 3)) memmove(out, out + 3, strlen(out + 3) + 1);
    }
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
}

/* The round winner as a player, for "<who> wins" (forum request 2026-11-23: a multiplayer match names the player,
 * not the character). The winner is the side with more health left when the game says WINS (a KO leaves the loser
 * at 0, a time-out goes to the one ahead). Online 1-on-1 sessions: this player's name or the rival's from the
 * session corner. Local 2-player Versus Mode: "Player 1" / "Player 2". Returns 0 to
 * keep the character's name: against the CPU (Team Ladder too), an even double KO, watching or a replay, King of the Hill. */
int fight_winner_name(char *out, size_t cap) {
    Fighter f[2];
    if (!read_fighter(0, &f[0]) || !read_fighter(1, &f[1]) || fabsf(f[0].hp - f[1].hp) < 0.001f) return 0;
    int w = f[0].hp > f[1].hp ? 0 : 1;
    const wchar_t *cl = GetCommandLineW();
    int online = cl && (wcsstr(cl, L"--mk9_net_role=host") || wcsstr(cl, L"--mk9_net_role=guest") ||
                        wcsstr(cl, L"--mk9_net_join=") || wcsstr(cl, L"--mk9_net_host="))
                 && !wcsstr(cl, L"--mk9_net_koth") && !wcsstr(cl, L"--mk9_net_replay");
    out[0] = 0;
    if (online) {
        if (w == me_slot()) own_online_name(out, cap);
        else snprintf(out, cap, "%s", im_rival_name());
        if (!out[0]) snprintf(out, cap, "Player %d", w + 1);   /* a direct test session has no names */
    } else if (menu_local_versus()) {
        snprintf(out, cap, "Player %d", w + 1);
    } else return 0;
    log_line("fight: round won by player %d (%s), health %.2f / %.2f", w + 1, out, f[0].hp, f[1].hp);
    return 1;
}

static void hp_tones(int p, float before, float after, float pan) {
    if (!s_hp_tones) return;
    static const float marks[2] = { 0.5f, 0.25f };
    for (int m = 0; m < 2; m++) {
        if (!(before > marks[m] && after <= marks[m])) continue;
        if (p == 0) {     /* mine: falling, low, centred */
            float f = m ? 294 : 392;
            tone_at(0, f, f, 0.12f, 0.16f, 0, W_SOFT);
            tone_at(0.15f, f * 0.75f, f * 0.75f, 0.16f, 0.16f, 0, W_SOFT);
        } else {          /* theirs: rising, higher, from their side */
            float f = m ? 784 : 587;
            tone_at(0, f, f, 0.10f, 0.13f, pan, W_SOFT);
            tone_at(0.13f, f * 1.335f, f * 1.335f, 0.12f, 0.13f, pan, W_SOFT);
        }
        log_line("fight: player %d health under %.0f%%", p + 1, marks[m] * 100);
    }
}

/* A Challenge Tower result (Success / Failure): the game stays in the fight state with both fighters behind the
 * "Continue? Retry" menu, so the hum played under that menu (forum report 2026-11-24). Held until something loads
 * (Retry, Return To Tower: [MainMenu] LOADING), the fight state ends, or both fighters are back at full health. */
void fight_hold(void) { s_hold = 1; log_line("fight: cues held (challenge result)"); }
void fight_resume(void) { if (s_hold) log_line("fight: cues resumed"); s_hold = 0; }

void fight_poll(void) {
    Fighter me, op;
    /* the hum carries on through Finish Him (state 11) until the fatality starts: range matters there */
    settings_load();
    int st = game_state();
    int ms = me_slot();
    int inplay = (st == GS_PLAY || (st == GS_FATALITY && fatality_finish_him())) && read_fighter(ms, &me) && read_fighter(1 - ms, &op);
    /* Story Mode loads the next fight behind its movies (state 8 with both fighters from 3 s in): no cues there */
    if (story_playing()) inplay = 0;
    if (!inplay) {
        if (s_inplay) { cues_off(); s_hp[0] = s_hp[1] = -1; log_line("fight: cues off"); }
        s_inplay = 0;
        s_live = 0;
        s_hold = 0;
        return;
    }
    if (s_hold && me.hp >= 0.999f && op.hp >= 0.999f) fight_resume();
    if (s_hold) {
        if (s_inplay) cues_off();
        s_live = 0;
        s_hp[0] = me.hp; s_hp[1] = op.hp;
        return;
    }
    if (!s_inplay) log_line("fight: cues on");
    s_inplay = 1;
    s_live = 1;
    /* paused (engine flag 0x833DD100, found 2026-11-16) or the port's overlay up: hush, keep counting */
    if (g32(PAUSED) || im_menu_on()) { cues_off(); return; }
    float dx = op.x - me.x, d = fabsf(dx);
    float k = clampf((D_FAR - d) / (D_FAR - D_NEAR), 0, 1);
    /* full side as soon as they are apart (contact is 89): the side matters most up close, for cross-ups */
    float pan = 0.8f * clampf(dx / 60.0f, -1, 1);
    if (op.z > 40) s_air = 1; else if (op.z < 25) s_air = 0;
    float vol = k_vol[s_vol];
    if (s_cue == CUE_HUM) {
        hum_set(0, 110.0f * powf(3.0f, k), (0.035f + 0.065f * k) * vol, pan, W_SOFT);
        hum_trem(0, s_air ? 0.6f : 0);
    } else { hum_set(0, 110, 0, 0, W_SOFT); hum_trem(0, 0); }
    if (s_cue == CUE_CLICKS) {
        /* 450 ms between ticks far away down to 85 ms touching: equal steps in ratio, so each step closer is
         * heard as the same speed-up; level rises a little with it */
        float interval = 0.450f * powf(0.085f / 0.450f, k);
        click_set(1.0f / interval, (0.075f + 0.035f * k) * vol, pan, s_air ? 935.0f : 700.0f);
    } else click_set(0, 0, 0, 700);
    float hp[2] = { me.hp, op.hp };
    for (int p = 0; p < 2; p++) {
        /* a new round refills health: start counting again without tones */
        if (s_hp[p] >= 0 && hp[p] < s_hp[p]) hp_tones(p, s_hp[p], hp[p], pan);
        s_hp[p] = hp[p];
    }
}

void fight_key(int vk, int shift, int ctrl) {
    Fighter me, op;
    int st = game_state();
    int ms = me_slot();
    int ok = (st == GS_PLAY || (st == GS_FATALITY && fatality_finish_him())) && read_fighter(ms, &me) && read_fighter(1 - ms, &op);
    char a[64], b[64];
    switch (vk) {
    case VK_F6:                                   /* health: mine first */
        if (!ok) { speak(L"Not in a fight", 1); return; }
        fighter_name(ms, a, sizeof a); fighter_name(1 - ms, b, sizeof b);
        speakf(1, "%s %d percent. %s %d percent.", a, (int)lroundf(clampf(me.hp, 0, 1) * 100),
               b, (int)lroundf(clampf(op.hp, 0, 1) * 100));
        break;
    /* No spoken opponent-position key: in a fight there's no time to listen to it;
     * the distance hum carries side, distance and airborne. */
    case VK_F8:                                   /* settings, saved at once */
        settings_load();
        if (shift && ctrl) {
            s_fight_speech = !s_fight_speech;
            speak(s_fight_speech ? L"Speech during fights on" : L"Speech during fights off", 1);
        } else if (ctrl) {
            s_vol = (s_vol + 1) % 4;
            speakf(1, "Distance volume %d of 4", s_vol + 1);
        } else if (shift) {
            s_hp_tones = !s_hp_tones; speak(s_hp_tones ? L"Health tones on" : L"Health tones off", 1);
        } else {
            s_cue = (s_cue + 1) % CUE_N;
            static const wchar_t *names[CUE_N] = { L"Distance cue off", L"Distance clicks", L"Distance hum" };
            speak(names[s_cue], 1);
        }
        settings_save();
        break;
    }
}
