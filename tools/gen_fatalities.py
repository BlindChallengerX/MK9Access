"""Record described-fatality clips from data/fatalities.tsv.
Voice: Kokoro af_heart (the Tekken 3 AD voice), 24 kHz mono 16-bit, one gain per
clip (no compression - it crackled), ~-13 dBFS while speaking, peaks < -1 dBFS,
5 ms fades - the same pipeline as T3Access gen_ad.py.

Each clip says: "<Fatality>. <Description>. Input: <input in words>."
Outputs ../out/MK9Access/fatalities/<key>.wav and an index.tsv
(<name><TAB><input in words><TAB><key>.wav<TAB><character><TAB><raw input>) that the mod loads: the
name matches what the Fatality Tutorial localizes; character + raw input let it recognise a fatality
in a live match from the player's motion.
Usage: python gen_fatalities.py [speed]     (records every clip)
       python gen_fatalities.py --index-only (rewrites index.tsv, keeps the clips)
       python gen_fatalities.py --missing    (records only clips that don't exist yet, keeps the rest)
"""
import os, re, sys, numpy as np, soundfile as sf
from kokoro_onnx import Kokoro

KDIR = r"C:\Users\michm\tools\tts\kokoro"
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "data", "fatalities.tsv")
OUT = os.path.join(HERE, "..", "out", "MK9Access", "fatalities")
VOICE = "af_heart"
INDEX_ONLY = "--index-only" in sys.argv
MISSING = "--missing" in sys.argv
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
SPEED = float(_args[0]) if _args else 1.12
k = None if INDEX_ONLY else Kokoro(os.path.join(KDIR, "kokoro-v1.0.onnx"), os.path.join(KDIR, "voices-v1.0.bin"))

# MK character / term names Kokoro tends to mangle, as espeak-style IPA.
LEXICON = {
    "Sub-Zero": "sˈʌbzˈiəɹoʊ", "Scorpion": "skˈɔːɹpiən", "Kung Lao": "kˈʊŋ lˈaʊ",
    "Liu Kang": "lˈuː kˈæŋ", "Kitana": "kɪtˈɑːnə", "Mileena": "mɪlˈiːnə",
    "Jade": "dʒˈeɪd", "Sindel": "sˈɪndɛl", "Ermac": "ˈɜːɹmæk", "Kabal": "kˈɑːbɑːl",
    "Sektor": "sˈɛktɔːɹ", "Cyrax": "sˈaɪɹæks", "Baraka": "bɑːɹˈɑːkə", "Sheeva": "ʃˈiːvə",
    "Quan Chi": "kwˈɑːn tʃˈiː", "Shang Tsung": "ʃˈɑːŋ sˈʌŋ", "Noob Saibot": "nˈuːb sˈaɪbɑːt",
    "Nightwolf": "nˈaɪtwʊlf", "Stryker": "stɹˈaɪkɚ", "Kenshi": "kˈɛnʃiː",
    "Skarlet": "skˈɑːɹlɛt", "Raiden": "ɹˈeɪdən", "Reptile": "ɹˈɛptaɪl",
    "Cyber Sub-Zero": "sˈaɪbɚ sˈʌbzˈiəɹoʊ", "Kombat": "kˈɑːmbæt",
    "Kratos": "kɹˈeɪtoʊs", "Medusa": "mədˈuːsə",
}
_NAME_RE = re.compile("(?<![A-Za-z])(" + "|".join(sorted(LEXICON, key=len, reverse=True)) + ")('s)?(?![A-Za-z])")

def phonemes(txt):
    out, pos = [], 0
    for m in _NAME_RE.finditer(txt):
        if m.start() > pos: out.append(k.tokenizer.phonemize(txt[pos:m.start()], "en-us"))
        out.append(LEXICON[m.group(1)] + ("z" if m.group(2) else ""))
        pos = m.end()
    if pos < len(txt): out.append(k.tokenizer.phonemize(txt[pos:], "en-us"))
    s = ""
    for o in (o.strip() for o in out if o.strip()):
        s += ("" if not s or o[0] in ",.;:!?" else " ") + o
    return s

def speak(txt, speed):
    a, sr = k.create(phonemes(txt), voice=VOICE, speed=speed, lang="en-us", is_phonemes=True)
    a = np.asarray(a, np.float32)
    nz = np.where(np.abs(a) > 0.01)[0]
    if len(nz): a = a[max(0, nz[0] - 120): nz[-1] + 1200]
    return a, sr

