/*
 * The accessibility menu: every MK9 Access setting in one list (user request 2026-11-27, after TBlaze's MKX Access).
 * F4 opens and closes it (Insert was the first choice, but NVDA and JAWS take Insert); on a controller hold Back and press A (the port uses Back + Y for F1, Back + Start for
 * F2, Back + LB / RB / X and Back + left / right in Practice). Up and Down choose, Left and Right change (saved at
 * once, and the setting plays its sound at the new value), Enter or A plays the sound or chooses, Escape, B or
 * Back + A closes. While it is open the game gets no keys (keys.c message hook) and an idle controller (pad.c), and
 * the fight cues are hushed except for a preview (fight.c). The F8 / F12 chords still work as shortcuts.
 */
#include "access.h"
#include <stdio.h>
#include <string.h>

enum { MI_CUE, MI_CUE_VOL, MI_HEALTH, MI_HEIGHT, MI_COMBO, MI_SPEECH, MI_DESC, MI_DESC_VOL, MI_FRAMES, MI_LAYOUT,
       MI_LANG, MI_VOLUME, MI_CLOSE, MI_N };
static const char *k_label[MI_N] = {
    "Distance cue", "Distance cue volume", "Health tones", "Attack height sounds", "Combo readout",
    "Speech during fights", "Descriptions", "Description voice volume", "Frame data in Practice", "Keyboard layout",
    "Game language", "Overall volume", "Close menu",
};
/* the fight.c setting behind an item, or -1 */
static int fs_of(int i) {
    switch (i) {
    case MI_CUE: return FS_CUE;       case MI_CUE_VOL: return FS_CUE_VOL; case MI_HEALTH: return FS_HEALTH;
    case MI_HEIGHT: return FS_HEIGHT; case MI_COMBO: return FS_COMBO;     case MI_SPEECH: return FS_SPEECH;
    case MI_DESC: return FS_DESC;     case MI_DESC_VOL: return FS_DESC_VOL; case MI_VOLUME: return FS_VOLUME;
    default: return -1;
    }
}

static volatile int s_open;
static int s_item, s_hinted, s_by_pad;
int a11y_menu_open(void) { return s_open; }

/* keys from the game window's thread (keys.c), handled on the mod thread */
static volatile LONG s_qhead, s_qtail;
static volatile int s_q[32];
void a11y_menu_key(int vk) {
    LONG h = s_qhead;
    if (h - s_qtail >= 32) return;
    s_q[h & 31] = vk;
    InterlockedExchange(&s_qhead, h + 1);
}

static void say(const char *s) { int was = speech_force(1); speak_utf8(s, 1); speech_force(was); }

static void value_of(int i, char *out, size_t cap) {
    int fs = fs_of(i);
    out[0] = 0;
    if (fs >= 0) { fset_value(fs, out, cap); return; }
    switch (i) {
    case MI_FRAMES: snprintf(out, cap, "%s", im_fd_auto() ? "after every attack" : "on F10 only"); break;
    case MI_LAYOUT: snprintf(out, cap, "%s", keys_pc_on() ? "original PC keys" : "port keys"); break;
    case MI_LANG: lang_next_name(out, cap); break;
    default: break;
    }
}

static void item_line(int i, char *out, size_t cap) {
    char v[96]; value_of(i, v, sizeof v);
    snprintf(out, cap, "%s%s%s, %d of %d", k_label[i], v[0] ? ", " : "", v, i + 1, MI_N);
}

static void snd_move(void) { tone_at(0, 1047, 1047, 0.025f, 0.05f, 0, W_SOFT); }
static void snd_edge(void) { tone_at(0, 196, 185, 0.07f, 0.09f, 0, W_SOFT); }

static void menu_open(void) {
    s_open = 1;
    tone_at(0, 523, 523, 0.06f, 0.08f, 0, W_SOFT);
    tone_at(0.06f, 784, 784, 0.08f, 0.08f, 0, W_SOFT);
    char line[600], it[200];
    item_line(s_item, it, sizeof it);
    snprintf(line, sizeof line, "MK9 Access %s settings. %s%s", MOD_VERSION,
             s_hinted ? "" : s_by_pad ? "Up and Down choose, Left and Right change, A plays the sound, B closes. "
                                     : "Up and Down choose, Left and Right change, Enter plays the sound, Escape closes. ", it);
    s_hinted = 1;
    say(line);
    log_line("a11y menu: open");
}

