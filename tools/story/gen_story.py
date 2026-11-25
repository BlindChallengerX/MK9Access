"""Record the Story Mode descriptions: data/story/<movie>.txt -> out/MK9Access/story/<movie>_<n>.wav + cues.tsv.

A description file holds one line per described moment, "SECONDS<TAB>text" (seconds from the movie's first picture;
# comments). Each clip must end before the next subtitle line (the game's MK9Story.eng timings) or the next
description: spoken at SPEED, re-spoken faster up to 1.45x when it would run over, reported when it still does.
Voice: Kokoro af_heart (the house description voice, as the fatality clips). Level ~-13 dBFS while speaking,
peaks under -1 dBFS, 5 ms fades, no compression.
cues.tsv rows: movie<TAB>seconds<TAB>clip<TAB>text (the text is spoken by the screen reader when the player picks
"Descriptions: screen reader", Alt+F12).
Usage: python -I gen_story.py [movie ...]      (no names: every data/story/*.txt; cues.tsv is always rewritten)
       python -I gen_story.py --index-only     (rewrites cues.tsv only, keeps the clips)"""
import os, re, sys, numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from prep import story_sections
import av
from kokoro_onnx import Kokoro

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "data", "story")
OUT = os.path.join(HERE, "..", "..", "out", "MK9Access", "story")
MOV = r"C:\Users\michm\Downloads\MK9 Access\game\game_root\Movies"
KDIR = r"C:\Users\michm\tools\tts\kokoro"
SPEED, MAXSPEED, MARGIN = 1.15, 1.45, 0.15

LEXICON = {
    "Sub-Zero": "sˈʌbzˈiəɹoʊ", "Scorpion": "skˈɔːɹpiən", "Kung Lao": "kˈʊŋ lˈaʊ",
    "Liu Kang": "lˈuː kˈæŋ", "Kitana": "kɪtˈɑːnə", "Mileena": "mɪlˈiːnə",
    "Jade": "dʒˈeɪd", "Sindel": "sˈɪndɛl", "Ermac": "ˈɜːɹmæk", "Kabal": "kˈɑːbɑːl",
    "Sektor": "sˈɛktɔːɹ", "Cyrax": "sˈaɪɹæks", "Baraka": "bɑːɹˈɑːkə", "Sheeva": "ʃˈiːvə",
    "Quan Chi": "kwˈɑːn tʃˈiː", "Shang Tsung": "ʃˈɑːŋ sˈʌŋ", "Noob Saibot": "nˈuːb sˈaɪbɑːt",
    "Nightwolf": "nˈaɪtwʊlf", "Stryker": "stɹˈaɪkɚ", "Kenshi": "kˈɛnʃiː",
    "Skarlet": "skˈɑːɹlɛt", "Raiden": "ɹˈeɪdən", "Reptile": "ɹˈɛptaɪl",
    "Cyber Sub-Zero": "sˈaɪbɚ sˈʌbzˈiəɹoʊ", "Kombat": "kˈɑːmbæt",
    "Shao Kahn": "ʃˈaʊ kˈɑːn", "Kintaro": "kɪntˈɑːɹoʊ", "Goro": "ɡˈɔːɹoʊ", "Motaro": "moʊtˈɑːɹoʊ",
    "Kano": "kˈɑːnoʊ", "Sonya": "sˈoʊnjə", "Jarek": "dʒˈæɹɛk", "Edenia": "ɛdˈiːniə",
    "Shokan": "ʃˈoʊkæn", "Tarkatan": "tˈɑːɹkətæn", "Tarkatans": "tˈɑːɹkətænz", "Lin Kuei": "lˈɪn kwˈeɪ",
    "Shirai Ryu": "ʃɪɹˈaɪ ɹˈjuː", "Hanzo Hasashi": "hˈɑːnzoʊ hɑːsˈɑːʃi", "Smoke": "smˈoʊk",
    "Netherrealm": "nˈɛðɚɹɛlm", "Kuai Liang": "kwˈaɪ lˈjɑːŋ", "Bi-Han": "bˈiː hˈɑːn",
}
_NAME_RE = re.compile("(?<![A-Za-z])(" + "|".join(sorted(map(re.escape, LEXICON), key=len, reverse=True)) + ")('s)?(?![A-Za-z])")
k = None

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
    a, sr = k.create(phonemes(txt), voice="af_heart", speed=speed, lang="en-us", is_phonemes=True)
    a = np.asarray(a, np.float32)
    nz = np.where(np.abs(a) > 0.01)[0]
    if len(nz): a = a[max(0, nz[0] - 120): nz[-1] + 1200]
    return a, sr

