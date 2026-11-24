"""Challenge Tower challenge 5 (Training: Finishing Techniques, Sonya vs Baraka) on the TEST saves: from the
challenge's "Continue? Retry" screen or its card, start it, poke Baraka's health low, hit him, and at Finish Him
perform Scissor Split (Down Down Back Forward FP at jump range). Prints what the mod said.
Sonya starts on the left, so Back = A, Forward = D."""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
import mk9, drive, cmd

mk9.attach(); mk9.lock(); mk9.focus()
pos = drive.log_size()
if '--now' not in sys.argv: print(drive.press('j', 3))
def state(): return mk9.g32(mk9.g32(0x833848D4) + 4)
t = time.time()
while state() != 8 and time.time() - t < 60: drive.press('j', 2)
time.sleep(4)
LOG = drive.LOG
def finishes():
    with open(LOG, 'rb') as f: f.seek(pos); return f.read().decode('utf-8', 'replace').count('fatality: finish him')
# each round: Baraka's health poked low and a few hits; the challenge's Finish Him is in the last round
for rnd in range(3):
    n0 = finishes()
    obj = mk9.g32(0x833C6C54 + 0xC8C + 0xDF4)
    cmd.cmd('poke %x 3c23d70a' % (obj + 0x78C0), wait=0.2)
    t = time.time()
    while time.time() - t < 10 and finishes() == n0:
        mk9.key('u', check=False); mk9.key('i', check=False)
    print('round', rnd, drive.said_since(pos))
    time.sleep(1.5)
    o = mk9.g32(0x833C6C54 + 0xC8C + 0xDF4)
    if mk9.gf(o + 0x78C0) <= 0.02: break   # still down: this is Finish Him
    print('health back to', mk9.gf(o + 0x78C0))
    time.sleep(3)
def gap():
    xs = []
    for p in (0, 1):
        o = mk9.g32(0x833C6C54 + p * 0xC8C + 0xDF4); a = mk9.g32(o + 8); xs.append(mk9.gf(a + 0xE0))
    return xs[1] - xs[0]
time.sleep(0.8)
g0 = gap(); print('gap', g0)
back, fwd = ('a', 'd') if g0 > 0 else ('d', 'a')
want = float(os.environ.get('GAP', '230'))
t = time.time()
mk9.shot('fat_a.png')
mk9.focus()
for _ in range(12):
    if abs(gap()) >= want - 20: break
    mk9.key(back, hold=0.07, gap=0.0)
mk9.shot('fat_b.png')
print('gap', gap())
time.sleep(0.15)
for k in ('s', 's', back, fwd): mk9.key(k, hold=0.06, gap=0.05, check=False)
mk9.key('u', hold=0.08, check=False)
time.sleep(16)
print(drive.said_since(pos))
