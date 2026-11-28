/*
 * Challenge Tower 29, "Ancestral Light" (Nightwolf against Scorpion): the opponent only takes damage inside a
 * cone of light. Reported 2026-11-27: the light is only seen, so the challenge could not be done by ear.
 *
 * How the game decides (found 2026-11-28 on the test saves, tools notes in NOTES.md "Ancestral Light"):
 *   tower.mko (TowerBlock5) runs p_cone_mover, which keeps the light's spot in its script data:
 *     [script + 0x84] = the script's static data, + 0x27C = the spot's X (float, fight units like the
 *     fighters' actor +0xE0), + 0x284 = 7.47 while the light shines, 0 for about a second when it moves.
 *   Every 10-15 s the script takes the opponent's position at that moment as the new spot (so the light starts
 *   on them and they walk out of it). A hit counts when the opponent is within about 133 of the spot: of 62 hits
 *   measured, all 18 damaging ones were 137 or nearer, the 44 others 129 or farther (bot hits, 50 ms samples).
 *   The script is found when the game looks its functions up by name (82A10380, hooked in fatality.c): any
 *   "ancestral_light_*" or "p_cone_mover" lookup hands over the script object.
 *
 * Sound (one meaning per channel, like the distance cue):
 *   a soft shimmering tone      = the light; its pan is where the light is from me
 *   its pitch                   = how near the opponent is to the light (G6 and level once inside, an octave
 *                                 lower 400 away; above the clicks and the distance hum)
 *   two rising notes            = the opponent stepped into the light; two falling = out of it
 *   three quick rising notes    = the light moved to a new spot (from where it is now)
 * The new spot is sometimes the opponent's position, sometimes mine, at the moment of the move.
 * Verified 2026-11-28 by bot on the test saves: script found on every load and Retry, spot moves logged, the
 * bot's damaging hits fell while the cue said "in" (in/out at 3-114 / 130-279 from the spot); mix peaks -14 to
 * -21 dBFS with the tone (audiorec). Not done: Healing Light and Seek The Warmth (other blocks, other scripts).
 */
#include "access.h"
#include <math.h>
#include <string.h>

#define SLOT_LIGHT 5
#define S_DATA     0x84u
#define D_SPOT     0x27Cu
#define D_SHINE    0x284u
#define R_IN       115.0f        /* inside: comfortably within the measured 129-137 edge */
#define R_OUT      130.0f

static volatile uint32_t s_script;    /* set on the game thread by the lookup hook */
static uint32_t s_used;                /* the script the cue runs from */
static int s_on, s_inside, s_told, s_want;
static float s_spot;
static DWORD s_used_t, s_want_t, s_chime_t;

static float rdf(uint32_t a) { uint32_t u = g32(a); float f; memcpy(&f, &u, 4); return f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* game thread (script lookup hook): a function of the light challenge was looked up in this script */
void light_lookup(const char *name, uint32_t script) {
    if (!_strnicmp(name, "ancestral_light", 15) || !strcmp(name, "p_cone_mover")) s_script = script;
}

/* a menu came up (tower card, main menu): whatever challenge ran is over */
void light_reset(void) { s_script = 0; }

static void off(void) {
    if (s_on) { hum_set(SLOT_LIGHT, 660, 0, 0, W_SINE); hum_trem(SLOT_LIGHT, 0); s_on = 0; }
}

/* fight.c, every poll while a fight is live and not paused; mx / ox = my and the opponent's positions */
void light_poll(float mx, float ox, float vol) {
    uint32_t scr = s_script;
    if (!scr || !guest_ok(scr + S_DATA, 4)) { off(); return; }
    uint32_t data = g32(scr + S_DATA);
    if (data < 0x40000000u || !guest_ok(data + D_SPOT, 12)) { off(); return; }
    float spot = rdf(data + D_SPOT), shine = rdf(data + D_SHINE);
    if (!isfinite(spot) || fabsf(spot) > 3000 || !isfinite(shine)) { off(); return; }
    DWORD now = GetTickCount();
    if (scr != s_used) {
        s_used = scr; s_inside = s_want = 0; s_spot = spot; s_used_t = now;
        log_line("light: Ancestral Light script %08x, data %08x, spot %.0f", scr, data, spot);
    }
    /* once per game, after the challenge's own objective and narrator line (they come 3-5 s after the load) */
    if (!s_told && now - s_used_t > 7000) {
        s_told = 1;
        int was = speech_force(1);
        speak_utf8("Light sound: the shimmer is the light. It rises as your opponent nears it, and chimes when they are in it.", 1);
        speech_force(was);
    }
    float pan = 0.8f * clampf((spot - mx) / 200.0f, -1, 1);
    if (fabsf(spot - s_spot) > 30) {                 /* the light moved on */
        log_line("light: moved %.0f -> %.0f (opponent at %.0f)", s_spot, spot, ox);
        tone_at(0.00f, 1568, 1568, 0.06f, 0.045f * vol, pan, W_SINE);
        tone_at(0.07f, 1976, 1976, 0.06f, 0.045f * vol, pan, W_SINE);
        tone_at(0.14f, 2349, 2349, 0.09f, 0.045f * vol, pan, W_SINE);
        s_spot = spot;
    }
    /* in or out: with a margin (115 in, 130 out) and held 150 ms, so a fighter rocking at the edge does not chatter;
     * the notes at most every 0.6 s */
    float d = fabsf(ox - spot);
    int want = s_inside ? d <= R_OUT : d < R_IN;
    if (want != s_want) { s_want = want; s_want_t = now; }
    if (s_want != s_inside && now - s_want_t >= 150) {
        s_inside = s_want;
        log_line("light: opponent %s the light (%.0f from it)", s_inside ? "in" : "out of", d);
        if (now - s_chime_t >= 600) {
            s_chime_t = now;
            if (s_inside) { tone_at(0, 1047, 1047, 0.06f, 0.06f * vol, pan, W_SINE); tone_at(0.07f, 1568, 1568, 0.09f, 0.06f * vol, pan, W_SINE); }
            else { tone_at(0, 1047, 1047, 0.06f, 0.05f * vol, pan, W_SINE); tone_at(0.07f, 784, 784, 0.09f, 0.05f * vol, pan, W_SINE); }
        }
    }
    /* level pitch inside (G6); outside it falls an octave over the next 400 - above the clicks (700/935 Hz) and the
     * distance hum (110-330 Hz) */
    float f = s_inside ? 1568.0f : 1568.0f * powf(0.5f, clampf((d - R_IN) / 400.0f, 0, 1));
    float g = (s_inside ? 0.05f : 0.035f) * vol;
    if (shine <= 0.01f) g *= 0.4f;                   /* moving to its next spot */
    hum_set(SLOT_LIGHT, f, g, pan, W_SINE);
    hum_trem(SLOT_LIGHT, 0.25f);                     /* a slow shimmer: not mistaken for the distance hum */
    s_on = 1;
}

void light_quiet(void) { off(); }
