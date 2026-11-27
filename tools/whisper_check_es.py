"""Whisper-check the Spanish description clips: transcribe each clip (faster-whisper, multilingual model, Spanish) and
compare with the text it should say, letter by letter after folding Spanish spellings that sound alike.
Prints the clips under THRESH similarity, worst first, and writes work/whisper_es_<set>.tsv with every score.
Usage: python -I whisper_check_es.py fatalities|story [model]      (model default: base; small is better if cached)"""
import difflib, os, re, sys, unicodedata
from faster_whisper import WhisperModel

HERE = os.path.dirname(os.path.abspath(__file__))
OUTD = os.path.join(HERE, "..", "out", "MK9Access")
THRESH = 0.75


def norm(s):
    """letters only, folded by sound (Whisper glues short Spanish words together - "Jade Lopatea" - and spells
    Castilian sounds its own way: z/c/s, b/v, ll/y, silent h, k/qu/c, j/g)"""
    s = unicodedata.normalize("NFD", s.lower())
    s = re.sub(r"[^a-zñ]", "", "".join(c for c in s if unicodedata.category(c) != "Mn"))
    for x, y in (("qu", "k"), ("ce", "se"), ("ci", "si"), ("z", "s"), ("v", "b"), ("ll", "y"), ("h", ""), ("c", "k"),
                 ("x", "ks"), ("w", "u"), ("j", "y"), ("g", "y")): s = s.replace(x, y)
    return s


def rows(which):
    if which == "fatalities":
        d = os.path.join(OUTD, "fatalities_es")
        for l in open(os.path.join(d, "index.tsv"), encoding="utf-8"):
            c = l.rstrip("\n").split("\t")
            if len(c) >= 6: yield os.path.join(d, c[2]), c[5]
        for l in open(os.path.join(d, "stages.tsv"), encoding="utf-8"):
            c = l.rstrip("\n").split("\t")
            if len(c) >= 3: yield os.path.join(d, c[1]), c[2]
    else:
        d = os.path.join(OUTD, "story_es")
        for l in open(os.path.join(d, "cues.tsv"), encoding="utf-8"):
            if l.startswith("#"): continue
            c = l.rstrip("\n").split("\t")
            if len(c) >= 4: yield os.path.join(d, c[2]), c[3]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    which = sys.argv[1]; model = sys.argv[2] if len(sys.argv) > 2 else "base"
    m = WhisperModel(model, device="cpu", compute_type="int8")
    res = []
    for path, text in rows(which):
        segs, _ = m.transcribe(path, language="es", beam_size=5, vad_filter=False)
        heard = " ".join(s.text for s in segs).strip()
        r = difflib.SequenceMatcher(None, norm(text), norm(heard)).ratio()
        res.append((r, os.path.basename(path), text, heard))
    out = os.path.join(HERE, "..", "..", "work", f"whisper_es_{which}.tsv")
    with open(out, "w", encoding="utf-8") as o:
        for r, f, t, h in res: o.write(f"{r:.2f}\t{f}\t{t}\t{h}\n")
    bad = sorted(x for x in res if x[0] < THRESH)
    for r, f, t, h in bad: print(f"{r:.2f} {f}\n   want: {t}\n   hear: {h}")
    print(f"{len(res)} clips, {len(bad)} under {THRESH}, mean {sum(x[0] for x in res) / max(1, len(res)):.2f}")


if __name__ == "__main__":
    main()
