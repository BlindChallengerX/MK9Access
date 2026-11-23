/*
 * Test Your Might / Test Your Strike meters and Test Your Sight (Fight menu, also in the Challenge Tower).
 *
 * Meters (Flash, found 2026-11-20): the minigame clip resolves "meter.fill" and sets its frame every game
 * frame (Invoke gotoAndStop %d, about +23 per button press, -1 a frame). The target is a frame on the same
 * scale: Might "meter.pointPosition.gotoAndStop" (157 on Pine Boards; 161 at the trigger won, 75 lost),
 * Strike "meter.pointCell.gotoAndStop" (235, the middle of the band to hold). The meter runs from
 * "gotoAndPlay buildOn" on the minigame clip to "meter.fill.fill_loop.fill.gotoAndPlay lockIn" (Might,
 * trigger pressed or time up) / "... ready" (Strike, charged) / "gotoAndPlay buildOff".
 * Sound, one meaning per channel (spatial-audio-kit):
 *   steady tone, slot 3: pitch = the fill, 220 Hz empty, 440 Hz at the target, up to 880 Hz
 *   short reference pip at 440 Hz every 0.8 s: the target's pitch, match it (the user's gap-jump model)
 *   tremolo on the tone: above the marker (Might only; Strike's band glow stays "idle", match the pip there)
 *   rising ding: crossed above the marker; falling tick: dropped below it
 *   bright double ding + "Charged": Strike is charged, press the trigger
 *
 * Test Your Sight (3D, guest memory, found 2026-11-20 on the heads table; UE3 actor Location at +0xDC =
 * depth, side, height): the heads are actors of class vtable 0x820b4148 at height 100.0 (each has a twin
 * at 100.2 that moves with it); on Begin they lift to ~110 over the eyeball, come down, and swap places
 * in pairs, side -110 (left), -90, -70 (right). The eyeball is a class 0x8249a188 actor at the start side
 * of the right head (always the middle on challenge 1). The spotlight is class 0x82152140 (depth 62.5):
 * its side is the head picked with Left/Right. Predicting by following the eyeball's head won 3 of 3 picks.
 * Sound: after the lift, "The eye is under the middle head"; then a soft tone panned with that head
 * through the shuffle (what a sighted player follows by eye); off at "Select Cup". In the selection a
 * panned tick and "left" / "middle" / "right" for the spotlight.
 * Other tables (cups, skulls) are not verified: if no heads are found nothing is played.
 */
#include "access.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_MG 3
static uint32_t bswap(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24); }
static float rdf(uint32_t a) { uint32_t u = g32(a); float f; memcpy(&f, &u, 4); return f; }

/* ---------------- meters ---------------- */
static volatile uint32_t s_fill_h;
static volatile int s_fill = -1, s_target = -1, s_strike, s_on, s_charged;
static volatile DWORD s_target_t;
static int s_above, s_was_on;
static DWORD s_pip_t;

void mg_resolve(uint32_t holder, const char *path) {
    if (!strcmp(path, "meter.fill")) s_fill_h = holder;
}

/* every Invoke (game thread): p = path.method, arg = its first value */
void mg_invoke(uint32_t holder, const char *p, const char *arg) {
    if (s_fill_h && holder == s_fill_h && !strcmp(p, "gotoAndStop")) { s_fill = atoi(arg); return; }
    if (strncmp(p, "meter", 5) && strcmp(p, "gotoAndPlay")) return;
    if (!strcmp(p, "meter.pointPosition.gotoAndStop")) { s_target = atoi(arg); s_strike = 0; s_target_t = GetTickCount(); }
    else if (!strcmp(p, "meter.pointCell.gotoAndStop")) { s_target = atoi(arg); s_strike = 1; s_target_t = GetTickCount(); }
    else if (!strcmp(p, "meter.fill.fill_loop.fill.gotoAndPlay")) {
        if (!_stricmp(arg, "lockIn")) s_on = 0;
        else if (!_stricmp(arg, "ready")) { s_charged = 1; s_on = 0; }
    } else if (!strcmp(p, "gotoAndPlay")) {
        if (!_stricmp(arg, "buildOn") && s_target > 0 && GetTickCount() - s_target_t < 30000) { s_on = 1; s_charged = 0; s_fill = 0; }
        else if (!_stricmp(arg, "buildOff")) s_on = 0;
    }
}