def level(a, sr):
    v = a[np.abs(a) > 0.02]
    rms = np.sqrt(np.mean(v ** 2)) if len(v) else 0.1
    a = a * (0.22 / rms)               # ~ -13 dBFS while speaking
    pk = np.max(np.abs(a))
    if pk > 0.89: a = a * (0.89 / pk)
    n = int(0.005 * sr); ramp = np.linspace(0, 1, n)
    a[:n] *= ramp; a[-n:] *= ramp[::-1]
    return a

BTN = {"1": "front punch", "2": "back punch", "3": "front kick", "4": "back kick"}
def input_words(s):
    s = s.strip()
    # range note in parentheses -> "at X range"
    rng = ""
    m = re.search(r"\(([^)]+)\)", s)
    if m:
        r = m.group(1).strip().lower()
        s = s[:m.start()].strip().rstrip(",").strip()
        rng = " at %s range" % r if r in ("close", "mid", "far", "jump", "sweep") else " (%s)" % r
    # map lone 1-4 to button names
    parts = [p.strip() for p in s.split(",") if p.strip()]
    words = [BTN.get(p, p) for p in parts]
    return ", ".join(words) + rng

def norm(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())

def main():
    os.makedirs(OUT, exist_ok=True)
    if not INDEX_ONLY and not MISSING:
        for f in os.listdir(OUT):
            if f.endswith(".wav"): os.remove(os.path.join(OUT, f))
    rows = []
    with open(SRC, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"): continue
            cols = line.split("\t")
            if len(cols) < 4 or cols[0].strip().lower() == "character": continue
            rows.append([c.strip() for c in cols[:4]])
    index = []
    seen = set()
    for ch, name, inp, desc in rows:
        key = norm(name)
        if not key or key in seen:
            if key in seen: print("dup key, skipped:", name)
            continue
        seen.add(key)
        words = input_words(inp)
        # the clip is the description only (the Kokoro voice, played when the fatality
        # is performed); the mod speaks the name and input through the screen reader.
        text = desc.rstrip(".") + "."
        if not INDEX_ONLY and not (MISSING and os.path.exists(os.path.join(OUT, key + ".wav"))):
            if MISSING: print("recording", key)
            a, sr = speak(text, SPEED)
            sf.write(os.path.join(OUT, key + ".wav"), level(a, sr), sr, subtype="PCM_16")
        elif not os.path.exists(os.path.join(OUT, key + ".wav")):
            print("missing clip:", key)
        # index: name <TAB> input in words <TAB> clip <TAB> character <TAB> raw input <TAB> description (said by the
        # screen reader instead of the clip when the player picks "Descriptions: screen reader", Alt+F12)
        index.append("%s\t%s\t%s.wav\t%s\t%s\t%s" % (name, words, key, ch, inp, text.replace("\t", " ")))
    with open(os.path.join(OUT, "index.tsv"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(index) + "\n")
    print("indexed" if INDEX_ONLY else "generated", len(index), "fatality clips in", OUT)
    # stage fatalities: data/stage_fatalities.tsv (<stage key><TAB><description>) -> stage_<key>.wav + stages.tsv
    # (<stage key><TAB><clip><TAB><description>); the stage key is the game's BGND_<key>.xxx package name.
    ssrc = os.path.join(HERE, "..", "data", "stage_fatalities.tsv")
    if not os.path.exists(ssrc): return
    stages = []
    for line in open(ssrc, encoding="utf-8"):
        cols = line.rstrip("\n").split("\t")
        if len(cols) < 2 or line.startswith("#") or cols[0] == "stage": continue
        skey, desc = cols[0].strip(), cols[1].strip().rstrip(".") + "."
        clip = "stage_" + skey.lower() + ".wav"
        if not INDEX_ONLY and not (MISSING and os.path.exists(os.path.join(OUT, clip))):
            if MISSING: print("recording", clip)
            a, sr = speak(desc, SPEED)
            sf.write(os.path.join(OUT, clip), level(a, sr), sr, subtype="PCM_16")
        stages.append("%s\t%s\t%s" % (skey, clip, desc))
    with open(os.path.join(OUT, "stages.tsv"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(stages) + "\n")
    print(len(stages), "stage fatality clips")

if __name__ == "__main__":
    main()
