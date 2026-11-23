"""Krypt-load crash rate with the mod on vs. unloaded, on the TEST saves.
python krypt_crashrate.py N   -> 2N fresh starts alternating "off" / "on": the mod drives the menus to Extras > Krypt;
for "off" MK9Access.dll is renamed away first (with devreload.txt the plugin then unhooks and unloads the mod), so
the Krypt loads with no mod code in the game. Prints ok / CRASH per run (crash = the port's "Unhandled guest access
violation" in its log within 30 s of Select)."""
import os, sys, time, glob, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import mk9, drive

DLL = os.path.join(mk9.GAME, 'MK9Access', 'MK9Access.dll')
AWAY = DLL + '.off'


def newest_log():
    return max(glob.glob(os.path.join(mk9.GAME, 'logs', 'mk9_*.log')), key=os.path.getmtime)


def text(p):
    try: return open(p, encoding='utf-8', errors='replace').read()
    except OSError: return ''


def one(mod_on):
    if os.path.exists(AWAY) and not os.path.exists(DLL): os.rename(AWAY, DLL)
    if not drive.launch_test(): return 'no main menu'
    log = newest_log()
    if not drive.press_until('down', 'Extras', 9, 0.9): return 'no Extras'
    if not any('Krypt' in s for s in drive.press('j', 2)): return 'not in Extras'
    if not mod_on:
        os.rename(DLL, AWAY); time.sleep(4)     # the plugin unhooks and unloads the mod within ~1.5 s
    mk9.key('j', hold=0.12)
    t1 = time.time()
    while time.time() - t1 < 30 and 'Unhandled guest access violation' not in text(log): time.sleep(1)
    res = 'CRASH' if 'Unhandled guest access violation' in text(log) else 'ok'
    subprocess.run(['taskkill', '/IM', 'mk9.exe', '/F'], capture_output=True); time.sleep(3)
    if os.path.exists(AWAY): os.rename(AWAY, DLL)
    return res


if __name__ == '__main__':
    n = int(sys.argv[1])
    for i in range(n):
        for on in (False, True):
            print(('on ' if on else 'off'), i + 1, one(on), flush=True)
