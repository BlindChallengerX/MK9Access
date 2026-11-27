"""Where a dub's voice speaks in each Story movie, from the movie's own audio.

Every movie carries five dubs (ger, spa, ita, fre, eng) over the same music and effects. Subtracting another dub
from the wanted one cancels the shared music and effects and leaves the two voices; the quietest of the four
differences at each moment is loud only while the wanted voice itself speaks (at least one other dub is silent then).
Measured on mk001_002_003 (Spanish): voice frames -18 to -30 dBFS, silence -50 to -120 dBFS, and the found spans match
the game's MK9Story.spa subtitle rows.

Writes work/story_<lang>/voice/<movie>.txt: one "start<TAB>end" line (seconds) per spoken span.
Usage: python -I voice_track.py [--lang spa] [movie ...]     (no names: every Movies/mk*.wmv)"""
import av, os, sys, numpy as np

ROOT = r"C:\Users\michm\Downloads\MK9 Access"
MOV = os.path.join(ROOT, "game", "game_root", "Movies")
LANGS = ("ger", "spa", "ita", "fre", "eng")
SR, HOP = 16000, 0.02
ON_DB, MERGE, MIN_SPAN = -42.0, 0.35, 0.12


def tracks(name):
    out = {}
    for lang in LANGS:
        c = av.open(os.path.join(MOV, name + ".wmv"))
        st = [s for s in c.streams.audio if s.language == lang][0]
        rs = av.AudioResampler(format="flt", layout="mono", rate=SR)
        buf = []
        for fr in c.decode(st):
            for f2 in rs.resample(fr):
                buf.append(f2.to_ndarray().reshape(-1))
        c.close()
        out[lang] = np.concatenate(buf)
    n = min(len(v) for v in out.values())
    return {k: v[:n] for k, v in out.items()}


def voice_spans(name, lang):
    t = tracks(name)
    h = int(HOP * SR); n = len(t[lang]) // h
    me = t[lang][:n * h]
    res = [np.sqrt((((me - t[o][:n * h]) ** 2).reshape(n, h)).mean(1)) for o in LANGS if o != lang]
    db = 20 * np.log10(np.min(np.array(res), 0) + 1e-9)
    on = db > ON_DB
    spans, i = [], 0
    while i < n:
        if on[i]:
            j = i
            while j < n and on[j]: j += 1
            spans.append([i * HOP, j * HOP]); i = j
        else:
            i += 1
    merged = []
    for s in spans:
        if merged and s[0] - merged[-1][1] < MERGE: merged[-1][1] = s[1]
        else: merged.append(s)
    return [(a, b) for a, b in merged if b - a >= MIN_SPAN]


def load(name, lang="spa"):
    p = os.path.join(ROOT, "work", "story_" + lang, "voice", name.lower() + ".txt")
    if not os.path.exists(p): return None
    return [tuple(map(float, l.split("\t"))) for l in open(p) if l.strip()]


def main(args):
    lang = "spa"
    if "--lang" in args:
        i = args.index("--lang"); lang = args[i + 1]; del args[i:i + 2]
    names = args or sorted(f[:-4] for f in os.listdir(MOV) if f.lower().startswith("mk") and f.endswith(".wmv"))
    d = os.path.join(ROOT, "work", "story_" + lang, "voice"); os.makedirs(d, exist_ok=True)
    for name in names:
        sp = voice_spans(name, lang)
        with open(os.path.join(d, name.lower() + ".txt"), "w") as o:
            for a, b in sp: o.write(f"{a:.2f}\t{b:.2f}\n")
        print(f"{name}: {len(sp)} spans, {sum(b - a for a, b in sp):.0f} s of voice", flush=True)


if __name__ == "__main__":
    main(sys.argv[1:])
