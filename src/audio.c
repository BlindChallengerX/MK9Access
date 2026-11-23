/*
 * Audio for MK9 Access: a small synth on its own waveOut stream (44.1 kHz
 * stereo, 4 buffers of 15 ms, so cues lag the game by under 60 ms).
 *
 * Two kinds of sound:
 *   tones  one-shots that glide from f0 to f1 with a short attack and decay
 *   hums   continuous voices (slot 0..7) whose pitch, loudness and pan
 *          follow targets smoothly, for things that are always there.
 *          W_WIND is noise through a band-pass at the hum's pitch with a
 *          slow gusting swell (the ring-edge drop-off).
 */
#include "access.h"
#include <math.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define BUF_FRAMES 662
#define NBUF 4
#define NTONES 48
#define NHUMS 8
#define PI2 6.28318530718f

typedef struct {
    int on; float delay; float t, len, f0, f1, gain, pan, phase; int wave;
} Tone;
typedef struct {
    float freq, gain, pan;            /* targets */
    float cf, cg, cp, phase;          /* current */
    int wave;
    float lp, bp, gust;               /* W_WIND filter state, gust phase */
    float trem, ctrem, tph;           /* tremolo depth target / current, phase (7 Hz) */
} Hum;
/* A voice clip (a recorded WAV, e.g. a Kokoro fatality description): mono float
 * samples at the file's own rate, resampled to the mix rate on playback. */
typedef struct {
    float *s; int n; double pos, step; float gain; int on;
} Clip;

/* Sound effects: short recorded samples (Krypt footsteps, bump) preloaded from MK9Access\sounds\,
 * several at once, each with its own delay, gain, pan and playback rate. */
#define NSAMPLES 16
#define NSFX 12
typedef struct { float *s; int n; double step; } Sample;
typedef struct { int on, id; float delay; double pos, step; float gain, pan; } SfxVoice;

static CRITICAL_SECTION s_lock;
static Sample s_samples[NSAMPLES];
static int s_nsamples;
static SfxVoice s_sfx[NSFX];
static Tone s_tones[NTONES];
static Hum s_hums[NHUMS];
static Clip s_clip;                   /* one voice clip at a time */
static float s_duck = 1.0f, s_duck_target = 1.0f;   /* the mod's own tones/hums dip under a clip */
static uint32_t s_noise = 22222;
volatile float g_master = 1.2f;
static volatile LONG s_bufs_done, s_open_ok, s_stop;
static short *s_rec; static int s_rec_n, s_rec_cap; static volatile int s_rec_done;
static volatile float s_peak;
static volatile int s_active_tones;

static float osc(int wave, float ph) {
    switch (wave) {
    case W_SQUARE: return ph < 0.5f ? 0.6f : -0.6f;
    case W_TRI:    return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
    case W_SAW:    return (2 * ph - 1) * 0.7f;
    case W_NOISE:  s_noise = s_noise * 1664525u + 1013904223u; return ((s_noise >> 9) / 4194304.0f - 1.0f) * 0.7f;
    case W_SOFT:   /* muffled beep: a sine with a little second harmonic */
        return (sinf(PI2 * ph) + 0.12f * sinf(2 * PI2 * ph)) * 0.85f;
    case W_REED: {
        /* Mist World's beacon tone: harmonics 1-11, strong odd, weak even */
        static const float amp[11] = { 1.0f, 0.125f, 0.5f, 0.0625f, 0.25f, 0.0234f, 0.0938f, 0.0078f, 0.0313f, 0.0039f, 0.0078f };
        float v = 0;
        for (int k = 0; k < 11; k++) v += amp[k] * sinf(PI2 * ph * (k + 1));
        return v * 0.45f;
    }
    default:       return sinf(PI2 * ph);
    }
}

/* A NaN anywhere would silence the whole mix for good: refuse bad values. */
static int bad(float v) { return !isfinite(v); }

