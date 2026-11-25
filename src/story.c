/* Story Mode audio description.
 *
 * Story cutscenes are the movies game_root\Movies\mk*.wmv, played by the XDK's XMV player; the port decodes their
 * video with FFmpeg (mk9_movie.cpp). Here:
 *  - which movie: the port's file system opens Movies\<name>.wmv on the host a moment before the codec starts
 *    (the same signal the port uses for its .bik pictures); NtCreateFile is watched for that path.
 *  - where in it: the codec's GetOutput (82D25390, the port's replacement; args table, buffer, capacity, &size,
 *    &flags, &time u64, &one) gives each picture's time in ms at *r8 (big-endian). Time 0 = the first picture.
 *  - what to say: MK9Access\story\cues.tsv, "movie<TAB>seconds<TAB>clip.wav<TAB>text", from data/story/<movie>.txt
 *    (tools/story/gen_story.py; clips are Kokoro af_heart lines fitted into the silences between the game's own
 *    subtitle timings, MK9Story.eng).
 *  - stop: the codec's Close (82D25280), e.g. the movie skipped with START or A.
 * The guest hooks only store numbers; story_poll on the mod thread plays the clips, or has the screen reader say the
 * text when the player chose "Descriptions: screen reader" (Alt+F12; forum report 2026-11-24: the voice was much
 * louder than the game's dialogue). */
#include "access.h"
#include <stdio.h>
#include <string.h>
#include <winternl.h>
#include "MinHook.h"

#define G_XMV_GETOUTPUT 0x82D25390u
#define G_XMV_INIT      0x82D255C0u
#define G_XMV_CLOSE     0x82D25280u
/* the codec hands out pictures ~200 ms before they are shown and heard (measured 2026-11-23 by loopback over the
 * whole of mk001_002_003: every clip landed 0.18-0.22 s early) */
#define LAG_MS 200

typedef struct { char movie[40]; int ms; char clip[48]; char *text; } Cue;
static Cue *g_cues; static int g_ncues;

static guest_fn o_getout, o_init, o_close;
static char g_opened[40];               /* last Movies\*.wmv opened (lower case, no extension) */
static CRITICAL_SECTION g_cs;
static volatile LONG g_gen;             /* bumped at every codec Init (a new movie) */
static volatile LONG g_closed_gen;      /* g_gen when the codec closed */
static char g_playing[40];              /* the movie of generation g_gen */
static volatile LONG64 g_first_ms = -1; /* the first picture's time */
static volatile LONG g_now_ms = -1;     /* the latest picture's time - first */

