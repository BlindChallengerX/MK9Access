"""Attack heights (high / mid / low / overhead) in Practice, for the attack height sounds.
Each probe: walk into range, do one attack as player 1, watch both fighters until it ends, and keep
  - the attacker's object (0x9000 bytes) at the attack's first frame and while it is active
  - what the dummy did: hit (action kind 0x46/0x47), blocked (0x0A at contact), or nothing (whiff)
Run with the game in a Practice fight (player 1 = the bot). Results go to work/hitlevel/<tag>.pkl."""
import os, sys, time, struct, pickle
sys.path.insert(0, os.path.dirname(__file__))
import mk9

GI, STRIDE, OBJ = 0x833C6C54, 0xC8C, 0xDF4
OUT = os.path.join(os.path.dirname(mk9.GAME), 'work', 'hitlevel')
os.makedirs(OUT, exist_ok=True)

JUMP_DELAY = 0.45
MOVES = [  # name, keys (player 1 faces right), pre-delay for jump-ins
    ('1', 'u'), ('2', 'i'), ('3', 'j'), ('4', 'k'),
    ('d1', 'down+u'), ('d2', 'down+i'), ('d3', 'down+j'), ('d4', 'down+k'),
    ('b1', 'B+u'), ('b2', 'B+i'), ('b3', 'B+j'), ('b4', 'B+k'),
    ('f2', 'F+i'), ('f4', 'F+k'),
    ('jk', 'JUMP+k'), ('jp', 'JUMP+i'),
    ('spear', 'B,B,u'), ('fire', 'down,B,i'), ('tele', 'down,B,j'), ('take', 'down,B,k'), ('throw', 'o'),
    ('str111', 'u,u,u'), ('str214', 'i,u,k'), ('str334', 'j,j,k'),
]


def obj(p): return mk9.g32(GI + p * STRIDE + OBJ)
def fstate(o):
    b = mk9.g(o + 0x188, 4); a = mk9.g(o + 0x1D4, 4); at = mk9.g(o + 0x2F74, 4); hp = mk9.g(o + 0x78C0, 4)
    return struct.unpack('>I', b)[0], struct.unpack('>I', a)[0], struct.unpack('>I', at)[0], struct.unpack('>f', hp)[0]
def xpos(p):
    o = obj(p); a = mk9.g32(o + 8); return mk9.gf(a + 0xE0)


def fwd(): return 'right' if xpos(1) > xpos(0) else 'left'
def back(): return 'left' if fwd() == 'right' else 'right'


def approach(target=110):
    for _ in range(40):
        d = abs(xpos(1) - xpos(0))
        if d <= target: return d
        mk9.key(fwd(), hold=0.12, gap=0.05, check=False)
    return abs(xpos(1) - xpos(0))


def probe(name, keys):
    o0, o1 = obj(0), obj(1)
    time.sleep(0.6)
    d0 = approach()
    time.sleep(0.5)
    s0 = fstate(o0)
    keys = keys.replace('F', fwd()).replace('B', back())
    if keys.startswith('JUMP+'):
        fv = mk9.vk_of(fwd())
        mk9._send(0x26, False); mk9._send(fv, False); time.sleep(0.08); mk9._send(fv, True); mk9._send(0x26, True)
        time.sleep(JUMP_DELAY)
        keys = keys[5:]
    if ',' in keys:
        for kk in keys.split(','): mk9.key(kk, hold=0.04, gap=0.02, check=False)
    else:
        mk9.key(keys, hold=0.07, gap=0.0, check=False)
    t0 = time.time(); rec = {'name': name, 'dist': d0, 'events': [], 'snaps': []}
    move = s0[0]; seen = set()
    hp1 = fstate(o1)[3]
    while time.time() - t0 < 1.6:
        m, act, att, hp = fstate(o0)
        dm, dact, datt, dhp = fstate(o1)
        ev = (m, act >> 8, att, dact >> 8, dact & 0xFF, round(dhp, 4))
        if not rec['events'] or rec['events'][-1][1:] != ev:
            rec['events'].append((round(time.time() - t0, 3),) + ev)
        if (dact >> 8) in (0x46, 0x47, 0x4F, 0x62) and len(rec.setdefault('dsnaps', [])) < 3:
            if not rec['dsnaps'] or time.time() - t0 - rec['dsnaps'][-1][0] > 0.05:
                rec['dsnaps'].append((time.time() - t0, dact, mk9.g(o1, 0x9000)))
        if m != 0xFFFFFFFF and (m, att) not in seen:
            seen.add((m, att))
            rec['snaps'].append((m, att, act, mk9.g(o0, 0x9000)))
    kinds = {e[4] for e in rec['events']}
    hit = any(k in (0x46, 0x47) for k in kinds) or rec['events'][-1][6] < hp1 - 1e-4
    blocked = 0x0A in kinds and not hit
    rec['result'] = 'hit' if hit else 'block' if blocked else 'whiff'
    return rec


def run(tag, moves=None, reps=1):
    reps = int(os.environ.get('REPS', reps))
    mk9.attach(); mk9.focus()
    out = []
    for name, keys in MOVES:
        if moves and name not in moves: continue
        for _ in range(reps):
            r = probe(name, keys)
            lv = [struct.unpack_from('>I', sn[3], 0x794C)[0] for sn in r['snaps']]
            dl = [hex(struct.unpack_from('>I', d[2], 0x7D84)[0]) for d in r.get('dsnaps', [])[:2]]
            print(f"{name:4} {r['result']:5} dlevel {dl} level {[hex(v) for v in lv]} dist {r['dist']:.0f} moves {[hex(s[0]) for s in r['snaps']][:4]} "
                  f"dummy acts {sorted({hex(e[4]<<8|e[5]) for e in r['events']})}", flush=True)
            out.append(r)
    with open(os.path.join(OUT, tag + '.pkl'), 'wb') as f: pickle.dump(out, f)
    return out


if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2].split(',') if len(sys.argv) > 2 else None)