void tone_at(float delay, float f0, float f1, float seconds, float gain, float pan, int wave) {
    if (bad(delay) || bad(f0) || bad(f1) || bad(seconds) || bad(gain) || bad(pan) || f0 <= 0 || f1 <= 0 || seconds <= 0) {
        log_line("audio: bad tone %g %g %g %g %g %g", delay, f0, f1, seconds, gain, pan);
        return;
    }
    EnterCriticalSection(&s_lock);
    int pick = 0; float best = -1;
    for (int i = 0; i < NTONES; i++) {
        if (!s_tones[i].on) { pick = i; best = 1e9f; break; }
        float left = s_tones[i].len - s_tones[i].t;
        if (best < 0 || left < best) { best = left; pick = i; }
    }
    Tone *t = &s_tones[pick];
    t->on = 1; t->delay = delay; t->t = 0; t->len = seconds; t->f0 = f0; t->f1 = f1;
    t->gain = gain; t->pan = pan < -1 ? -1 : pan > 1 ? 1 : pan; t->phase = 0; t->wave = wave;
    LeaveCriticalSection(&s_lock);
}

void tone(float f0, float f1, float seconds, float gain, float pan, int wave) {
    tone_at(0, f0, f1, seconds, gain, pan, wave);
}

void hum_set(int slot, float freq, float gain, float pan, int wave) {
    if (slot < 0 || slot >= NHUMS) return;
    if (bad(freq) || bad(gain) || bad(pan)) {
        static DWORD last;
        if (GetTickCount() - last > 2000) { last = GetTickCount(); log_line("audio: bad hum %d %g %g %g", slot, freq, gain, pan); }
        freq = 200; gain = 0; pan = 0;
    }
    EnterCriticalSection(&s_lock);
    Hum *h = &s_hums[slot];
    if (h->cg < 0.001f) { h->cf = freq; h->cp = pan; }   /* starting from silence: jump to the pitch */
    h->freq = freq; h->gain = gain; h->pan = pan < -1 ? -1 : pan > 1 ? 1 : pan; h->wave = wave;
    LeaveCriticalSection(&s_lock);
}

/* Tremolo (0 none .. 1 full) on a hum: a texture change for extra information. */
void hum_trem(int slot, float depth) {
    if (slot < 0 || slot >= NHUMS || bad(depth)) return;
    EnterCriticalSection(&s_lock);
    s_hums[slot].trem = depth < 0 ? 0 : depth > 1 ? 1 : depth;
    LeaveCriticalSection(&s_lock);
}

void hum_all_off(void) {
    EnterCriticalSection(&s_lock);
    for (int i = 0; i < NHUMS; i++) s_hums[i].gain = 0;
    LeaveCriticalSection(&s_lock);
}

/* ---------- voice clips (recorded WAV) ---------- */
static uint32_t rd_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Load a 16-bit PCM WAV (mono or stereo, any rate) as mono float; *rate gets its rate. */
static float *load_wav(const wchar_t *path, int *out_n, int *out_rate) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 44 || sz > 64 * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *d = (uint8_t *)malloc((size_t)sz);
    if (!d || fread(d, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(d); return NULL; }
    fclose(f);
    if (memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { free(d); return NULL; }
    int ch = 1, rate = 24000, bits = 16;
    const uint8_t *data = NULL; uint32_t dlen = 0;
    size_t p = 12;
    while (p + 8 <= (size_t)sz) {
        uint32_t id = rd_le32(d + p); uint32_t len = rd_le32(d + p + 4); const uint8_t *body = d + p + 8;
        if (id == 0x20746d66) { /* "fmt " */ ch = rd_le16(body + 2); rate = (int)rd_le32(body + 4); bits = rd_le16(body + 14); }
        else if (id == 0x61746164) { /* "data" */ data = body; dlen = len; }
        p += 8 + len + (len & 1);
    }
    if (!data || bits != 16 || ch < 1 || ch > 2) { free(d); return NULL; }
    if ((size_t)(data - d) + dlen > (size_t)sz) dlen = (uint32_t)(sz - (data - d));
    int frames = (int)(dlen / (2 * ch));
    float *s = (float *)malloc(sizeof(float) * (frames + 2));
    if (!s) { free(d); return NULL; }
    const int16_t *pcm = (const int16_t *)data;
    for (int i = 0; i < frames; i++) {
        int v = ch == 2 ? (pcm[2 * i] + pcm[2 * i + 1]) / 2 : pcm[i];
        s[i] = v / 32768.0f;
    }
    s[frames] = s[frames + 1] = 0;
    free(d);
    *out_n = frames; *out_rate = rate;
    return s;
}

/* Play a voice clip; stops any clip already playing. The mod's own cues duck under it. */
void clip_play(const wchar_t *path, float gain) {
    int n = 0, rate = 24000;
    float *s = load_wav(path, &n, &rate);
    if (!s || n <= 0) { if (s) free(s); log_line("audio: clip load failed"); return; }
    EnterCriticalSection(&s_lock);
    free(s_clip.s);
    s_clip.s = s; s_clip.n = n; s_clip.pos = 0; s_clip.step = (double)rate / RATE;
    s_clip.gain = bad(gain) || gain <= 0 ? 1.0f : gain; s_clip.on = 1;
    s_duck_target = 0.35f;
    LeaveCriticalSection(&s_lock);
    log_line("audio: clip %d frames @ %d Hz (%.1f s)", n, rate, n / (float)rate);
}

void clip_stop(void) {
    EnterCriticalSection(&s_lock);
    s_clip.on = 0; free(s_clip.s); s_clip.s = NULL; s_duck_target = 1.0f;
    LeaveCriticalSection(&s_lock);
}

int clip_playing(void) { return s_clip.on; }

/* Load MK9Access\sounds\<file> once; returns its id, or -1. */
int sfx_load(const wchar_t *file) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lssounds\\%ls", g_mod_dir, file);
    if (s_nsamples >= NSAMPLES) return -1;
    int n = 0, rate = RATE;
    float *f = load_wav(path, &n, &rate);
    if (!f || n <= 0) { free(f); log_line("audio: sound %ls missing", file); return -1; }
    EnterCriticalSection(&s_lock);
    int id = s_nsamples++;
    s_samples[id].s = f; s_samples[id].n = n; s_samples[id].step = (double)rate / RATE;
    LeaveCriticalSection(&s_lock);
    return id;
}

