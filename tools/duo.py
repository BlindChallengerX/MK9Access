"""Two copies of the game on this PC for online room tests: the host = the user's game folder on the TEST saves, the
guest = work\\fresh (a separate install made by install.ps1, its own saves and online name).
    import duo; h, g = duo.start(); h.key('f1'); g.said()
Each Client focuses its own window before a key and reads its own MK9Access.log."""
import os, sys, time, subprocess, ctypes, ctypes.wintypes as w
sys.path.insert(0, os.path.dirname(__file__))
import mk9

u = ctypes.windll.user32
ROOT = os.path.dirname(mk9.GAME)
HOST_DIR = mk9.GAME
GUEST_DIR = os.path.join(ROOT, 'work', 'fresh')
TESTSAVES = os.path.join(ROOT, 'work', 'testsaves')


class Client:
    def __init__(self, gamedir, pid, name):
        self.dir, self.pid, self.name = gamedir, pid, name
        self.log = os.path.join(gamedir, 'MK9Access', 'MK9Access.log')
        self.mark()

    def mark(self):
        try: self.pos = os.path.getsize(self.log)
        except OSError: self.pos = 0

    def said(self, since=None):
        try:
            with open(self.log, 'rb') as f:
                f.seek(self.pos if since is None else since); data = f.read().decode('utf-8', 'replace')
        except OSError: return []
        return [l.split('SAY', 1)[1].lstrip('+: ').strip() for l in data.splitlines() if ' SAY' in l]

    def lines(self, word):
        with open(self.log, 'rb') as f:
            f.seek(self.pos); data = f.read().decode('utf-8', 'replace')
        return [l for l in data.splitlines() if word in l]

    def window(self):
        mk9.pid = self.pid
        return mk9.window()

    def focus(self):
        mk9.pid = self.pid
        return mk9.focus()

    def front(self):
        return u.GetForegroundWindow() == self.window()

    def key(self, k, wait=1.2, focus=True):
        if focus: self.focus()
        self.mark()
        mk9.key(k, check=False)
        time.sleep(wait)
        out = self.said()
        print('[%s] %s -> %s' % (self.name, k, out))
        return out

    def type(self, text, wait=0.8):
        self.focus(); self.mark()
        for ch in text:
            if ch == ' ': mk9.key('space', hold=0.03, gap=0.03, check=False)
            else:
                shift = ch.isupper()
                if shift: mk9._send(0x10, False)
                mk9._send(ord(ch.upper()), False); time.sleep(0.03); mk9._send(ord(ch.upper()), True)
                if shift: mk9._send(0x10, True)
                time.sleep(0.03)
        time.sleep(wait)
        return self.said()

    def wait_say(self, text, timeout=30):
        t = time.time()
        while time.time() - t < timeout:
            for s in self.said():
                if text.lower() in s.lower(): return s
            time.sleep(0.25)
        return None


def pids():
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-Process mk9 -ErrorAction SilentlyContinue | ForEach-Object { \"$($_.Id)|$($_.Path)\" }"],
                         capture_output=True, text=True).stdout
    res = {}
    for line in out.splitlines():
        if '|' in line:
            p, path = line.split('|', 1); res[os.path.normcase(os.path.dirname(path))] = int(p)
    return res


def launch(gamedir, saves):
    env = dict(os.environ, MK9A_NOUPDATE='1')
    subprocess.Popen([os.path.join(gamedir, 'mk9.exe'), '--user_data_root=' + saves], cwd=gamedir, env=env)


def attach():
    ps = pids()
    h = ps.get(os.path.normcase(HOST_DIR)); g = ps.get(os.path.normcase(GUEST_DIR))
    return (Client(HOST_DIR, h, 'host') if h else None), (Client(GUEST_DIR, g, 'guest') if g else None)


def start():
    mk9.lock()
    ps = pids()
    if os.path.normcase(HOST_DIR) not in ps: launch(HOST_DIR, TESTSAVES)
    if os.path.normcase(GUEST_DIR) not in ps: launch(GUEST_DIR, os.path.join(GUEST_DIR, 'saves'))
    for _ in range(60):
        h, g = attach()
        if h and g and h.window() and g.window(): return h, g
        time.sleep(1)
    raise RuntimeError('both games did not start')


def post_key(client, vk, hold=0.06):
    """a key to a window that is NOT in front (WM_KEYDOWN/UP posted to it): the other player acting while this PC's
    window keeps the focus"""
    hwnd = client.window()
    sc = u.MapVirtualKeyW(vk, 0)
    ext = 1 if vk in (0x25, 0x26, 0x27, 0x28) else 0
    down = 1 | (sc << 16) | (ext << 24)
    up = down | (1 << 30) | (1 << 31)
    u.PostMessageW(hwnd, 0x0100, vk, down); time.sleep(hold)
    u.PostMessageW(hwnd, 0x0101, vk, up); time.sleep(0.12)
