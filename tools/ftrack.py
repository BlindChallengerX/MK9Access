"""Float search in guest memory (big-endian float32), for positions and similar.
steps: list of (key or None, hold seconds, rule) where rule(d) -> bool mask on the change since the last snapshot.
Example: walk = [(None, .5, still), ('right', .5, up), ('left', .5, down)]"""
import numpy as np, time, sys, os
sys.path.insert(0, os.path.dirname(__file__))
import mk9, snap


def fsnap(regs):
    out = {}
    for b, n in regs:
        raw = mk9.read(b, n) or b'\0' * n
        a = np.frombuffer(raw[:n // 4 * 4], dtype='>f4').astype(np.float64)
        out[b] = a
    return out


def search(steps, regs=None, cand=None):
    regs = regs or snap.regions()
    s0 = fsnap(regs)
    for key, hold, rule in steps:
        if key: mk9.key(key, hold=hold, gap=0.05)
        else: time.sleep(hold)
        time.sleep(0.15)
        s1 = fsnap(regs)
        new = {}
        for b in s0:
            d = s1[b] - s0[b]
            ok = np.isfinite(s1[b]) & np.isfinite(d) & rule(d) & (np.abs(s1[b]) < 1e6)
            idx = np.nonzero(ok)[0]
            if cand is not None: idx = np.intersect1d(idx, cand.get(b, np.array([], int)))
            new[b] = idx
        cand = new
        s0 = s1
        print(key, sum(len(v) for v in cand.values()))
    return cand, s0


def listing(cand, s, limit=40):
    out = []
    for b, idx in cand.items():
        for i in idx[:limit]: out.append((b + 4 * int(i) - mk9.GBASE, float(s[b][i])))
    return out


still = lambda d: d == 0
up = lambda d: d > 0.01
down = lambda d: d < -0.01
