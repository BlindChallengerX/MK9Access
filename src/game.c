/* MK9-specific readers and the mod's keys. */
#include "access.h"

void game_init(void) { keys_init(); audio_init(); im_init(); menu_init(); fatality_init(); sysdialog_init(); story_init(); }
void game_shutdown(void) { keys_shutdown(); sysdialog_shutdown(); im_shutdown(); menu_shutdown(); hum_all_off(); audio_shutdown(); }
void game_poll(void) { keys_poll(); a11y_menu_poll(); im_poll(); menu_poll(); fight_poll(); mg_poll(); fatality_poll(); story_poll(); sysdialog_poll(); audio_rec_poll(); }

void game_key(int vk, int shift, int ctrl) {
    switch (vk) {
    /* In Practice the port records the opponent on F5, plays it back on F6 and loops it on F7 (port 1.4.2): those
     * presses go to the port there, and imgui.c speaks what changed. */
    case VK_F5: if (!im_practice_record_on()) im_read_screen(); break;   /* read the whole screen */
    case VK_F10: im_frame_data_key(shift); break;  /* Practice frame data */
    /* F4: the accessibility menu (not Alt+F4, which closes the game). In the Krypt: Shift+F4 koffins in view,
     * Ctrl+F4 koffin pings on / off (until 1.0.13 F4 and Shift+F4) */
    case VK_F4:
        if (GetAsyncKeyState(VK_MENU) < 0) break;
        if (shift || ctrl) { if (!krypt_key(vk, ctrl)) speak(L"Koffins are listed in the Krypt", 1); }
        else a11y_menu_toggle();
        break;
    case VK_F6:
        if (im_practice_nothing_recorded()) speak(L"Nothing recorded yet. F5 records the opponent.", 1);
        else if (!im_practice_record_on()) fight_key(vk, shift, ctrl);
        break;
    /* plain F8 in a Practice fight belongs to the port since 1.4.4: your fighter while recording (stands, blocks
     * standing, blocks crouching); the mod says what it became. The distance cue is in the menu (F4). */
    case VK_F8:
        if (!shift && !ctrl && GetAsyncKeyState(VK_MENU) >= 0 && im_practice_record_on()) practice_guard_say();
        else fight_key(vk, shift, ctrl);
        break;
    case VK_F12:              /* repeat last; Shift: keyboard layout; Ctrl: game language; Alt: descriptions */
        if (GetAsyncKeyState(VK_MENU) < 0) desc_key(shift);
        else if (ctrl) lang_cycle(shift); else if (shift) keys_toggle(); else speak_repeat();
        break;
    default: break;
    }
}
