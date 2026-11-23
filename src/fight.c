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
 * Sound channels, one meaning each (spatial-audio-kit rules):
 *   hum pitch and level = distance (110 Hz far .. 330 Hz touching, as SC2 Access)
 *   hum pan             = the opponent's side (equal power, width 0.8)
 *   hum tremolo         = the opponent is in the air
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

static int s_hum_on = 1, s_hp_tones = 1;
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

void fight_poll(void) {
    Fighter me, op;
    /* the hum carries on through Finish Him (state 11) until the fatality starts: range matters there */
    int st = game_state();
    int inplay = (st == GS_PLAY || (st == GS_FATALITY && fatality_finish_him())) && read_fighter(0, &me) && read_fighter(1, &op);
    if (!inplay) {
        if (s_inplay) { hum_set(0, 110, 0, 0, W_SOFT); hum_trem(0, 0); s_hp[0] = s_hp[1] = -1; log_line("fight: cues off"); }
        s_inplay = 0;
        return;
    }
    if (!s_inplay) log_line("fight: cues on");
    s_inplay = 1;
    /* paused (engine flag 0x833DD100, found 2026-11-16) or the port's overlay up: hush, keep counting */
    if (g32(PAUSED) || im_menu_on()) { hum_set(0, 110, 0, 0, W_SOFT); return; }
    float dx = op.x - me.x, d = fabsf(dx);
    float k = clampf((D_FAR - d) / (D_FAR - D_NEAR), 0, 1);
    /* full side as soon as they are apart (contact is 89): the side matters most up close, for cross-ups */
    float pan = 0.8f * clampf(dx / 60.0f, -1, 1);
    hum_set(0, 110.0f * powf(3.0f, k), s_hum_on ? 0.035f + 0.065f * k : 0, pan, W_SOFT);
    hum_trem(0, op.z > 40 ? 0.6f : 0);
    float hp[2] = { me.hp, op.hp };
    for (int p = 0; p < 2; p++) {
        /* a new round refills health: start counting again without tones */
        if (s_hp[p] >= 0 && hp[p] < s_hp[p]) hp_tones(p, s_hp[p], hp[p], pan);
        s_hp[p] = hp[p];
    }
}

void fight_key(int vk, int shift, int ctrl) {
    (void)ctrl;
    Fighter me, op;
    int st = game_state();
    int ok = (st == GS_PLAY || (st == GS_FATALITY && fatality_finish_him())) && read_fighter(0, &me) && read_fighter(1, &op);
    char a[64], b[64];
    switch (vk) {
    case VK_F6:                                   /* health */
        if (!ok) { speak(L"Not in a fight", 1); return; }
        fighter_name(0, a, sizeof a); fighter_name(1, b, sizeof b);
        speakf(1, "%s %d percent. %s %d percent.", a, (int)lroundf(clampf(me.hp, 0, 1) * 100),
               b, (int)lroundf(clampf(op.hp, 0, 1) * 100));
        break;
    /* No spoken opponent-position key: in a fight there's no time to listen to it;
     * the distance hum carries side, distance and airborne. */
    case VK_F8:
        if (shift) { s_hp_tones = !s_hp_tones; speak(s_hp_tones ? L"Health tones on" : L"Health tones off", 1); }
        else { s_hum_on = !s_hum_on; speak(s_hum_on ? L"Distance hum on" : L"Distance hum off", 1); }
        break;
    }
}