/* Play a loaded sample after `delay` seconds; rate 1 = as recorded (0.9 = a little lower). */
void sfx_play(int id, float delay, float gain, float pan, float rate) {
    if (id < 0 || id >= s_nsamples || bad(delay) || bad(gain) || bad(pan) || bad(rate) || rate <= 0) return;
    EnterCriticalSection(&s_lock);
    int pick = 0;
    for (int i = 0; i < NSFX; i++) if (!s_sfx[i].on) { pick = i; break; } else if (s_sfx[i].pos > s_sfx[pick].pos) pick = i;
    SfxVoice *v = &s_sfx[pick];
    v->on = 1; v->id = id; v->delay = delay; v->pos = 0; v->step = s_samples[id].step * rate;
    v->gain = gain; v->pan = pan < -1 ? -1 : pan > 1 ? 1 : pan;
    LeaveCriticalSection(&s_lock);
}

static void mix(short *out, int frames) {
    static float buf[BUF_FRAMES * 2];
    memset(buf, 0, sizeof buf);
    const float dt = 1.0f / RATE;
    EnterCriticalSection(&s_lock);
    for (int i = 0; i < NTONES; i++) {
        Tone *t = &s_tones[i];
        if (!t->on) continue;
        float lg = sqrtf(0.5f * (1 - t->pan)), rg = sqrtf(0.5f * (1 + t->pan));
        for (int n = 0; n < frames; n++) {
            if (t->delay > 0) { t->delay -= dt; continue; }
            if (t->t >= t->len) { t->on = 0; break; }
            float k = t->t / t->len;
            float f = t->f0 * powf(t->f1 / t->f0, k);
            /* short notes get a short release; W_SOFT fades in and out gently */
            float att = t->wave == W_SOFT ? 0.015f : 0.004f;
            float rel = t->wave == W_SOFT ? t->len * 0.5f : fminf(0.03f, t->len * 0.25f);
            float env = fminf(1.0f, t->t / att) * fminf(1.0f, (t->len - t->t) / rel);
            float s = osc(t->wave, t->phase) * env * t->gain;
            t->phase += f * dt; t->phase -= floorf(t->phase);
            buf[2 * n] += s * lg; buf[2 * n + 1] += s * rg;
            t->t += dt;
        }
    }
    for (int i = 0; i < NHUMS; i++) {
        Hum *h = &s_hums[i];
        if (h->gain < 0.001f && h->cg < 0.0005f) { h->cg = 0; continue; }
        for (int n = 0; n < frames; n++) {
            h->cg += (h->gain - h->cg) * 0.0015f;
            h->cf += (h->freq - h->cf) * 0.002f;
            h->cp += (h->pan - h->cp) * 0.002f;
            float s;
            if (h->wave == W_WIND) {
                /* two slow LFOs make uneven gusts; they swell the level and
                 * sweep the filter so it whooshes rather than hisses */
                h->gust += dt * 0.21f; if (h->gust >= 100) h->gust -= 100;
                float g = 0.5f + 0.5f * sinf(PI2 * h->gust) * (0.6f + 0.4f * sinf(PI2 * h->gust * 2.7f));
                float fc = h->cf * (0.7f + 0.6f * g);
                float c = 2 * sinf(3.14159265f * fminf(fc, 6000.0f) / RATE);
                float x = osc(W_NOISE, 0);
                h->lp += c * h->bp;
                h->bp += c * (x - h->lp - 0.9f * h->bp);
                s = h->bp * 2.2f * (0.55f + 0.45f * g) * h->cg;
            } else {
                s = osc(h->wave, h->phase) * h->cg;
                h->phase += h->cf * dt; h->phase -= floorf(h->phase);
            }
            h->ctrem += (h->trem - h->ctrem) * 0.0015f;
            if (h->ctrem > 0.001f) {
                h->tph += 7.0f * dt; h->tph -= floorf(h->tph);
                s *= 1.0f - h->ctrem * (0.5f - 0.5f * cosf(PI2 * h->tph));
            }
            buf[2 * n] += s * sqrtf(0.5f * (1 - h->cp)); buf[2 * n + 1] += s * sqrtf(0.5f * (1 + h->cp));
        }
    }
    for (int i = 0; i < NSFX; i++) {
        SfxVoice *v = &s_sfx[i];
        if (!v->on) continue;
        const Sample *sm = &s_samples[v->id];
        float lg = sqrtf(0.5f * (1 - v->pan)) * v->gain, rg = sqrtf(0.5f * (1 + v->pan)) * v->gain;
        for (int n = 0; n < frames; n++) {
            if (v->delay > 0) { v->delay -= dt; continue; }
            int idx = (int)v->pos;
            if (idx + 1 >= sm->n) { v->on = 0; break; }
            float fr = (float)(v->pos - idx);
            float x = sm->s[idx] * (1 - fr) + sm->s[idx + 1] * fr;
            buf[2 * n] += x * lg; buf[2 * n + 1] += x * rg;
            v->pos += v->step;
        }
    }
    /* A voice clip plays over the mod's own cues, which duck under it. */
    for (int n = 0; n < frames; n++) {
        s_duck += (s_duck_target - s_duck) * 0.0012f;
        buf[2 * n] *= s_duck; buf[2 * n + 1] *= s_duck;
        if (!s_clip.on || !s_clip.s) continue;
        int idx = (int)s_clip.pos;
        if (idx + 1 >= s_clip.n) { s_clip.on = 0; free(s_clip.s); s_clip.s = NULL; s_duck_target = 1.0f; continue; }
        float fr = (float)(s_clip.pos - idx);
        float v = (s_clip.s[idx] * (1 - fr) + s_clip.s[idx + 1] * fr) * s_clip.gain;
        buf[2 * n] += v; buf[2 * n + 1] += v;
        s_clip.pos += s_clip.step;
    }
    LeaveCriticalSection(&s_lock);
    float m = g_master, pk = 0;
    for (int n = 0; n < frames * 2; n++) {
        if (!isfinite(buf[n])) {
            buf[n] = 0;
            EnterCriticalSection(&s_lock);
            for (int i = 0; i < NHUMS; i++) if (!isfinite(s_hums[i].cg) || !isfinite(s_hums[i].cf) || !isfinite(s_hums[i].cp) || !isfinite(s_hums[i].phase) || !isfinite(s_hums[i].bp) || !isfinite(s_hums[i].lp)) memset(&s_hums[i], 0, sizeof s_hums[i]);
            for (int i = 0; i < NTONES; i++) if (!isfinite(s_tones[i].phase)) s_tones[i].on = 0;
            LeaveCriticalSection(&s_lock);
        }
        float v = tanhf(buf[n] * m) * 30000.0f;
        out[n] = (short)v;
        if (fabsf(v) > pk) pk = fabsf(v);
    }
    if (pk > s_peak) s_peak = pk;
    EnterCriticalSection(&s_lock);
    if (s_rec && !s_rec_done && s_rec_n + frames * 2 <= s_rec_cap) {   /* "audiorec": keep the mix for analysis */
        memcpy(s_rec + s_rec_n, out, sizeof(short) * frames * 2);
        s_rec_n += frames * 2;
        if (s_rec_n + frames * 2 > s_rec_cap) s_rec_done = 1;
    }
    LeaveCriticalSection(&s_lock);
}

