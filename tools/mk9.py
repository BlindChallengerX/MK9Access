"""Drive MK9 by Lauta for testing: launch, keys, screenshots, guest memory reads.
Usage from other scripts: import mk9; mk9.launch(); mk9.key('enter'); mk9.shot('a.png')"""
import ctypes, ctypes.wintypes as w, subprocess, time, os, struct
GAME = r"C:\Users\michm\Downloads\MK9 Access\game"
SHOTS = os.environ.get('MK9_SHOTS') or os.path.join(os.environ.get('TEMP', '.'), 'mk9shots')
os.makedirs(SHOTS, exist_ok=True)
u = ctypes.windll.user32; k = ctypes.windll.kernel32
u.SetProcessDPIAware()
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid = None; hproc = None; GBASE = 0x100000000

# Shared keyboard/focus lock with other sessions driving games on this PC (agreed with The Long Count session):
# while someone else's lock is under 2 minutes old, wait; otherwise take it ("MK9Access <time>"), drop it at exit.
import atexit
LOCK = r"C:\Users\michm\tools\gamemod-kit\input.lock"
LOCK_NAME = "MK9Access"
REQUEST = r"C:\Users\michm\tools\gamemod-kit\input.request"   # "<name> wants focus": yield until it is deleted
def lock(wait_max=300):
    t0 = time.time()
    while True:
        try:
            with open(LOCK, encoding='utf-8') as f: who = f.read().strip()
            age = time.time() - os.path.getmtime(LOCK)
        except OSError:
            who, age = '', 1e9
        try: wants = open(REQUEST, encoding='utf-8').read().strip()   # another session asked for the screen
        except OSError: wants = ''
        if wants and not wants.startswith(LOCK_NAME) and time.time() - t0 < wait_max:
            unlock(); time.sleep(2); continue
        if not who or who.startswith(LOCK_NAME) or age > 120: break
        if time.time() - t0 > wait_max: raise RuntimeError('input lock held by ' + who)
        time.sleep(1)
    with open(LOCK, 'w', encoding='utf-8') as f: f.write(LOCK_NAME + ' ' + time.strftime('%Y-%m-%d %H:%M:%S'))
def unlock():
    try:
        with open(LOCK, encoding='utf-8') as f: mine = f.read().startswith(LOCK_NAME)
        if mine: os.remove(LOCK)
    except OSError: pass
atexit.register(unlock)

def find_pid():
    out = subprocess.run(['tasklist', '/fi', 'imagename eq mk9.exe', '/fo', 'csv', '/nh'], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split('","')
        if len(p) > 1 and p[0].strip('"').lower() == 'mk9.exe': return int(p[1])
    return None

def attach():
    global pid, hproc
    pid = find_pid()
    hproc = k.OpenProcess(0x1F0FFF, False, pid) if pid else None
    return pid

def launch(args=(), wait=0):
    # Saves go to the game folder, not Documents (Controlled Folder Access crashes the port there).
    # The command line wins; the port reads mk9.toml's user_data_root too late. The real launcher does the same.
    args = list(args)
    if not any(str(a).startswith('--user_data_root') for a in args):
        args = ['--user_data_root=' + os.path.join(GAME, 'saves')] + args
    subprocess.Popen([os.path.join(GAME, 'mk9.exe')] + args, cwd=GAME)
    time.sleep(3); attach()
    if wait: time.sleep(wait)
    return pid

def kill():
    subprocess.run(['taskkill', '/im', 'mk9.exe', '/f'], capture_output=True)

def window():
    res = []
    def cb(h, l):
        p = w.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            b = ctypes.create_unicode_buffer(256); u.GetWindowTextW(h, b, 256)
            if b.value.startswith('mk9-lauta'): res.append(h)
        return True
    u.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)(cb), 0)
    return res[0] if res else None

