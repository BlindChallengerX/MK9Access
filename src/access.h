/* Shared declarations for MK9Access (screen reader mod for MK9 by Lauta). */
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "rex_mod.h"

#define MOD_VERSION "1.0.19"

extern wchar_t g_game_dir[MAX_PATH];   /* folder of mk9.exe, with trailing backslash */
extern wchar_t g_mod_dir[MAX_PATH];    /* g_game_dir + "MK9Access\" */
extern const RexModHost *g_host;       /* port plugin host (rex_mod.h); NULL when started without the plugin */

void log_line(const char *fmt, ...);
char *to_utf8(const wchar_t *w);       /* malloc'd; caller frees */
wchar_t *from_utf8(const char *s);     /* malloc'd; caller frees */
int game_has_focus(void);

/* Game threads currently inside mod hooks; reload waits for zero. */
extern volatile LONG g_inflight;
#define CALL_ORIG(o, c, b) do { InterlockedIncrement(&g_inflight); (o)(c, b); InterlockedDecrement(&g_inflight); } while (0)

/* speech.c */
void speech_init(void);
void speech_shutdown(void);
void speak(const wchar_t *text, int interrupt);
void speak_utf8(const char *text, int interrupt);
void speakf(int interrupt, const char *fmt, ...);
void speak_repeat(void);
int speech_force(int on);              /* this thread's speech gets through during a fight (keys, tower objectives) */
void speech_stop(void);

/* guest.c: the Xbox 360 memory and the recompiled game code.
 * Guest memory is mapped at 0x100000000 (the log says "virtual base"), big-endian.
 * Recompiled functions: table at image base + image size, 8-byte host pointers
 * indexed by (guest address - CODE_BASE) / 4 (the log's "Function table initialized"). */
extern uintptr_t g_guest_base;
#define CODE_BASE    0x82360000u
#define CODE_END     0x83376864u
#define FTABLE       0x83560000u

typedef struct {
    uint64_t r3, r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15,
             r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31;
} PPCContext;
typedef void (*guest_fn)(PPCContext *ctx, uint8_t *base);

static inline uint8_t *gptr(uint32_t a) { return (uint8_t *)(g_guest_base + a + (a >= 0xE0000000u ? 0x1000 : 0)); }
int  guest_ok(uint32_t a, uint32_t len);
uint32_t g32(uint32_t a);
uint16_t g16(uint32_t a);
uint8_t  g8(uint32_t a);
float    gf(uint32_t a);
void     w32(uint32_t a, uint32_t v);
void    *guest_host_fn(uint32_t guest);
uint32_t guest_fn_of_host(uintptr_t rip);
int  guest_ready(void);
int  guest_hook(uint32_t guest, void *detour, void **orig);
void guest_hooks_apply(void);
int  guest_hooks_bad(void);
void tools_init(void);
void tools_shutdown(void);
void update_init(void);                /* update.c: port updates (download now, install on close) */
void update_announce_start(void);
void update_poll(void);
void tools_poll(void);                 /* RE commands from MK9Access\cmd.txt */

/* pad.c */
void pad_init(void);
void pad_shutdown(void);
void pad_inject(const char *names, int ms);
void pad_pin(uint32_t addr, uint32_t val, int ms);
uint16_t pad_ui_take_edges(void);
int pad_motion(int user, DWORD since, char *out, int cap);   /* recent directions L R U D + attack buttons 1-4 */
extern volatile uint16_t g_pad_buttons;
extern volatile LONG g_pad_moves;
extern volatile uint8_t g_pad_lt, g_pad_rt;
extern volatile int16_t g_pad_lx, g_pad_ly, g_pad_rx, g_pad_ry;

