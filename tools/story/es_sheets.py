"""Worksheets for writing the Spanish Story descriptions: work/story_spa/sheets/<movie>.txt.

Each English cue (data/story/<movie>.txt) gets a time re-fitted to the Spanish dub and the room it has there:
the Spanish voice spans come from voice_track.py (the movie's own Spanish track), joined with the game's MK9Story.spa
subtitle rows. A cue that falls inside Spanish speech moves to just after it (at most MAXSHIFT s later, never past the
next cue); otherwise it keeps its English time. Room = time to the next Spanish speech, next cue or the movie's end.
The word budget assumes the Spanish voice at about WPS words a second (ef_dora measured ~2.9 w/s at speed 1.15; the
recorder may speed up to 1.45x, so the budget leaves headroom).
Usage: python -I es_sheets.py [movie ...]"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import av
from voice_track import load as load_voice

ROOT = r"C:\Users\michm\Downloads\MK9 Access"
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "data", "story")
MOV = os.path.join(ROOT, "game", "game_root", "Movies")
OUT = os.path.join(ROOT, "work", "story_spa", "sheets")
MAXSHIFT, MARGIN, WPS, WIDEN = 3.0, 0.15, 3.0, 0.6


def sections(lang):
    ext = {"spa": "spa", "eng": "eng"}[lang]
    t = open(os.path.join(ROOT, "game", "game_root", "Localization", "Coalesced." + ext), "rb").read().decode("cp1252", "ignore")
    body = t[t.find("MK9Story." + ext):]
    j = body.find("..\\MK9Game", 20)
    if j > 0: body = body[:j]
    secs = {}
    for name, b in re.findall(r"\[([A-Za-z0-9_]+)\]\r?\n((?:\d+=\d+,\d+,[^\n]*\n)+)", body):
        rows = []
        for l in b.strip().split("\n"):
            k, v = l.split("=", 1); s, e, txt = v.split(",", 2)
            rows.append((int(s) / 30, int(e) / 30, txt.strip().replace("\\n", " ")))
        secs[name.lower()] = rows
    return secs


def read_cues(name):
    cues = []
    for line in open(os.path.join(SRC, name + ".txt"), encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"): continue
        t, txt = re.split(r"\s+", line, 1)
        cues.append((float(t), txt.strip()))
    return sorted(cues)


def speech_spans(name, subs):
    """Spanish speech: the track's voice, kept only inside the Spanish subtitle rows widened by WIDEN s (the rows are
    a little off; the voice gives the real edges). Voice away from any row is fight grunts and efforts, which the
    English cues also talk over. Subtitle rows stand in when no voice file exists."""
    v = load_voice(name, "spa")
    if v is None: return [(s, e) for s, e, _ in subs]
    out = []
    for a, b in v:
        for s, e, _ in subs:
            lo, hi = max(a, s - WIDEN), min(b, e + WIDEN)
            if hi - lo > 0.1: out.append((lo, hi))
    out.sort()
    merged = []
    for a, b in out:
        if merged and a <= merged[-1][1] + 0.1: merged[-1] = (merged[-1][0], max(merged[-1][1], b))
        else: merged.append((a, b))
    return merged


def fit(name, subs, dur):
    """Each cue keeps its English time unless that falls inside Spanish speech; then it moves to whichever of
    'just after that speech' and 'early enough to finish before it' leaves more room (at most MAXSHIFT s away)."""
    cues = read_cues(name)
    sp = speech_spans(name, subs)
    out = []; prev = -1.0
    def room_at(t, nxt):
        if any(a - 0.05 <= t < b for a, b in sp): return 0.0
        return min([a for a, _ in sp if a > t] + [nxt, dur]) - t - MARGIN
    for i, (t, txt) in enumerate(cues):
        nxt = cues[i + 1][0] if i + 1 < len(cues) else dur
        cands = [(room_at(t, nxt), t, "")]
        inside = [(a, b) for a, b in sp if a - 0.05 <= t < b]
        if inside:
            a0, b0 = inside[0]
            after = round(b0 + 0.1, 2)
            if after - t <= MAXSHIFT and after < nxt - 0.8: cands.append((room_at(after, nxt), after, f"moved from {t:.2f}"))
            before_end = max([b for _, b in sp if b <= a0] + [0.0])
            before = round(max(before_end + 0.1, t - MAXSHIFT, prev + 1.0), 2)
            if before < t: cands.append((room_at(before, nxt), before, f"moved from {t:.2f}"))
        room, t2, note = max(cands, key=lambda c: (c[0], -abs(c[1] - t)))
        if room <= 0.3: note = "INSIDE SPANISH SPEECH (could not move)"; room = max(room, 0.0)
        out.append((t2, room, txt, note)); prev = t2
    return out


def main(names):
    subs = sections("spa")
    os.makedirs(OUT, exist_ok=True)
    allnames = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".txt"))
    tight = moved = stuck = 0
    for name in names or allnames:
        c = av.open(os.path.join(MOV, name + ".wmv")); dur = c.duration / 1e6; c.close()
        rows = subs.get(name.lower(), [])
        fitted = fit(name, rows, dur)
        with open(os.path.join(OUT, name + ".txt"), "w", encoding="utf-8", newline="\n") as o:
            o.write(f"movie {name}  duration {dur:.1f}s\n\nSPANISH DIALOGUE (game subtitles, start-end s; the spoken line can be a little off):\n")
            for s, e, txt in rows: o.write(f"{s:6.1f}-{e:6.1f}  {txt}\n")
            o.write("\nCUES: time (use exactly) | room in seconds | most Spanish words that fit | English description\n")
            for t2, room, txt, note in fitted:
                words = max(3, int(room * WPS))
                o.write(f"{t2:.2f}\t| room {room:4.1f}s | max {words:2d} words | {txt}" + (f"   [{note}]" if note else "") + "\n")
                tight += len(txt.split()) > words; moved += note.startswith("moved"); stuck += note.startswith("INSIDE")
    print(f"sheets in {OUT}: {moved} cues moved, {stuck} stuck inside Spanish speech, {tight} where English is longer than the budget")


if __name__ == "__main__":
    main(sys.argv[1:])
