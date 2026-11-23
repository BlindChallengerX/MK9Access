"""Development: close the game, deploy the build, start it, inject the mod, go to the main menu.
python restart.py [--title]   (--title: stop at the title screen)"""
import os, sys, time, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mk9, inject
SRC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
mk9.kill(); time.sleep(2)
subprocess.run(['sh', os.path.join(SRC, 'deploy.sh')], capture_output=True)
log = os.path.join(mk9.GAME, 'MK9Access', 'MK9Access.log')
if os.path.exists(log): os.remove(log)
mk9.launch(wait=22)
inject.main(); time.sleep(2)
mk9.focus(); time.sleep(0.5)
mk9.key('f1'); time.sleep(1)              # hide the online window (no user name yet)
if '--title' not in sys.argv:
    for _ in range(2):
        mk9.key('enter', hold=0.15); time.sleep(3)
print('ready')