static void meter_poll(void) {
    if (s_charged) {
        s_charged = 0;
        tone(880, 880, 0.07f, 0.10f, 0, W_SINE); tone_at(0.09f, 1320, 1320, 0.10f, 0.10f, 0, W_SINE);
        speak(L"Charged", 0);
    }
    if (!s_on) {
        if (s_was_on) { hum_set(SLOT_MG, 220, 0, 0, W_TRI); hum_trem(SLOT_MG, 0); s_was_on = 0; s_above = 0; }
        return;
    }
    s_was_on = 1;
    float r = s_target > 0 && s_fill > 0 ? (float)s_fill / (float)s_target : 0;
    if (r > 2) r = 2;
    hum_set(SLOT_MG, 220.0f * powf(2.0f, r), 0.06f, 0, W_TRI);
    /* Strike: the band's glow stays "idle" the whole time (checked 2026-11-20), so there is no in-band flag:
     * the pitch matching the reference pip is the cue there. */
    int good = !s_strike && r >= 1.0f;
    hum_trem(SLOT_MG, good ? 0.5f : 0);
    if (!s_strike) {
        if (good && !s_above) tone(660, 990, 0.08f, 0.09f, 0, W_SINE);
        else if (!good && s_above) tone(330, 250, 0.06f, 0.06f, 0, W_SINE);
    }
    s_above = good;
    DWORD now = GetTickCount();
    if (now - s_pip_t > 800) { s_pip_t = now; tone(440, 440, 0.04f, 0.035f, 0, W_SINE); }
}

/* ---------------- Test Your Sight ---------------- */
#define VT_HEAD  0x820b4148u
#define VT_EYE   0x8249a188u
#define VT_SPOT  0x82152140u
#define A_LOC    0xDCu
#define MAXC 32
enum { TS_IDLE, TS_ARMED, TS_RUN, TS_SELECT };
static volatile int s_ts_req;          /* requests from the game thread: 1 armed, 2 begin pressed, 3 select, 4 end */
static int s_ts = TS_IDLE;
static uint32_t s_cand[MAXC]; static float s_cand_y0[MAXC]; static int s_ncand;
static uint32_t s_head[8]; static float s_head_y0[8]; static int s_nhead;
static uint32_t s_spot; static float s_eye_y = 1e9f;
static int s_target_head = -1, s_said_eye, s_spot_slot = -1;
static DWORD s_run_t;
static float s_ymin, s_ymax;

void mg_sight(int what) { s_ts_req = what; }

/* every actor of class vt in the guest heap (16-byte aligned objects), host-side scan of committed pages */
static int scan_class(uint32_t vt, uint32_t *out, int max) {
    int n = 0;
    uint32_t want = bswap(vt);
    uint8_t *lo = gptr(0x40000000u), *hi = gptr(0x50000000u), *a = lo;
    while (a < hi && n < max) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery(a, &m, sizeof m)) break;
        uint8_t *end = (uint8_t *)m.BaseAddress + m.RegionSize;
        if (end > hi) end = hi;
        if (m.State == MEM_COMMIT && (m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))
            && !(m.Protect & PAGE_GUARD)) {
            for (uint8_t *p = a; p + 16 <= end && n < max; p += 16)
                if (*(uint32_t *)p == want) out[n++] = (uint32_t)(0x40000000u + (p - lo));
        }
        a = end;
    }
    return n;
}

static int on_table(uint32_t a) {
    float x = rdf(a + A_LOC), z = rdf(a + A_LOC + 8);
    return x > 40 && x < 100 && z > 50 && z < 130;
}

static const char *side_word(float y) {
    if (s_ymax - s_ymin < 1) return "";
    float k = (y - s_ymin) / (s_ymax - s_ymin);
    if (s_nhead == 3 || s_nhead == 0) return k < 0.25f ? "left" : k > 0.75f ? "right" : "middle";
    return k < 0.5f ? "left" : "right";
}

static float side_pan(float y) {
    if (s_ymax - s_ymin < 1) return 0;
    float k = (y - s_ymin) / (s_ymax - s_ymin) * 2 - 1;
    return 0.8f * (k < -1 ? -1 : k > 1 ? 1 : k);
}

