"""Extract every fighter's move list from the game packages into data/movelists/<package>.txt.
Run: python movelists.py   (needs ue3unpack.exe built from ue3unpack.cpp)"""
import os, re, subprocess, glob, sys, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
GAME = r"C:\Users\michm\Downloads\MK9 Access\game\game_root"
TMP = r"C:\Users\michm\Downloads\MK9 Access\work\pk"
OUT = os.path.join(HERE, "..", "data", "movelists")
SECTIONS = ["BASIC ATTACKS", "KOMBO ATTACKS", "FAST TAGS", "TAG KOMBOS", "SPECIAL ATTACKS", "ENHANCED ATTACKS",
            "TAG SPECIALS", "X-RAY ATTACK", "FINISHING MOVES"]

def strings(blob, start):
    """null-separated printable strings from start, stopping at the first non-text run"""
    out = []; i = start
    while i < len(blob):
        j = blob.find(b"\0", i)
        if j < 0: break
        s = blob[i:j]
        if s:
            if not all(32 <= c < 127 for c in s): break
            out.append(s.decode())
        i = j + 1
    return out

def parse(tokens):
    """-> list of (section, [moves]); move = dict(name, input, notes)"""
    secs = []; cur = None; move = None; junk = 0
    for t in tokens:
        if t not in SECTIONS and "[" not in t and t.isupper() and len(t) > 3 and not t.startswith("("):
            if any(sec == "X-RAY ATTACK" for sec, _ in secs): break   # boss lists run into TAG SWAP, GORO...
            continue
        if t in SECTIONS:
            cur = (t, []); secs.append(cur); move = None; continue
        if cur is None: continue
        if t.startswith("(Uses"):           # "(Uses one bar of super.)" belongs to the section
            cur[1].append({"name": None, "input": None, "notes": [t.strip("()").rstrip(".")]}); continue
        if "[" in t and t.strip().startswith("["):
            if move and move["input"] is None: move["input"] = t
            elif move: move["notes"].append(t)
            continue
        fin = secs[-1][0] == "FINISHING MOVES"
        is_note = t.endswith(".") or fin and (t in ("Touching", "Sweep", "Jump", "Varies", "Fullscreen", "Any") or t.endswith(" range"))
        if is_note and move: move["notes"].append(t.rstrip(".")); continue
        camel = " " not in t and re.search(r"[a-z][A-Z]", t) is not None
        if len(t) > 40 or "_" in t or "." in t or camel or t[0].islower() or (t.isupper() and len(t) > 3 and fin):
            junk += 1
            if (fin and secs[-1][1]) or junk >= 40: break
            continue
        junk = 0
        move = {"name": t, "input": None, "notes": []}; cur[1].append(move)
    return secs

def main():
    os.makedirs(OUT, exist_ok=True); os.makedirs(TMP, exist_ok=True)
    pkgs = sorted(glob.glob(os.path.join(GAME, "Asset", "CHAR_*.xxx")))
    pkgs += glob.glob(os.path.join(GAME, "dlc", "**", "CHAR_*.xxx"), recursive=True)
    seen = {}
    for p in pkgs:
        name = os.path.basename(p)[5:-4]
        if name in seen and "dlc" in p: continue
        subprocess.run([os.path.join(HERE, "ue3unpack.exe"), TMP, p], capture_output=True)
        b = os.path.join(TMP, os.path.basename(p) + ".bin")
        blob = open(b, "rb").read(); os.remove(b)
        if not blob: blob = open(p, "rb").read()       # uncompressed package (Kratos)
        i = blob.find(b"BASIC ATTACKS\0")
        if i < 0: print(name, "no move list"); continue
        secs = parse(strings(blob, i))
        lines = []
        for sec, moves in secs:
            lines.append("# " + sec)
            for m in moves:
                if m["name"] is None: lines.append("  note: " + "; ".join(m["notes"])); continue
                lines.append("%s | %s%s" % (m["name"], m["input"] or "", (" | " + "; ".join(m["notes"])) if m["notes"] else ""))
        text = "\n".join(lines) + "\n"
        h = hashlib.md5(text.encode()).hexdigest()
        open(os.path.join(OUT, name + ".txt"), "w", encoding="utf-8").write(text)
        seen[name] = h
        print(name, sum(len(m) for _, m in secs), "moves", "(same as " + [k for k, v in seen.items() if v == h and k != name][0] + ")" if list(seen.values()).count(h) > 1 else "")

main()
