/*
 * MK9Access plugin. Installed as game\plugins\mk9access.dll and loaded by the
 * port's code-plugin loader (mk9_plugins = true in mk9.toml), which calls
 * rex_mod_init once in OnPostLaunchModule: the game's image is loaded and the
 * host (rex_mod.h) is valid, but the port has ALREADY set up its folders and
 * read mk9.toml by then.
 *
 * So the two things the old version.dll loader did before the port read its
 * config no longer fit here:
 *   - saves in the game folder: the port picks the save folder from the
 *     command line BEFORE it reads mk9.toml, so the toml's user_data_root line
 *     alone does nothing. The launch .cmd passes --user_data_root; when the game
 *     was started any other way (mk9.exe directly, a shortcut), relaunch_for_saves
 *     starts it again with the flag and ends this copy before the game runs.
 *     Without it the saves go to Documents, Controlled Folder Access blocks the
 *     first write (the Challenge Tower's MK9PSET) and the game crashes.
 *   - lightest graphics: persisted once in mk9.toml, set by the launcher.
 *
 * This plugin starts the mod (MK9Access\MK9Access.dll) and, with
 * MK9Access\devreload.txt present, hot-swaps a rebuilt copy during development.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <wchar.h>
#include "rex_mod.h"

static wchar_t g_dir[MAX_PATH];           /* game\MK9Access\ */
static const RexModHost *g_host;

typedef void (*core_fn)(void);
typedef void (*core_host_fn)(const RexModHost *);

static FILETIME mtime(const wchar_t *p) {
    WIN32_FILE_ATTRIBUTE_DATA a; FILETIME z = { 0, 0 };
    return GetFileAttributesExW(p, GetFileExInfoStandard, &a) ? a.ftLastWriteTime : z;
}

static HMODULE load_core(int dev, unsigned n) {
    wchar_t src[MAX_PATH], run[MAX_PATH];
    swprintf(src, MAX_PATH, L"%lsMK9Access.dll", g_dir);
    if (!dev) return LoadLibraryW(src);
    /* Load a copy so the build can overwrite MK9Access.dll. */
    swprintf(run, MAX_PATH, L"%lsMK9Access_run%u.dll", g_dir, n);
    if (!CopyFileW(src, run, FALSE)) return NULL;
    return LoadLibraryW(run);
}

/* Start the mod, handing it the host so it can use guest_base / get_function
 * (an older mod DLL without core_start_host still starts through core_start). */
static void start_core(HMODULE core) {
    core_host_fn starth = (core_host_fn)(void *)GetProcAddress(core, "core_start_host");
    if (starth) { starth(g_host); return; }
    core_fn start = (core_fn)(void *)GetProcAddress(core, "core_start");
    if (start) start();
}

static DWORD WINAPI loader_thread(LPVOID unused) {
    (void)unused;
    wchar_t src[MAX_PATH], flag[MAX_PATH], old[MAX_PATH];
    swprintf(src, MAX_PATH, L"%lsMK9Access.dll", g_dir);
    swprintf(flag, MAX_PATH, L"%lsdevreload.txt", g_dir);
    int dev = GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES;
    wchar_t logp[MAX_PATH];
    swprintf(logp, MAX_PATH, L"%lsMK9Access.log", g_dir);
    DeleteFileW(logp);
    for (unsigned i = 0; i < 64; i++) { swprintf(old, MAX_PATH, L"%lsMK9Access_run%u.dll", g_dir, i); DeleteFileW(old); }
    unsigned n = 0;
    HMODULE core = load_core(dev, n);
    if (!core) return 0;
    start_core(core);
    if (!dev) return 0;
    FILETIME t = mtime(src);
    for (;;) {
        Sleep(1000);
        FILETIME t2 = mtime(src);
        if (CompareFileTime(&t, &t2) == 0) continue;
        Sleep(500);                       /* let the build finish writing */
        t = mtime(src);
        core_fn stop = (core_fn)(void *)GetProcAddress(core, "core_stop");
        if (stop) stop();
        FreeLibrary(core);
        n = (n + 1) % 64;
        core = load_core(dev, n);
        if (!core) continue;
        start_core(core);
    }
    return 0;
}

static void plog(const char *msg) {
    if (g_host && g_host->log) g_host->log("MK9Access", msg);
}

/* The user_data_root value from mk9.toml (the save folder the player chose), or
 * 0 when the line is missing or commented out (saves in Documents on purpose). */
