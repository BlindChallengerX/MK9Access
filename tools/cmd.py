"""Send RE commands to the running mod (MK9Access\\cmd.txt) and read results.
cmd('count arm'); counts() -> {guest_fn: n}; log_since(mark) -> new log lines."""
import os, time
MOD = r"C:\Users\michm\Downloads\MK9 Access\game\MK9Access"
LOG = os.path.join(MOD, 'MK9Access.log')


def cmd(*lines, wait=0.8):
    p = os.path.join(MOD, 'cmd.txt')
    t0 = time.time()
    while os.path.exists(p) and time.time() - t0 < 5: time.sleep(0.05)
    with open(p, 'w') as f: f.write('\n'.join(lines) + '\n')
    t0 = time.time()
    while os.path.exists(p) and time.time() - t0 < 5: time.sleep(0.05)
    time.sleep(wait)


def counts():
    p = os.path.join(MOD, 'counts.txt')
    if os.path.exists(p): os.remove(p)
    cmd('count dump', wait=0.3)
    t0 = time.time()
    while not os.path.exists(p) and time.time() - t0 < 10: time.sleep(0.1)
    time.sleep(0.5)
    out = {}
    for line in open(p):
        a, n = line.split(); out[int(a, 16)] = int(n)
    return out


def log_mark():
    return os.path.getsize(LOG)


def log_since(mark):
    with open(LOG, 'rb') as f:
        f.seek(mark); return f.read().decode('utf-8', 'replace').splitlines()