static void menu_close(void) {
    s_open = 0;
    tone_at(0, 784, 784, 0.06f, 0.08f, 0, W_SOFT);
    tone_at(0.06f, 523, 523, 0.08f, 0.08f, 0, W_SOFT);
    say("Settings closed");
    log_line("a11y menu: closed");
}

static void menu_move(int d) {
    int n = s_item + d;
    if (n < 0 || n >= MI_N) { snd_edge(); n = (n + MI_N) % MI_N; } else snd_move();
    s_item = n;
    char line[200]; item_line(s_item, line, sizeof line); say(line);
}

static void menu_jump(int to) {
    s_item = to; snd_move();
    char line[200]; item_line(s_item, line, sizeof line); say(line);
}

/* d: -1 / +1 = Left / Right; d == 0: Enter */
static void menu_change(int d) {
    int i = s_item, fs = fs_of(i);
    char v[96], line[160];
    if (!d) {                                       /* Enter: listen, or choose */
        if (i == MI_CLOSE) { menu_close(); return; }
        if (i == MI_HEIGHT) { fset_height_learn(); return; }
        if (fs >= 0) fset_preview(fs);
        return;
    }
    if (fs >= 0) {
        if (!fset_step(fs, d)) { snd_edge(); value_of(i, v, sizeof v); say(v); return; }   /* at its end */
        value_of(i, v, sizeof v); say(v);
        fset_preview(fs);
        log_line("a11y menu: %s = %s", k_label[i], v);
        return;
    }
    switch (i) {
    case MI_FRAMES:
        im_fd_auto_set(!im_fd_auto()); fset_save();
        value_of(i, v, sizeof v); say(v);
        break;
    case MI_LAYOUT: keys_toggle(); break;           /* says the new keys itself */
    case MI_LANG: lang_cycle(d < 0); break;         /* says it in that language, for the next start */
    case MI_CLOSE: snd_edge(); snprintf(line, sizeof line, "%s", k_label[i]); say(line); return;
    default: return;
    }
    value_of(i, v, sizeof v);
    log_line("a11y menu: %s = %s", k_label[i], v);
}

static void key(int vk) {
    if (!s_open) return;
    switch (vk) {
    case VK_UP: case 'W': menu_move(-1); break;
    case VK_DOWN: case 'S': menu_move(1); break;
    case VK_LEFT: case 'A': menu_change(-1); break;
    case VK_RIGHT: case 'D': menu_change(1); break;
    case VK_HOME: menu_jump(0); break;
    case VK_END: menu_jump(MI_N - 1); break;
    case VK_RETURN: case VK_SPACE: case 'J': menu_change(0); break;
    case VK_ESCAPE: case VK_BACK: case 'K': menu_close(); break;
    default: break;
    }
}

/* the controller, from the buttons the game was given (pad.c keeps the raw ones in g_pad_buttons) */
static void pad(void) {
    static uint16_t was;
    static int stick_was;
    uint16_t b = g_pad_buttons, edge = (uint16_t)(b & ~was);
    was = b;
    int stick = g_pad_ly > 20000 ? 1 : g_pad_ly < -20000 ? 2 : g_pad_lx < -20000 ? 3 : g_pad_lx > 20000 ? 4 : 0;
    int sedge = stick != stick_was ? stick : 0;
    stick_was = stick;
    if ((b & 0x20) && (edge & 0x1000)) { if (s_open) menu_close(); else { s_by_pad = 1; menu_open(); } return; }   /* Back + A */
    if (!s_open) return;
    if ((edge & 0x1) || sedge == 1) menu_move(-1);
    if ((edge & 0x2) || sedge == 2) menu_move(1);
    if ((edge & 0x4) || sedge == 3) menu_change(-1);
    if ((edge & 0x8) || sedge == 4) menu_change(1);
    if ((edge & 0x1000) && !(b & 0x20)) menu_change(0);
    if (edge & 0x2000) menu_close();
}

void a11y_menu_poll(void) {
    while (s_qtail != s_qhead) {
        int vk = s_q[s_qtail & 31];
        InterlockedIncrement(&s_qtail);
        key(vk);
    }
    pad();
}

/* F4 (game.c, the mod thread) */
void a11y_menu_toggle(void) { if (s_open) menu_close(); else { s_by_pad = 0; menu_open(); } }