/* keys.c: keyboard layouts (Shift+F12: port keys / original PC keys) */
void keys_init(void);
void keys_toggle(void);
void keys_game_thread(void);
int keys_pc_on(void);
void keys_poll(void);                  /* hooks the game window's messages (PC layout: right Shift/Ctrl, Enter/Backspace in fights) */
void keys_shutdown(void);
int keys_for_button(const char *token, char *out, size_t cap);
int lang_code(void);                   /* the game's language: 1 English, 5 Spanish, 9 Portuguese, 4 French, 3 German, 6 Italian */
const wchar_t *lang_file_ext(void);    /* its Coalesced.* extension: eng, spa, ... */
void lang_cycle(int back);             /* Ctrl+F12 (Ctrl+Shift+F12 back): the game's language at the next start */
const char *lang_or(void);             /* " or " in the game's language, between its choice words */
void game_text_utf8(char *s, size_t cap);   /* menu.c: a game text (Windows-1252) as UTF-8, in place */

/* imgui.c: text of the port's own screens */
void im_init(void);
void im_shutdown(void);
void im_dump(void);
void im_poll(void);
int im_menu_on(void);
extern const wchar_t k_name_hint[];
extern volatile int g_loaded_said;     /* "MK9 Access loaded" has been said */
int im_name_prompt_open(void);         /* the port's Online username window is showing */
int im_read_screen(void);
int im_tabs_on(void);                  /* a port window with tabs is open (F2 options, F1 online) */
void im_tab_step(int dir);             /* Ctrl+Tab (+1) / Ctrl+Shift+Tab (-1): the next / previous tab */
const char *im_rival_name(void);       /* online session: the rival's username ("" before the corner showed it) */
int im_practice_record_on(void);       /* Practice: the port's opponent-recording panel (F5/F6/F7 are the port's) */
int im_practice_nothing_recorded(void);
void im_frame_data_key(int shift);
void fd_frame(void);                   /* game thread, at each pad read: measures attacks for the recovery */      /* F10: the Practice frame data panel */

/* movelist.c */
void ml_spoken(const char *in, char *out, size_t cap);
int ml_load(const char *fighter);
void ml_step(int dir);

/* health.c */
void selfcheck_log_versions(void);
void selfcheck_fail(const char *what);
int selfcheck_failed(void);
void selfcheck_poll(void);

/* audio.c: tones and continuous hums on a waveOut stream */
enum { W_SINE, W_SQUARE, W_TRI, W_SAW, W_NOISE, W_WIND, W_REED, W_SOFT };
void audio_init(void);
void audio_shutdown(void);
void audio_stat(void);
void audio_rec(int seconds);             /* development: record the mix to MK9Access\audiorec.wav */
void audio_rec_poll(void);
void tone(float f0, float f1, float seconds, float gain, float pan, int wave);
void tone_at(float delay, float f0, float f1, float seconds, float gain, float pan, int wave);
void hum_set(int slot, float freq, float gain, float pan, int wave);
void hum_trem(int slot, float depth);
void hum_all_off(void);
void click_set(float rate, float gain, float pan, float pitch);   /* distance clicks; rate 0 stops */
void clip_play(const wchar_t *path, float gain);   /* play a recorded WAV (e.g. a fatality description) over ducked cues */
void clip_stop(void);
int clip_playing(void);
int sfx_load(const wchar_t *file);     /* MK9Access\sounds\<file>: id or -1 */
void sfx_play(int id, float delay, float gain, float pan, float rate);
extern volatile LONG g_speak_count;    /* every message queued for speech */
extern volatile float g_master;

/* fight.c: cues during a fight */
void fight_poll(void);
void fight_round_over(void);             /* "<Fighter> WINS!": speech free until the next round */
/* fight.c: the settings the accessibility menu shows */
enum { FS_CUE, FS_CUE_VOL, FS_HEALTH, FS_XRAY, FS_HEIGHT, FS_COMBO, FS_SPEECH, FS_DESC, FS_DESC_VOL, FS_FDESC, FS_VOLUME };
int fighter_desc_auto(void);              /* character select: a fighter's look after resting on them */
int fight_game_state(void);              /* 7 character select, 8 fighting, ... */
void menu_fighter_describe(void);        /* F6 at character select */
void fset_value(int id, char *out, size_t cap);
int fset_step(int id, int d);            /* 0 at the end of a range */
void fset_preview(int id);
void fset_height_learn(void);
void fset_save(void);
/* a11ymenu.c: the accessibility menu (F4, Back + A) */
int a11y_menu_open(void);
void a11y_menu_key(int vk);              /* game window thread: a key press while it is open */
void a11y_menu_poll(void);
void a11y_menu_toggle(void);             /* F4 */
/* imgui.c */
int im_practice_on(void);
int im_fd_auto(void);
void im_fd_auto_set(int on);
/* keys.c */
void lang_next_name(char *out, size_t cap);
void practice_guard_say(void);           /* Practice F8: say the port's new "your fighter while recording" */
void keys_focus_kick(const char *why);   /* SDL takes the game window's focus again (dead keys after a .cmd start) */
void fight_ending(const char *sec);     /* a Ladder ending or the credits: cues held until the fight state ends */
void fight_hold(void);                   /* a challenge result: no fight cues behind its Continue menu */
void fight_resume(void);                 /* something loads: cues allowed again */
int fight_live(void);                    /* a fight is being played: not paused, no port window, no result menu */
int fight_quiet(void);                   /* a fight is on: the screen reader stays silent unless forced */

