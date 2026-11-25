"""Attack height sounds for fight.c: data/sounds/height_<high|mid|low|over>_<hit|block>.wav (mono, 16-bit, at sakit.SR).
Channels, one meaning each:
  pitch           = the attack's height (low D4, mid A4, high E5: fifths apart, so three clear steps)
  falling sweep   = overhead (D6 down to D5 in 60 ms: it comes down from above)
  bright / muffled = it landed / it was blocked (the blocked one is a dull, shorter knock, 5 dB softer)
Timbre: a wooden mallet (partials 1, 3.9, 9.2 decaying fast) with a short noise tick on hits, so the attack is sharp
and the sound is short (hits 0.22 s, blocks 0.12 s). Run: python tools/gen_height_sounds.py"""
import os, sys
import numpy as np
sys.path.insert(0, r"C:\Users\michm\tools\spatial-audio-kit")
import sakit

SR = sakit.SR
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data', 'sounds')
NOTES = {'low': 293.66, 'mid': 440.0, 'high': 659.26}


def mallet(f0, dur, tau, bright, sweep=None):
    """f0 in Hz (or a frequency curve when sweep=(f_start, f_end, seconds)); bright 0..1 scales the upper partials"""
    n = int(dur * SR); t = np.arange(n) / SR
    if sweep:
        fs, fe, ts = sweep
        f = np.where(t < ts, fs * (fe / fs) ** (t / ts), fe)
    else:
        f = np.full(n, f0)
    ph = 2 * np.pi * np.cumsum(f) / SR
    x = np.sin(ph) * np.exp(-t / tau)
    x += bright * 0.35 * np.sin(3.9 * ph) * np.exp(-t / (tau / 4))
    x += bright * 0.10 * np.sin(9.2 * ph) * np.exp(-t / (tau / 10))
    return x * sakit.env(n, attack=0.002 if bright > 0.5 else 0.006, release=0.03)


def tick(dur, level):
    n = int(dur * SR)
    x = sakit.bandpass(sakit.noise(dur, seed=7), 1800, 4500)
    return level * x / (np.max(np.abs(x)) + 1e-9) * sakit.env(n, attack=0.0005, release=dur * 0.7)


def make(height, kind):
    hit = kind == 'hit'
    dur, tau, bright = (0.22, 0.075, 1.0) if hit else (0.12, 0.035, 0.15)
    sweep = (1174.7, 587.3, 0.06) if height == 'over' else None
    x = mallet(NOTES.get(height, 587.3), dur, tau, bright, sweep)
    if hit:
        tk = tick(0.004, 0.25); x[:len(tk)] += tk
    else:
        x = sakit.lowpass(x, 1400)
    # equal loudness across heights (the low note needs more level to sound as loud), hits at -24 LUFS, blocks 5 dB under
    target = -24.0 if hit else -29.0
    pad = np.concatenate([x, np.zeros(int(0.4 * SR))])   # BS.1770 needs 400 ms blocks
    x = x * 10 ** ((target - sakit.lufs_integrated(pad)) / 20)
    peak = np.max(np.abs(x))
    if peak > 10 ** (-9 / 20): x *= 10 ** (-9 / 20) / peak    # never above -9 dBFS in the file
    return x


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for h in ('high', 'mid', 'low', 'over'):
        for k in ('hit', 'block'):
            p = os.path.join(OUT, f'height_{h}_{k}.wav')
            sakit.write(p, make(h, k))
            print(p)