typedef NTSTATUS (NTAPI *NtCreateFile_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER,
                                         ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
static NtCreateFile_t o_ntcreate;

static void note_path(const UNICODE_STRING *u) {
    if (!u || !u->Buffer) return;
    int n = u->Length / 2;
    if (n < 12) return;
    const wchar_t *p = u->Buffer;
    if (_wcsnicmp(p + n - 4, L".wmv", 4)) return;
    int s = n - 4;
    while (s > 0 && p[s - 1] != L'\\' && p[s - 1] != L'/') s--;
    if (s < 8 || _wcsnicmp(p + s - 7, L"Movies", 6)) return;
    char name[40]; int k = 0;
    for (int i = s; i < n - 4 && k < (int)sizeof name - 1; i++) {
        wchar_t c = p[i];
        name[k++] = (char)(c < 128 ? (c >= 'A' && c <= 'Z' ? c + 32 : c) : '_');
    }
    name[k] = 0;
    EnterCriticalSection(&g_cs);
    strcpy(g_opened, name);
    LeaveCriticalSection(&g_cs);
}

static NTSTATUS NTAPI h_ntcreate(PHANDLE h, ACCESS_MASK a, POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io,
                                 PLARGE_INTEGER sz, ULONG attr, ULONG share, ULONG disp, ULONG opt, PVOID ea, ULONG eal) {
    NTSTATUS r = o_ntcreate(h, a, oa, io, sz, attr, share, disp, opt, ea, eal);
    if (r >= 0 && oa) {
        note_path(oa->ObjectName);
    }
    return r;
}

static void h_init(PPCContext *ctx, uint8_t *base) {
    CALL_ORIG(o_init, ctx, base);
    EnterCriticalSection(&g_cs);
    strcpy(g_playing, g_opened);
    g_first_ms = -1; g_now_ms = -1;
    InterlockedIncrement(&g_gen);
    LeaveCriticalSection(&g_cs);
}

static void h_close(PPCContext *ctx, uint8_t *base) {
    CALL_ORIG(o_close, ctx, base);
    g_closed_gen = g_gen;
}

static void h_getout(PPCContext *ctx, uint8_t *base) {
    uint32_t tp = (uint32_t)ctx->r8;
    CALL_ORIG(o_getout, ctx, base);
    if ((int32_t)ctx->r3 < 0 || !tp || !guest_ok(tp, 8)) return;
    int64_t t = (int64_t)(((uint64_t)g32(tp) << 32) | g32(tp + 4));
    if (g_first_ms < 0 || t < g_first_ms) g_first_ms = t;
    g_now_ms = (LONG)(t - g_first_ms);
}

static void load_cues(void) {
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%lsstory\\cues.tsv", g_mod_dir);
    FILE *f = _wfopen(p, L"r");
    if (!f) { log_line("story: no cues.tsv"); return; }
    char line[1024]; int cap = 0;
    while (fgets(line, sizeof line, f)) {
        char mv[40], clip[48]; double sec; int used = 0;
        if (line[0] == '#' || sscanf(line, "%39[^\t]\t%lf\t%47[^\t\r\n]%n", mv, &sec, clip, &used) != 3) continue;
        if (g_ncues == cap) { cap = cap ? cap * 2 : 256; g_cues = realloc(g_cues, cap * sizeof *g_cues); }
        Cue *c = &g_cues[g_ncues++];
        strcpy(c->movie, mv); strcpy(c->clip, clip); c->ms = (int)(sec * 1000 + 0.5);
        const char *t = line + used;
        if (*t == '\t') t++;
        c->text = _strdup(t);
        if (c->text) c->text[strcspn(c->text, "\r\n")] = 0;
    }
    fclose(f);
    log_line("story: %d described moments", g_ncues);
}

void story_init(void) {
    InitializeCriticalSection(&g_cs);
    load_cues();
    if (!g_ncues) return;
    guest_hook(G_XMV_INIT, (void *)h_init, (void **)&o_init);
    guest_hook(G_XMV_CLOSE, (void *)h_close, (void **)&o_close);
    guest_hook(G_XMV_GETOUTPUT, (void *)h_getout, (void **)&o_getout);
    void *target = NULL;
    MH_STATUS s = MH_CreateHookApiEx(L"ntdll", "NtCreateFile", (void *)h_ntcreate, (void **)&o_ntcreate, &target);
    log_line("story: NtCreateFile hook %d", (int)s);
}

/* mod thread */
int desc_play(const wchar_t *clip, const char *text) {
    int m = desc_mode();
    if (m == DESC_OFF) return 0;
    if (m == DESC_READER && text && text[0]) {
        int was = speech_force(1); speak_utf8(text, 0); speech_force(was);   /* queued: never cuts a line off */
        return 0;
    }
    clip_play(clip, desc_gain());
    return 1;
}

static LONG s_gen = -1;      /* generation being described */
static int s_first, s_last;  /* this movie's cues [s_first, s_last) */
static int s_next;           /* next cue to play */
static int s_clip_mine;      /* the clip playing is a story clip */
static DWORD s_said_t;       /* the screen reader was given a story description (screen reader mode) */

void story_poll(void) {
    if (!g_ncues) return;
    LONG gen = g_gen;
    if (gen != s_gen) {
        if (s_clip_mine && clip_playing()) clip_stop();
        s_clip_mine = 0;
        s_gen = gen;
        char mv[40];
        EnterCriticalSection(&g_cs); strcpy(mv, g_playing); LeaveCriticalSection(&g_cs);
        s_first = s_last = s_next = 0;
        for (int i = 0; i < g_ncues; i++)
            if (!strcmp(g_cues[i].movie, mv)) { if (s_last == 0) s_first = i; s_last = i + 1; }
        s_next = s_first;
        if (gen > 0) log_line("story: movie %s, %d descriptions", mv[0] ? mv : "?", s_last - s_first);
    }
    if (g_closed_gen == gen) {           /* skipped or ended */
        if (s_clip_mine && clip_playing()) clip_stop();
        if (s_said_t && GetTickCount() - s_said_t < 15000) speech_stop();   /* skipped: the rest of it is stale */
        s_said_t = 0;
        s_clip_mine = 0; s_next = s_last;
        return;
    }
    if (s_next >= s_last) return;
    LONG now = g_now_ms;
    if (now < 0) return;
    now -= LAG_MS;
    Cue *c = &g_cues[s_next];
    if (now < c->ms) return;
    if (s_clip_mine && clip_playing()) {   /* the last one still speaking: wait, give up when too late */
        if (now - c->ms > 2500) { log_line("story: %s %d ms dropped (late)", c->movie, c->ms); s_next++; }
        return;
    }
    if (now - c->ms > 4000) { s_next++; return; }   /* jumped past it */
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%lsstory\\%hs", g_mod_dir, c->clip);
    s_clip_mine = desc_play(p, c->text);
    if (!s_clip_mine && desc_mode() == DESC_READER) s_said_t = GetTickCount();
    log_line("story: %s at %d ms (cue %d ms): %s%s", c->movie, (int)now, c->ms, c->clip,
             desc_mode() == DESC_READER ? " (screen reader)" : desc_mode() == DESC_OFF ? " (off)" : "");
    s_next++;
}

/* RE: is a story movie being described right now */
int story_playing(void) { return g_ncues && g_closed_gen != g_gen && g_now_ms >= 0; }