/* towerlight.c: Challenge Tower "Ancestral Light": where the light is and whether the opponent stands in it */
void light_lookup(const char *name, uint32_t script);   /* game thread: a script function looked up by name */
void light_reset(void);
void light_poll(float mx, float ox, float vol);
void light_quiet(void);
/* minigame.c: Test Your Might / Strike meter tones, Test Your Sight tracking */
void mg_resolve(uint32_t holder, const char *path);
void mg_invoke(uint32_t holder, const char *p, const char *arg);
void mg_sight(int what);               /* 1 Begin prompt, 2 Begin pressed, 3 select phase, 4 ended */
void mg_poll(void);
void mg_reset(void);                    /* a menu came up: any minigame cue stops */
void fight_key(int vk, int shift, int ctrl);
/* the round winner as a player: an online username, or "Player 2" in local 2-player Versus; 0 = say the character */
int fight_winner_name(char *out, size_t cap);

/* menu.c: the game's own menus (Localize hook) */
int menu_local_versus(void);           /* a local 2-player Versus Mode match was chosen */
int krypt_key(int vk, int shift);      /* menu.c: F4 in the Krypt; 0 when not in the Krypt */
void fatality_init(void);
int fatality_try(const char *text);   /* a fatality name -> speak name + input; 1 if handled */
void fatality_input_settext(uint32_t obj, const char *val);   /* tutorial input display -> detect the fatality performed */
void fatality_poll(void);              /* live matches: Finish Him, fatality cinematic -> described fatality */
int fatality_finish_him(void);         /* Finish Him on, fatality not started (keep the distance hum) */
void menu_init(void);
void menu_shutdown(void);
void menu_poll(void);
void menu_debug(int on);
const char *loc_get(const char *section, const char *key);
void text_tidy(const char *in, char *out, size_t cap);   /* game text -> speech (Title Case for ALL CAPS) */

/* sysdialog.c: the runtime's system dialogs (XamShowMessageBoxUI), spoken and answerable */
void sysdialog_init(void);
void sysdialog_shutdown(void);
void sysdialog_poll(void);
int sysdialog_recent(void);            /* a system dialog is up, or was shown in the last 2 s */

/* Descriptions (story movies, fatalities): settings in fight.c, desc_play() in story.c */
enum { DESC_VOICE, DESC_READER, DESC_OFF, DESC_N };
int desc_mode(void);                     /* recorded voice, screen reader, or off */
float desc_gain(void);                   /* the recorded voice's volume */
void desc_key(int shift);                /* Alt+F12 mode, Alt+Shift+F12 volume */
/* one description: its clip by the recorded voice, or its text by the screen reader (queued), or nothing;
 * returns 1 when the clip plays */
int desc_play(const wchar_t *clip, const char *text);

/* story.c: Story Mode movies described (MK9Access\story) */
void story_init(void);
void story_poll(void);
int story_playing(void);
void story_stage(char *out, size_t cap);   /* the fight's stage package name ("Pit", "DeadPool"), "" before one */

/* game.c */
void game_init(void);
void game_shutdown(void);
void game_poll(void);
void game_key(int vk, int shift, int ctrl);
