/* MK9-specific readers and the mod's keys. */
#include "access.h"

void game_init(void) { keys_init(); audio_init(); im_init(); menu_init(); fatality_init(); sysdialog_init(); story_init(); }
void game_shutdown(void) { keys_shutdown(); sysdialog_shutdown(); im_shutdown(); menu_shutdown(); hum_all_off(); audio_shutdown(); }
void game_poll(void) { keys_poll(); im_poll(); menu_poll(); fight_poll(); mg_poll(); fatality_poll(); story_poll(); sysdialog_poll(); audio_rec_poll(); }

void game_key(int vk, int shift, int ctrl) {
    switch (vk) {
    /* In Practice the port records the opponent on F5, plays it back on F6 and loops it on F7 (port 1.4.2): those
     * presses go to the port there, and imgui.c speaks what changed. */
    case VK_F5: if (!im_practice_record_on()) im_read_screen(); break;   /* read the whole screen */
    case VK_F10: im_frame_data_key(shift); break;  /* Practice frame data */
    case VK_F4: krypt_key(vk, shift); break;      /* in the Krypt: koffins in view */
    case VK_F6:
        if (im_practice_nothing_recorded()) speak(L"Nothing recorded yet. F5 records the opponent.", 1);
        else if (!im_practice_record_on()) fight_key(vk, shift, ctrl);
        break;
    case VK_F8: fight_key(vk, shift, ctrl); break;
    case VK_F12:              /* repeat last; Shift: keyboard layout; Ctrl: game language; Alt: descriptions */
        if (GetAsyncKeyState(VK_MENU) < 0) desc_key(shift);
        else if (ctrl) lang_cycle(shift); else if (shift) keys_toggle(); else speak_repeat();
        break;
    default: break;
    }
}