def level(a, sr):
    v = a[np.abs(a) > 0.02]
    rms = np.sqrt(np.mean(v ** 2)) if len(v) else 0.1
    a = a * (0.22 / rms)
    pk = np.max(np.abs(a))
    if pk > 0.89: a = a * (0.89 / pk)
    n = int(0.005 * sr); ramp = np.linspace(0, 1, n)
    a[:n] *= ramp; a[-n:] *= ramp[::-1]
    return a

def read_cues(name):
    cues = []
    for line in open(os.path.join(SRC, name + ".txt"), encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"): continue
        t, txt = re.split(r"\s+", line, 1)
        cues.append((float(t), txt.strip()))
    return sorted(cues)

def main(names):
    global k
    allnames = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".txt"))
    index_only = "--index-only" in names
    names = [n for n in names if not n.startswith("--")]
    todo = [] if index_only else names or allnames
    secs = story_sections()
    os.makedirs(OUT, exist_ok=True)
    if todo: k = Kokoro(os.path.join(KDIR, "kokoro-v1.0.onnx"), os.path.join(KDIR, "voices-v1.0.bin"))
    late = total = 0
    for name in todo:
        for f in os.listdir(OUT):
            if f.startswith(name + "_") and f.endswith(".wav"): os.remove(os.path.join(OUT, f))
        cues = read_cues(name)
        rows = secs.get(name.lower(), [])
        c = av.open(os.path.join(MOV, name + ".wmv")); dur = c.duration / 1e6; c.close()
        for i, (t, txt) in enumerate(cues):
            limit = min([s for s, e, _ in rows if s > t + 0.05] + [cues[i + 1][0] if i + 1 < len(cues) else dur, dur])
            talking = [x for s, e, x in rows if s <= t < e - 0.1]
            if talking: print(f"  ! {name} {t:.1f}: starts while someone speaks: {talking[0][:40]}")
            room = limit - t - MARGIN
            speed = SPEED; a, sr = speak(txt, speed)
            while len(a) / sr > room and speed < MAXSPEED:
                speed = min(MAXSPEED, speed + 0.1); a, sr = speak(txt, speed)
            if len(a) / sr > room + 0.25:
                late += 1; print(f"  LATE {name} {t:6.1f}: {len(a)/sr:.1f}s in {room:.1f}s room: {txt}")
            elif speed > SPEED + 0.05: print(f"  sped {speed:.2f} {name} {t:6.1f}: {txt[:60]}")
            sf.write(os.path.join(OUT, f"{name}_{i}.wav"), level(a, sr), sr, subtype="PCM_16")
            total += 1
    with open(os.path.join(OUT, "cues.tsv"), "w", encoding="utf-8", newline="\n") as o:
        o.write("# movie\tseconds\tclip\ttext  (tools/story/gen_story.py from data/story)\n")
        for name in allnames:
            for i, (t, txt) in enumerate(read_cues(name)):
                o.write(f"{name.lower()}\t{t:.2f}\t{name}_{i}.wav\t{txt.replace(chr(9), ' ')}\n")
    print(total, "clips recorded,", late, "still run over")

if __name__ == "__main__":
    main(sys.argv[1:])
