/*
 * Self checks, so a port update cannot crash the game through the mod.
 * The Xbox 360 game code and data never change between port updates; the
 * port's plumbing can (where the function table lives, how registers are
 * passed). guest.c checks the table and hook targets at startup; if they are
 * wrong the mod switches itself off and says so.
 */
#include "access.h"
#include <stdio.h>
#include <string.h>

static int g_failed;

void selfcheck_log_versions(void) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsmk9.exe", g_game_dir);
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &a))
        log_line("port: mk9.exe %lu bytes", (unsigned long)a.nFileSizeLow);
}

void selfcheck_fail(const char *what) {
    if (g_failed) return;
    g_failed = 1;
    log_line("SELF CHECK FAILED: %s", what);
    speakf(0, "MK9 Access: %s. The game port may have been updated. The mod has switched itself off so the game keeps "
              "running. Please look for an MK9 Access update.", what);
}

int selfcheck_failed(void) { return g_failed; }
void selfcheck_poll(void) {}
