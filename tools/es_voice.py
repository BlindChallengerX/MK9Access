"""The Spanish description voice, shared by gen_fatalities.py --es and story/gen_story.py --es.

Kokoro voice ef_dora (the Spanish female voice, as af_heart is the English one), espeak Spanish ("es", the same
Castilian Spanish as the game's own Spanish text and dub). Names are said the way a Spanish dub says these English and
Chinese names (Johnny Cage = "Yoni Keich", Sub-Zero = "Sab Ziro"), spliced in as phonemes; espeak alone reads them as
Spanish words ("Xoni Caje", "Sub-cero").
Raiden = "Réiden" (the user's RAY-den); Liu Kang = espeak's own "Li-ú Kang" (Whisper heard "You can" for "Lyu Kang").
Set LANG = "es-419" for Latin American pronunciation (seseo) if players ask for it."""
import re

VOICE, LANG = "ef_dora", "es"
# Spanish at 1.45x garbled short lines in a Whisper check ("Liu Kang le da la espalda" -> "No, no, no"); 1.3 is the
# most the story recorder speeds up. Soft first sounds (K, T) fell under the trim threshold: keep 40 ms before it.
MAXSPEED, PREROLL = 1.3, 960

LEXICON = {
    "Cyber Sub-Zero": "sˈaiβeɾ sˈab sˈiɾo", "Sub-Zero": "sˈab sˈiɾo", "Scorpion": "eskˈoɾpjon",
    "Liu Kang": "liˈu kˈaŋ", "Raiden": "rˈeiden", "Johnny Cage": "ʝˈoni kˈeitʃ", "Cage": "kˈeitʃ", "Sonya Blade": "sˈonja blˈeid",
    "Sonya": "sˈonja", "Jax": "ʝˈaks", "Shang Tsung": "ʃˈaŋ tsˈuŋ", "Sheeva": "ʃˈiβa", "Sindel": "sˈindel",
    "Mileena": "milˈina", "Jade": "ʝˈeid", "Reptile": "rˈeptail", "Ermac": "ˈeɾmak", "Smoke": "esmˈouk",
    "Cyrax": "sˈaiɾaks", "Sektor": "sˈektoɾ", "Stryker": "estɾˈaikeɾ", "Nightwolf": "nˈaitwulf",
    "Noob Saibot": "nˈuβ sˈaiβot", "Noob": "nˈuβ", "Kenshi": "kˈenʃi", "Skarlet": "eskˈaɾlet",
    "Freddy Krueger": "fɾˈedi kɾˈuɡeɾ", "Freddy": "fɾˈedi", "Rain": "rˈein", "Lin Kuei": "lˈin kwˈei",
    "Shirai Ryu": "ʃiɾˈai rjˈu", "Shaolin": "ʃaolˈin", "Hanzo Hasashi": "ˈanso asˈaʃi", "Kuai Liang": "kwˈai ljˈaŋ",
    "Bi-Han": "bˈi xˈan", "Jarek": "ʝˈaɾek", "Kombat": "kˈombat", "Fatality": "fatˈaliti", "Babality": "baβˈaliti",
    "Cyber": "sˈaiβeɾ", "Kurtis": "kˈuɾtis",
}
_NAME_RE = re.compile("(?<![A-Za-zÁÉÍÓÚáéíóúÑñ])(" + "|".join(sorted(map(re.escape, LEXICON), key=len, reverse=True))
                      + ")(?![A-Za-zÁÉÍÓÚáéíóúÑñ])")


def phonemes(k, txt):
    out, pos = [], 0
    for m in _NAME_RE.finditer(txt):
        if m.start() > pos: out.append(k.tokenizer.phonemize(txt[pos:m.start()], LANG))
        out.append(LEXICON[m.group(1)])
        pos = m.end()
    if pos < len(txt): out.append(k.tokenizer.phonemize(txt[pos:], LANG))
    s = ""
    for o in (o.strip() for o in out if o.strip()):
        s += ("" if not s or o[0] in ",.;:!?" else " ") + o
    return s


# Fatality inputs in the game's Spanish words ([TutorialMissions] button names, [TowerMission] ranges).
DIRS = {"forward": "Adelante", "back": "Atrás", "down": "Abajo", "up": "Arriba",
        "hold block": "mantén Bloquear", "release block": "suelta Bloquear", "block": "Bloquear"}
BTN = {"1": "Puñetazo frontal", "2": "Puñetazo posterior", "3": "Patada frontal", "4": "Patada posterior"}
RANGE = {"close": "corto alcance", "mid": "alcance medio", "far": "largo alcance", "jump": "distancia de salto",
         "sweep": "distancia de barrido", "anywhere": "cualquier distancia"}


def input_words(s):
    s = s.strip(); rng = ""
    m = re.search(r"\(([^)]+)\)", s)
    if m:
        r = m.group(1).strip().lower()
        s = s[:m.start()].strip().rstrip(",").strip()
        rng = ", " + RANGE.get(r, r)
    parts = [p.strip() for p in s.split(",") if p.strip()]
    return ", ".join(BTN.get(p, DIRS.get(p.lower(), p)) for p in parts) + rng
