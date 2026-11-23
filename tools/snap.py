"""Guest memory snapshots of the running game (read from outside) and value tracking.
track(keys, deltas): press keys, keep addresses whose value moved by the given deltas (u8/u16/u32 big-endian)."""
import ctypes, ctypes.wintypes as w, numpy as np, time, sys, os
sys.path.insert(0, os.path.dirname(__file__)); import mk9
k = ctypes.windll.kernel32
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_ulonglong), ('AllocationBase', ctypes.c_ulonglong), ('AllocationProtect', w.DWORD),
                ('__a', w.DWORD), ('RegionSize', ctypes.c_ulonglong), ('State', w.DWORD), ('Protect', w.DWORD), ('Type', w.DWORD), ('__b', w.DWORD)]
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]

def regions(lo=0x100000000 + 0x40000000, hi=0x100000000 + 0xA0000000):
    """committed RW guest regions (default: guest 0x40000000-0xA0000000 heaps)"""
    out = []; a = lo; m = MBI()
    while a < hi and k.VirtualQueryEx(mk9.hproc, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
        if m.State == 0x1000 and m.Protect in (0x04, 0x40) and m.RegionSize < 0x40000000:
            out.append((m.BaseAddress, m.RegionSize))
        a = m.BaseAddress + m.RegionSize
    return out

def snap(regs):
    return {b: np.frombuffer(mk9.read(b, n) or b'\0' * n, dtype=np.uint8).copy() for b, n in regs}

def vals(a, width):
    n = len(a) // width * width
    t = '>u4' if width == 4 else '>u2' if width == 2 else 'u1'
    return a[:n].view(t).astype(np.int64)

def track(keys, deltas, width=4, regs=None, settle=0.6):
    """press each key; keep host addresses whose value changed by the matching delta every time"""
    regs = regs or regions()
    s0 = snap(regs); cand = None
    for key, d in zip(keys, deltas):
        mk9.key(key); time.sleep(settle)
        s1 = snap(regs); hit = set()
        for b in s0:
            v0, v1 = vals(s0[b], width), vals(s1[b], width)
            hit.update((np.nonzero((v1 - v0) == d)[0] * width + b).tolist())
        cand = hit if cand is None else cand & hit
        s0 = s1
        print(key, d, len(cand))
    return sorted(cand)

def show(addrs, width=4):
    for a in addrs[:40]:
        b = mk9.read(a, width)
        v = int.from_bytes(b, 'big') if b else None
        print(hex(a - 0x100000000), v)