static void sight_arm(void) {
    uint32_t buf[MAXC * 4];
    int n = scan_class(VT_HEAD, buf, MAXC * 4);
    s_ncand = 0;
    for (int i = 0; i < n && s_ncand < MAXC; i++) {
        float z = rdf(buf[i] + A_LOC + 8);
        if (on_table(buf[i]) && z > 99.9f && z < 100.1f) { s_cand[s_ncand] = buf[i]; s_cand_y0[s_ncand++] = rdf(buf[i] + A_LOC + 4); }
    }
    s_eye_y = 1e9f;
    n = scan_class(VT_EYE, buf, MAXC * 4);
    for (int i = 0; i < n; i++) if (on_table(buf[i])) { s_eye_y = rdf(buf[i] + A_LOC + 4); break; }
    s_spot = 0;                          /* the spotlight is made when the pick starts (find_spot) */
    s_nhead = 0; s_target_head = -1; s_said_eye = 0; s_spot_slot = -1;
    log_line("sight: %d head candidates, eye side %.1f, spotlight %08x", s_ncand, s_eye_y, s_spot);
}

static DWORD s_spot_try;
static void find_spot(void) {
    uint32_t buf[MAXC];
    int n = scan_class(VT_SPOT, buf, MAXC);
    for (int i = 0; i < n; i++) { float x = rdf(buf[i] + A_LOC); if (x > 40 && x < 100) { s_spot = buf[i]; break; } }
    if (s_spot) log_line("sight: spotlight %08x", s_spot);
}

static void sight_poll(void) {
    int req = s_ts_req; s_ts_req = 0;
    if (req == 1) { sight_arm(); s_ts = TS_ARMED; }
    else if (req == 2 && s_ts == TS_ARMED) { s_ts = TS_RUN; s_run_t = GetTickCount(); }
    else if (req == 3 && s_ts == TS_RUN) { s_ts = TS_SELECT; hum_set(SLOT_MG + 1, 330, 0, 0, W_SOFT); }
    else if (req == 4) { s_ts = TS_IDLE; hum_set(SLOT_MG + 1, 330, 0, 0, W_SOFT); }
    if (s_ts == TS_RUN) {
        DWORD dt = GetTickCount() - s_run_t;
        if (dt < 2500)                                  /* the heads lift off the table: that marks them */
            for (int i = 0; i < s_ncand; i++) {
                int known = 0;
                for (int j = 0; j < s_nhead; j++) known |= s_head[j] == s_cand[i];
                if (!known && s_nhead < 8 && rdf(s_cand[i] + A_LOC + 8) > 104) { s_head[s_nhead] = s_cand[i]; s_head_y0[s_nhead++] = s_cand_y0[i]; }
            }
        else if (!s_said_eye) {
            s_said_eye = 1;
            if (s_nhead < 2) { log_line("sight: heads not found (%d)", s_nhead); s_ts = TS_IDLE; return; }
            s_ymin = s_ymax = s_head_y0[0];
            for (int j = 1; j < s_nhead; j++) { if (s_head_y0[j] < s_ymin) s_ymin = s_head_y0[j]; if (s_head_y0[j] > s_ymax) s_ymax = s_head_y0[j]; }
            float best = 1e9f;
            for (int j = 0; j < s_nhead; j++) if (fabsf(s_head_y0[j] - s_eye_y) < best) { best = fabsf(s_head_y0[j] - s_eye_y); s_target_head = j; }
            if (s_target_head >= 0 && best < 5) {
                char m[96]; snprintf(m, sizeof m, "The eye is under the %s head. Follow its sound.", side_word(s_head_y0[s_target_head]));
                speak_utf8(m, 0);
            } else s_target_head = -1;
            log_line("sight: %d heads, eye under head %d", s_nhead, s_target_head);
        }
        if (s_target_head >= 0) {
            float y = rdf(s_head[s_target_head] + A_LOC + 4);
            hum_set(SLOT_MG + 1, 330, 0.07f, side_pan(y), W_SOFT);
        }
    } else if (s_ts == TS_SELECT && !s_spot) {
        if (GetTickCount() - s_spot_try > 400) { s_spot_try = GetTickCount(); find_spot(); }
    } else if (s_ts == TS_SELECT && s_spot) {
        float y = rdf(s_spot + A_LOC + 4);
        float k = s_ymax - s_ymin > 1 ? (y - s_ymin) / (s_ymax - s_ymin) : 0.5f;
        int slot = (int)lroundf(k * 4);                 /* quarter steps: left 0, middle 2, right 4 */
        if (slot != s_spot_slot && fabsf(k * 4 - slot) < 0.2f) {   /* settled on a head, not sliding between */
            char m[64]; snprintf(m, sizeof m, "%s%s", s_spot_slot < 0 ? "Spotlight on the " : "", side_word(y));
            tone(700, 700, 0.03f, 0.06f, side_pan(y), W_SINE);
            speak_utf8(m, s_spot_slot >= 0);
            s_spot_slot = slot;
        }
    }
}

void mg_poll(void) { meter_poll(); sight_poll(); }
