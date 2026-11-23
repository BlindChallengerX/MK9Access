"""Prepare the Story movies for describing: for each game_root/Movies/mk*.wmv write work/story/<movie>/
lines.txt (the game's own subtitle lines from MK9Story.eng, frames at 30 fps -> seconds, and the silent gaps) and
sheet*.jpg contact sheets (one picture a second in and just before gaps, one every 2 s while people talk).
Usage: python -I prep.py [movie ...]"""
import av, os, re, sys
from PIL import Image, ImageDraw
ROOT = r"C:\Users\michm\Downloads\MK9 Access"
MOV = os.path.join(ROOT, "game", "game_root", "Movies")
OUT = os.path.join(ROOT, "work", "story")
GAP = 1.5

def story_sections():
    t = open(os.path.join(ROOT, "game", "game_root", "Localization", "Coalesced.eng"), "rb").read().decode("cp1252", "ignore")
    body = t[t.find("MK9Story.eng"):]
    body = body[:body.find("..\MK9Game", 20)] if "..\MK9Game" in body[20:] else body
    secs = {}
    for name, b in re.findall(r"\[([A-Za-z0-9_]+)\]\r?\n((?:\d+=\d+,\d+,[^\n]*\n)+)", body):
        rows = []
        for l in b.strip().split("\n"):
            k, v = l.split("=", 1); s, e, txt = v.split(",", 2)
            rows.append((int(s) / 30, int(e) / 30, txt.strip().replace("\n", " ")))
        secs[name.lower()] = rows
    return secs

def main(names):
    secs = story_sections()
    for f in names:
        name = os.path.splitext(f)[0]; rows = secs.get(name.lower(), [])
        c = av.open(os.path.join(MOV, f)); dur = c.duration / 1e6
        gaps = []; prev = 0.0
        for s, e, _ in rows + [(dur, dur, None)]:
            if s - prev >= GAP: gaps.append((prev, s))
            prev = max(prev, e)
        d = os.path.join(OUT, name); os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "lines.txt"), "w", encoding="utf-8") as o:
            o.write(f"movie {name}  duration {dur:.1f}s\n\nDIALOGUE (start-end seconds):\n")
            for s, e, txt in rows: o.write(f"{s:6.1f}-{e:6.1f}  {txt}\n")
            o.write(f"\nSILENT GAPS of {GAP}s or more (start-end, length):\n")
            for a, b in gaps: o.write(f"{a:6.1f}-{b:6.1f}  {b-a:4.1f}s\n")
        dense = lambda t: any(a - 3 <= t <= b for a, b in gaps)
        v = c.streams.video[0]; frames = []; nxt = 0.0
        for fr in c.decode(v):
            t = float(fr.pts * v.time_base)
            if t >= nxt:
                frames.append((t, fr.to_image().resize((426, 240))))
                nxt = t + (1.0 if dense(t) else 2.0)
        c.close()
        for old in os.listdir(d):
            if old.startswith("sheet"): os.remove(os.path.join(d, old))
        for i in range(0, len(frames), 12):
            sheet = Image.new("RGB", (426 * 4, 240 * 3)); dr = ImageDraw.Draw(sheet)
            for kk, (t, im) in enumerate(frames[i:i + 12]):
                x, y = (kk % 4) * 426, (kk // 4) * 240; sheet.paste(im, (x, y))
                dr.rectangle([x, y, x + 62, y + 18], fill=(0, 0, 0)); dr.text((x + 3, y + 3), f"{t:6.1f}s", fill=(255, 255, 0))
            sheet.save(os.path.join(d, f"sheet{i // 12:02d}.jpg"), quality=82)
        print(f"{name}: {dur:.0f}s, {len(rows)} lines, {len(gaps)} gaps, {len(frames)} frames", flush=True)

if __name__ == "__main__":
    main(sys.argv[1:] or sorted(f for f in os.listdir(MOV) if f.lower().startswith("mk") and f.endswith(".wmv")))