def focus():
    lock()
    h = window()
    if not h: raise RuntimeError('no game window')
    # Alt tap lets SetForegroundWindow work from a background process
    u.keybd_event(0x12, 0, 0, 0); u.keybd_event(0x12, 0, 2, 0)
    u.ShowWindow(h, 9); u.SetForegroundWindow(h); time.sleep(0.2)
    if u.GetForegroundWindow() != h: raise RuntimeError('game not in front')
    return h

VK = {'enter': 0x0D, 'space': 0x20, 'back': 0x08, 'esc': 0x1B, 'tab': 0x09, 'up': 0x26, 'down': 0x28, 'left': 0x25,
      'right': 0x27, 'shift': 0x10, 'f1': 0x70, 'f2': 0x71, 'f3': 0x72, 'f4': 0x73, 'f5': 0x74, 'f6': 0x75, 'f7': 0x76,
      'f8': 0x77, 'f9': 0x78, 'f10': 0x79, 'f11': 0x7A, 'f12': 0x7B}
EXT = {0x26, 0x28, 0x25, 0x27}

class KI(ctypes.Structure):
    _fields_ = [('wVk', w.WORD), ('wScan', w.WORD), ('dwFlags', w.DWORD), ('time', w.DWORD), ('dwExtraInfo', ctypes.c_void_p)]
class INP(ctypes.Structure):
    class _U(ctypes.Union): _fields_ = [('ki', KI), ('pad', ctypes.c_byte * 32)]
    _anonymous_ = ('u',); _fields_ = [('type', w.DWORD), ('u', _U)]

def _send(vk, up):
    sc = u.MapVirtualKeyW(vk, 0)
    fl = 0x8 | (0x2 if up else 0) | (0x1 if vk in EXT else 0)
    i = INP(type=1); i.ki = KI(0, sc, fl, 0, None)
    u.SendInput(1, ctypes.byref(i), ctypes.sizeof(i))

def vk_of(name):
    name = name.lower()
    if name in VK: return VK[name]
    if len(name) == 1: return ord(name.upper())
    raise KeyError(name)

def key(names, hold=0.08, gap=0.15, check=True):
    """key('down') or key('shift+down'); presses together, releases in reverse."""
    if check: focus()
    vks = [vk_of(n) for n in names.split('+')]
    for v in vks: _send(v, False)
    time.sleep(hold)
    for v in reversed(vks): _send(v, True)
    time.sleep(gap)

def shot(name):
    from PIL import ImageGrab
    h = window(); r = w.RECT(); u.GetWindowRect(h, ctypes.byref(r))
    p = name if os.path.isabs(name) else os.path.join(SHOTS, name)
    ImageGrab.grab((r.left, r.top, r.right, r.bottom), all_screens=True).resize(((r.right - r.left) // 2, (r.bottom - r.top) // 2)).save(p)
    return p

def read(addr, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    if not k.ReadProcessMemory(hproc, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)): return None
    return buf.raw[:got.value]

def g(addr, n): return read(GBASE + addr, n)
def g32(a):
    b = g(a, 4); return struct.unpack('>I', b)[0] if b else None
def gf(a):
    b = g(a, 4); return struct.unpack('>f', b)[0] if b else None

def log_tail(n=20):
    logs = sorted(f for f in os.listdir(os.path.join(GAME, 'logs')) if f.startswith('mk9_') and f.endswith('.log'))
    with open(os.path.join(GAME, 'logs', logs[-1]), encoding='utf-8', errors='replace') as f: return f.readlines()[-n:]

def click(x, y):
    """click at screenshot coordinates (half scale)"""
    focus()
    h = window(); r = w.RECT(); u.GetWindowRect(h, ctypes.byref(r))
    u.SetCursorPos(r.left + x * 2, r.top + y * 2); time.sleep(0.1)
    u.mouse_event(2, 0, 0, 0, 0); time.sleep(0.05); u.mouse_event(4, 0, 0, 0, 0); time.sleep(0.3)
