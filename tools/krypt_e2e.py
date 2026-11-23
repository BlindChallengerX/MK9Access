"""End-to-end Krypt check on the TEST saves (work\\testsaves, never the user's): a Ladder win (koin speech after the
fight), Krypt entry, walking with footsteps (recorded via audiorec), a bump, a purchase and its prize, the balance.
Prints every step's speech; the recording lands in game\\MK9Access\\audiorec.wav."""
import os, sys, time, struct
sys.path.insert(0, os.path.dirname(__file__))
import mk9, drive, cmd, krypt_repro


def step(label, out):
    print(f'{label:12} {out}')
    return out


def main(fight=True):
    if not drive.launch_test(): print('no main menu'); return
    if fight:
        drive.press('j', 2); drive.press('j', 4); drive.press('j', 3)
        drive.press('up', 1); drive.press('up', 1); drive.press('j', 3)
        pos = drive.log_size()
        print('fight won', krypt_repro.fight_one_match())
        time.sleep(6)
        step('after fight', [s for s in drive.said_since(pos) if 'koin' in s or 'wins' in s])
        drive.press('enter', 1.5)
        for _ in range(4): drive.press('down', 0.8)
        drive.press('j', 1.5); drive.press('left', 0.8); drive.press('j', 6)
    if not drive.press_until('down', 'Extras', 9, 0.9): print('no Extras'); return
    drive.press('j', 2)
    pos = drive.log_size(); drive.press('j', 3); time.sleep(10)
    if krypt_repro.crashed(): print('CRASH on Krypt load'); return
    step('entry', drive.said_since(pos))
    cmd.cmd('audiorec 30', wait=0.2)
    moved = 0
    for k in ['up', 'up', 'left', 'right', 'down', 'down', 'down', 'down', 'down', 'down']:
        a = mk9.g(0x40e2cd64, 8); out = drive.press(k, 2.4, hold=0.25); b = mk9.g(0x40e2cd64, 8)
        step(k, (out, 'moved' if a != b else 'still'))
    # buy the koffin we stand on, or walk until one is locked on
    for k in ['up', 'up', 'up', 'left']:
        out = drive.press(k, 3, hold=0.25)
        if any('koins' in s for s in out): break
    step('buy', drive.press('j', 8))
    step('close', drive.press('j', 4))


if __name__ == '__main__':
    main('--nofight' not in sys.argv)