static int toml_user_data_root(const wchar_t *game_dir, wchar_t *out, int cap) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsmk9.toml", game_dir);
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    static char buf[65536];
    DWORD n = 0;
    BOOL ok = ReadFile(f, buf, sizeof buf - 1, &n, NULL);
    CloseHandle(f);
    if (!ok) return 0;
    buf[n] = 0;
    for (char *line = buf; line && *line; ) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "user_data_root", 14) == 0) {
            char *p = line + 14;
            while (*p == ' ' || *p == '\t') p++;
            if (*p++ == '=') {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '"' || *p == '\'') {
                    char q = *p++, *end = strchr(p, q);
                    if (end && end > p) {
                        *end = 0;
                        return MultiByteToWideChar(CP_UTF8, 0, p, -1, out, cap) > 1;
                    }
                }
            }
        }
        line = next;
    }
    return 0;
}

/* Saves in the folder mk9.toml names (Controlled Folder Access crashes the port in Documents), whichever way the game
 * starts. The port reads user_data_root from the command line or the REX_USER_DATA_ROOT environment variable before
 * any plugin loads (cvar ApplyEnvironment; the command line outranks it), never from mk9.toml in time.
 * - Every copy puts the toml's folder in REX_USER_DATA_ROOT, so the copies the port starts itself inherit it: an
 *   online room's session passes its own --user_data_root (which wins), and the way back to the lobby (session.cpp
 *   Back) passes none and gets the saves folder from the variable. Before 1.0.8 that restart came up without the
 *   folder, the mod restarted it a second time, and the window of that second copy could open without the keyboard
 *   focus (forum report 2026-11-24: "the app would unfocus and become unresponsive").
 * - Started with neither (a bare mk9.exe, not the launch .cmd): start mk9.exe again with the folder and the same
 *   arguments, then end this copy (before the game's first instruction, nothing saved yet). That copy may take the
 *   foreground. MK9ACCESS_RELAUNCHED stops a loop and is cleared again in the new copy, so its own children don't
 *   inherit it. */
static void relaunch_for_saves(void) {
    const wchar_t *cmd = GetCommandLineW();
    int relaunched = GetEnvironmentVariableW(L"MK9ACCESS_RELAUNCHED", NULL, 0) != 0;
    SetEnvironmentVariableW(L"MK9ACCESS_RELAUNCHED", NULL);
    wchar_t exe[MAX_PATH], dir[MAX_PATH], root[MAX_PATH], env[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wcscpy(dir, exe);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) return;
    slash[1] = 0;
    if (!toml_user_data_root(dir, root, MAX_PATH)) return;   /* player chose Documents */
    int inherited = GetEnvironmentVariableW(L"REX_USER_DATA_ROOT", env, MAX_PATH) > 0;
    SetEnvironmentVariableW(L"REX_USER_DATA_ROOT", root);    /* for the copies the port starts */
    if (wcsstr(cmd, L"--user_data_root") || inherited) return;
    if (relaunched) {
        plog("started without --user_data_root after a relaunch; saves may go to Documents");
        return;
    }

    /* The original arguments without argv[0]. */
    const wchar_t *args = cmd;
    if (*args == L'"') { args = wcschr(args + 1, L'"'); args = args ? args + 1 : L""; }
    else while (*args && *args != L' ' && *args != L'\t') args++;
    while (*args == L' ' || *args == L'\t') args++;

    static wchar_t line[32768];
    swprintf(line, 32768, L"\"%ls\" %ls%ls--user_data_root=\"%ls\"", exe, args, *args ? L" " : L"", root);
    SetEnvironmentVariableW(L"MK9ACCESS_RELAUNCHED", L"1");
    AllowSetForegroundWindow(ASFW_ANY);                      /* the new copy's window may come to the front */
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        SetEnvironmentVariableW(L"MK9ACCESS_RELAUNCHED", NULL);
        plog("relaunch with the save folder failed; saves may go to Documents");
        return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    plog("started without --user_data_root: relaunched with the save folder from mk9.toml");
    TerminateProcess(GetCurrentProcess(), 0);
}

__declspec(dllexport) void rex_mod_init(const RexModHost *host) {
    g_host = host;
    relaunch_for_saves();
    plog("accessibility plugin: starting MK9Access.dll");
    GetModuleFileNameW(NULL, g_dir, MAX_PATH);   /* mk9.exe, so the folder is the game folder */
    wchar_t *slash = wcsrchr(g_dir, L'\\');
    if (slash) slash[1] = 0;
    wcscat(g_dir, L"MK9Access\\");
    CloseHandle(CreateThread(NULL, 0, loader_thread, NULL, 0, NULL));
}

/* The port's normal close hard-exits right after this; the mod's own threads go
 * with the process. A dev reload uses core_stop through loader_thread, not here. */
__declspec(dllexport) void rex_mod_shutdown(void) {
}
