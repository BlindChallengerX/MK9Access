"""Menu driving helpers on top of mk9.py: wait for spoken text in MK9Access.log, press until something is said.
Test saves: launch_test() starts the game on work\\testsaves (a copy of the user's saves; never spend the real ones)."""
import os, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import mk9

LOG = os.path.join(mk9.GAME, 'MK9Access', 'MK9Access.log')
TESTSAVES = os.path.join(os.path.dirname(mk9.GAME), 'work', 'testsaves')


def log_size():
    try: return os.path.getsize(LOG)
    except OSError: return 0


def said_since(pos):
    """SAY lines written after byte offset pos"""
    try:
        with open(LOG, 'rb') as f:
            f.seek(pos); data = f.read().decode('utf-8', 'replace')
    except OSError:
        return []
    return [l.split('SAY', 1)[1].lstrip('+: ').strip() for l in data.splitlines() if ' SAY' in l]


def wait_say(text, pos=None, timeout=20):
    pos = log_size() if pos is None else pos
    t0 = time.time()
    while time.time() - t0 < timeout:
        for s in said_since(pos):
            if text.lower() in s.lower(): return s
        time.sleep(0.25)
    return None


def press(key, wait=1.2, hold=0.12):
    """press a key, return what was said in the next `wait` seconds"""
    pos = log_size()
    mk9.key(key, hold=hold)
    time.sleep(wait)
    return said_since(pos)


def press_until(key, text, tries=8, wait=1.5):
    for _ in range(tries):
        out = press(key, wait)
        if any(text.lower() in s.lower() for s in out): return out
    return None


def launch_test(update=False):
    if not update: os.environ['MK9A_NOUPDATE'] = '1'
    pos = log_size()
    mk9.launch(['--user_data_root=' + TESTSAVES])
    return to_main_menu(0)


def to_main_menu(pos=None):
    """from a fresh start: hide the Online window, wait for the title, Select into the main menu"""
    if wait_say('Type a username', pos, 90):
        time.sleep(1); mk9.focus(); press('f1', 1)
    if not wait_say('Press Start', pos, 90): return False
    time.sleep(1)
    return press_until('j', 'Main menu', 6, 2.5) is not None
