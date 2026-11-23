"""Repro for the Krypt-load crash (guest write to 0x820E0858): test saves, one Ladder win, then Extras > Krypt.
python krypt_repro.py [fights]   (fights=0: straight to the Krypt). Prints CRASH or what the Krypt said."""
import os, sys, time, random, re, glob
sys.path.insert(0, os.path.dirname(__file__))
import mk9, drive


def crashed():
    logs = sorted(glob.glob(os.path.join(mk9.GAME, 'logs', 'mk9_*.log')), key=os.path.getmtime)
    try: return 'Unhandled guest access violation' in open(logs[-1], encoding='utf-8', errors='replace').read()
    except (OSError, IndexError): return False


def fight_one_match(timeout=300):
    mk9.focus(); pos = drive.log_size(); t0 = time.time()
    while time.time() - t0 < timeout:
        seq = random.choice([['left', 'right', 'u'], ['down', 'left', 'u'], ['i'], ['j'], ['k'], ['u'], ['right+k'], ['down+i']])
        for s in seq: mk9.key(s, hold=0.05, gap=0.03, check=False)
        time.sleep(0.05)
        if [s for s in drive.said_since(pos) if re.match(r'^\d+ koins$', s) and s != '0 koins']: return True
    return False


def main(fights):
    if not drive.launch_test(): print('no main menu'); return
    if fights:
        drive.press('j', 2); drive.press('j', 4); drive.press('j', 3)          # Fight > Ladder > Scorpion
        drive.press('up', 1); drive.press('up', 1); drive.press('j', 3)       # difficulty: easier
        for _ in range(fights):
            print('fight', fight_one_match())
        time.sleep(4)
        drive.press('enter', 1.5)
        for _ in range(4): drive.press('down', 0.8)
        drive.press('j', 1.5); drive.press('left', 0.8)
        print(drive.press('j', 6))
    if not drive.press_until('down', 'Extras', 9, 0.9): print('no Extras'); return    # wherever the cursor is
    if not any('Krypt' in x for x in drive.press('j', 2)): print('not in Extras'); return
    pos = drive.log_size(); drive.press('j', 3)
    t0 = time.time()
    while time.time() - t0 < 40:
        if crashed(): print('CRASH'); return
        s = drive.said_since(pos)
        if any('koins' in x for x in s): print('KRYPT OK', s); return
        time.sleep(1)
    print('no Krypt speech', drive.said_since(pos))


if __name__ == '__main__':
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