/* Development: record the mod's own mix for `seconds` to MK9Access\audiorec.wav (analyze.py). */
void audio_rec(int seconds) {
    if (seconds < 1 || seconds > 60) seconds = 5;
    short *b = (short *)malloc(sizeof(short) * RATE * 2 * seconds);
    if (!b) return;
    EnterCriticalSection(&s_lock);
    free(s_rec); s_rec = b; s_rec_n = 0; s_rec_cap = RATE * 2 * seconds; s_rec_done = 0;
    LeaveCriticalSection(&s_lock);
    log_line("audio: recording %d s", seconds);
}

void audio_rec_poll(void) {
    if (!s_rec_done) return;
    EnterCriticalSection(&s_lock);
    short *b = s_rec; int n = s_rec_n; s_rec = NULL; s_rec_done = 0;
    LeaveCriticalSection(&s_lock);
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%lsaudiorec.wav", g_mod_dir);
    FILE *f = _wfopen(path, L"wb");
    if (f) {
        uint32_t bytes = (uint32_t)n * 2, v; uint16_t s;
        fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
        v = RATE; fwrite(&v, 4, 1, f); v = RATE * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(b, 2, (size_t)n, f);
        fclose(f);
        log_line("audio: recording written");
    }
    free(b);
}

static DWORD WINAPI audio_thread(LPVOID unused) {
    (void)unused;
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX wf; memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = RATE;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = RATE * 4;
    HWAVEOUT wo;
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        log_line("audio: waveOutOpen failed"); return 0;
    }
    s_open_ok = 1;
    log_line("audio: stream open");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    static short data[NBUF][BUF_FRAMES * 2];
    WAVEHDR hdr[NBUF];
    for (int i = 0; i < NBUF; i++) {
        memset(&hdr[i], 0, sizeof hdr[i]);
        hdr[i].lpData = (LPSTR)data[i]; hdr[i].dwBufferLength = sizeof data[i];
        mix(data[i], BUF_FRAMES);
        waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
        waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
    }
    while (!s_stop) {
        WaitForSingleObject(ev, 100);
        for (int i = 0; i < NBUF; i++) {
            if (hdr[i].dwFlags & WHDR_DONE) {
                s_bufs_done++;
                mix(data[i], BUF_FRAMES);
                waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
            }
        }
    }
    waveOutReset(wo);
    for (int i = 0; i < NBUF; i++) waveOutUnprepareHeader(wo, &hdr[i], sizeof hdr[i]);
    waveOutClose(wo);
    CloseHandle(ev);
    return 0;
}

static HANDLE s_thread;
void audio_init(void) {
    InitializeCriticalSection(&s_lock);
    s_stop = 0;
    s_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
}

/* Development reload: the stream thread must end before this DLL is unloaded. */
void audio_shutdown(void) {
    s_stop = 1;
    if (s_thread) { WaitForSingleObject(s_thread, 2000); CloseHandle(s_thread); s_thread = NULL; }
}

void audio_stat(void) {
    int tones = 0;
    EnterCriticalSection(&s_lock);
    for (int i = 0; i < NTONES; i++) tones += s_tones[i].on;
    int hums = 0; for (int i = 0; i < NHUMS; i++) hums += s_hums[i].cg > 0.001f;
    LeaveCriticalSection(&s_lock);
    log_line("audio: open %ld, buffers played %ld, tones on %d, hums on %d, peak %.0f, master %.2f", s_open_ok, s_bufs_done, tones, hums, s_peak, g_master);
    s_peak = 0;
}
