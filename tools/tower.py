"""Challenge Tower bot on the TEST saves (work\\testsaves): open the tower, list the open challenges' cards, start one.
python tower.py list            -> speaks/prints each card (Up through the open ones)
python tower.py play N          -> scroll to challenge N and start it (Select, then Continue through the intro)
Run from a fresh start; the game window must be in front (the bot takes the input lock)."""
import sys, time, os
sys.path.insert(0, os.path.dirname(__file__))
import mk9, drive
from drive import press, press_until, wait_say, said_since, log_size


def open_tower():
    mk9.lock()
    if not drive.launch_test(): raise SystemExit('no main menu')
    mk9.focus()
    out = press_until('down', 'Challenge Tower', 10, 1.2)
    if not out: raise SystemExit('no Challenge Tower item')
    out = press('j', 6)
    print('open:', out)
    return out


def card_number(lines):
    for s in lines:
        if s.startswith('Challenge '):
            try: return int(s.split()[1].rstrip(','))
            except ValueError: pass
    return None


def goto(n, tries=40):
    """the open auto-scrolls to the current challenge; Up goes to higher numbers in the tower?  try both ways"""
    pos = log_size()
    cur = None
    for s in said_since(0)[-6:]:
        c = card_number([s]); cur = c or cur
    for _ in range(tries):
        if cur == n: return True
        key = 'up' if (cur or 0) < n else 'down'
        out = press(key, 1.4)
        c = card_number(out)
        print(key, out)
        if c is None:
            key2 = 'down' if key == 'up' else 'up'
            out = press(key2, 1.4); c = card_number(out); print(key2, out)
        cur = c if c else cur
    return cur == n


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'list'
    open_tower()
    if what == 'list':
        for _ in range(12): print(press('up', 1.4))
    elif what == 'play':
        n = int(sys.argv[2])
        print('at', n, goto(n))
        print(press('j', 5))
