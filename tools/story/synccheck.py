"""Check where the Story descriptions really land: record the speakers (loopback) while a movie plays, find the movie's
English track in the recording (cross-correlation) and each description clip, and print each clip's time in movie
seconds next to the cue time from cues.tsv, plus overlaps with the subtitle lines.
Usage: python -I synccheck.py rec.wav movie [seconds]       record (starts when called; start before the movie)
       python -I synccheck.py --analyze rec.wav movie"""
import os, sys, numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import av
from prep import story_sections
GAME = r"C:\Users\michm\Downloads\MK9 Access\game"
SR = 16000

def movie_track(name):
    c = av.open(os.path.join(GAME, "game_root", "Movies", name + ".wmv"))
    st = [s for s in c.streams.audio if s.language == "eng"][0]
    res = av.AudioResampler(format="flt", layout="mono", rate=SR); buf = []
    for fr in c.decode(st):
        for r in res.resample(fr): buf.append(r.to_ndarray().ravel())
    return np.concatenate(buf).astype(np.float32)

def xcorr_at(rec, ref):
    n = 1 << int(np.ceil(np.log2(len(rec) + len(ref))))
    R = np.fft.rfft(rec, n) * np.conj(np.fft.rfft(ref, n))
    c = np.fft.irfft(R, n)[:len(rec)]
    i = int(np.argmax(c)); return i, c[i] / (np.linalg.norm(ref) * np.linalg.norm(rec[i:i + len(ref)]) + 1e-9)

def load(path):
    a, sr = sf.read(path, dtype="float32")
    if a.ndim > 1: a = a.mean(1)
    if sr != SR: a = np.interp(np.arange(0, len(a), sr / SR), np.arange(len(a)), a).astype(np.float32)
    return a

def analyze(path, name):
    rec = load(path); mv = movie_track(name)
    # the movie's start in the recording: a 15 s piece from 30 s in
    a0 = 30 * SR; piece = mv[a0:a0 + 15 * SR]
    i, q = xcorr_at(rec, piece); start = (i - a0) / SR
    print(f"movie {name} starts at {start:.2f} s of the recording (match {q:.2f})")
    rows = story_sections().get(name.lower(), [])
    cues = [l.rstrip("\n").split("\t") for l in open(os.path.join(GAME, "MK9Access", "story", "cues.tsv"), encoding="utf-8")
            if not l.startswith("#") and l.split("\t")[0] == name.lower()]
    # remove the movie from the recording to leave the clips
    s0 = int(start * SR); resid = rec.copy()
    seg = resid[max(0, s0):max(0, s0) + len(mv)]
    m = mv[:len(seg)]; g = float(np.dot(seg, m) / (np.dot(m, m) + 1e-9)); seg -= g * m
    for mvn, t, clip in cues:
        c = load(os.path.join(GAME, "MK9Access", "story", clip))
        lo = int((start + float(t) - 2) * SR); hi = int((start + float(t) + 4) * SR) + len(c)
        if lo < 0 or hi > len(resid): print(f"{float(t):7.2f}  outside the recording"); continue
        j, qq = xcorr_at(resid[lo:hi], c); at = (lo + j) / SR - start
        end = at + len(c) / SR
        over = [x for s, e, x in rows if s < end and e > at]
        print(f"cue {float(t):7.2f}  heard {at:7.2f} (+{at - float(t):.2f})  ends {end:7.2f}  match {qq:.2f}"
              + (f"  OVERLAPS: {over[0][:40]}" if over else ""))

if __name__ == "__main__":
    if sys.argv[1] == "--analyze": analyze(sys.argv[2], sys.argv[3]); sys.exit()
    import soundcard as scard
    path, name = sys.argv[1], sys.argv[2]; secs = float(sys.argv[3]) if len(sys.argv) > 3 else 120
    mic = scard.get_microphone(str(scard.default_speaker().name), include_loopback=True)
    with mic.recorder(samplerate=48000, channels=2) as r:
        data = r.record(numframes=int(48000 * secs))
    sf.write(path, data, 48000, subtype="PCM_16")
    analyze(path, name)
