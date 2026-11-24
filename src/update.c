/*
 * Port and mod updates. At every game start the mod runs MK9Access\mk9update.ps1
 * hidden with the game's process id (however the game was launched: the
 * launcher, the Arcade Cabinet or a bare mk9.exe). The script downloads a newer
 * mk9-lauta release and a newer MK9 Access release while the game runs and
 * installs them once the game closes. It reports through one status file each,
 * MK9Access\update_status.txt (port) and MK9Access\mod_update_status.txt (mod),
 * one line:
 *   "ready X.Y.Z"    downloaded, installs on close  -> spoken once this session
 *   "applied X.Y.Z"  installed                      -> spoken at the next start
 *   "failed X.Y.Z"   the copy failed, left as it was (retried next close)
 * The port's own banner is off (mk9_update_check = false): mouse-only here.
 * MK9A_NOUPDATE=1 skips it (test bots).
 */
#include "access.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wchar_t g_status_path[MAX_PATH], g_mod_status_path[MAX_PATH];
static int g_ready_said, g_mod_ready_said;
static DWORD g_last_poll, g_started;

static int read_status(const wchar_t *path, char *word, size_t wn, char *ver, size_t vn) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;
    char line[128] = { 0 };
    size_t n = fread(line, 1, sizeof line - 1, f);
    fclose(f);
    line[n] = 0;
    char w[32] = { 0 }, v[32] = { 0 };
    if (sscanf(line, "%31s %31s", w, v) != 2) return 0;
    snprintf(word, wn, "%s", w);
    snprintf(ver, vn, "%s", v);
    return 1;
}

static void start_script(void) {
    wchar_t script[MAX_PATH], game[MAX_PATH], ps[MAX_PATH], cmd[2048], port[32] = L"";
    swprintf(script, MAX_PATH, L"%lsmk9update.ps1", g_mod_dir);
    if (GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) { log_line("update: mk9update.ps1 missing"); return; }
    wcscpy(game, g_game_dir);
    size_t len = wcslen(game);
    if (len && game[len - 1] == L'\\') game[len - 1] = 0;   /* a trailing \ would escape the closing quote */
    UINT sl = GetSystemDirectoryW(ps, MAX_PATH);
    if (!sl || sl > MAX_PATH - 40) return;
    wcscat(ps, L"\\WindowsPowerShell\\v1.0\\powershell.exe");
    if (g_host && g_host->port_version) {
        const char *v = g_host->port_version;          /* digits and dots only, so it is safe on a command line */
        int ok = v[0] != 0;
        for (const char *p = v; *p; p++) if (!((*p >= '0' && *p <= '9') || *p == '.')) ok = 0;
        if (ok && strlen(v) < 24) swprintf(port, 32, L" -Installed %hs", v);
    }
    swprintf(cmd, 2048,
             L"\"%ls\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%ls\" "
             L"-GameDir \"%ls\" -WaitPid %lu%ls -ModVersion %hs",
             ps, script, game, GetCurrentProcessId(), port, MOD_VERSION);
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    /* It must outlive the game: break away from any job the game runs in, if allowed. */
    BOOL ok = CreateProcessW(ps, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, NULL, game, &si, &pi);
    if (!ok) ok = CreateProcessW(ps, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, game, &si, &pi);
    if (!ok) { log_line("update: could not start the updater (%lu)", GetLastError()); return; }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log_line("update: updater started (pid %lu)", pi.dwProcessId);
}

/* Called once at mod start, before "MK9 Access loaded". */
void update_init(void) {
    swprintf(g_status_path, MAX_PATH, L"%lsupdate_status.txt", g_mod_dir);
    swprintf(g_mod_status_path, MAX_PATH, L"%lsmod_update_status.txt", g_mod_dir);
    g_started = GetTickCount();
    if (getenv("MK9A_NOUPDATE")) { g_ready_said = g_mod_ready_said = 1; return; }
    start_script();
}

/* What the last close installed (or failed to), for one status file. */
static void announce(const wchar_t *path, const char *what_updated, const char *what_update) {
    char word[32], ver[32];
    if (!read_status(path, word, sizeof word, ver, sizeof ver)) return;
    if (!strcmp(word, "applied")) {
        speakf(0, "%s updated to version %s.", what_updated, ver);
        DeleteFileW(path);
        log_line("update: told the player: %s updated to %s", what_updated, ver);
    } else if (!strcmp(word, "failed")) {
        speakf(0, "The %s update to version %s could not be installed. It will try again when you close the game.",
               what_update, ver);
        DeleteFileW(path);
    }
}

/* Called after "MK9 Access loaded" has been queued. */
void update_announce_start(void) {
    announce(g_mod_status_path, "MK9 Access", "MK9 Access");
    announce(g_status_path, "Game", "game");
}

/* Mod thread, every frame after the announcement: speak "ready" once per download when it finishes. */
void update_poll(void) {
    if (g_ready_said && g_mod_ready_said) return;
    DWORD now = GetTickCount();
    if (now - g_last_poll < 2000) return;
    g_last_poll = now;
    if (now - g_started > 30 * 60 * 1000) { g_ready_said = g_mod_ready_said = 1; return; }   /* 30-minute download: stop */
    char word[32], ver[32];
    if (!g_ready_said && read_status(g_status_path, word, sizeof word, ver, sizeof ver) && !strcmp(word, "ready") &&
        !(g_host && g_host->port_version && !strcmp(g_host->port_version, ver))) {
        speakf(0, "Game update %s is ready. It installs when you close the game.", ver);
        log_line("update: port %s ready, told the player", ver);
        g_ready_said = 1;
    }
    if (!g_mod_ready_said && read_status(g_mod_status_path, word, sizeof word, ver, sizeof ver) && !strcmp(word, "ready") &&
        strcmp(ver, MOD_VERSION)) {
        speakf(0, "MK9 Access update %s is ready. It installs when you close the game.", ver);
        log_line("update: mod %s ready, told the player", ver);
        g_mod_ready_said = 1;
    }
}
