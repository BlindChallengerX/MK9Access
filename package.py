"""Builds the release zip from the build output (run sh build.sh first): ..\\..\\MK9Access-<version>.zip
(Downloads). The zip's root is unzipped into the MK9 by Lauta folder, next to mk9.exe. Paths use forward slashes."""
import os, re, sys, zipfile

SRC = os.path.dirname(os.path.abspath(__file__))
ver = re.search(r'#define MOD_VERSION "([^"]+)"', open(os.path.join(SRC, 'src', 'access.h')).read()).group(1)
out = os.path.join(os.path.dirname(os.path.dirname(SRC)), 'MK9Access-%s.zip' % ver)

files = [
    ('docs/MK9 Access README.txt', 'MK9 Access README.txt'),
    ('dist/launch.cmd', 'MK9 Access launch.cmd'),
    ('docs/MK9 BnB Combos.html', 'MK9 BnB Combos.html'),
    ('out/plugins/mk9access.dll', 'plugins/mk9access.dll'),
    ('out/MK9Access/MK9Access.dll', 'MK9Access/MK9Access.dll'),
    ('out/MK9Access/prism.dll', 'MK9Access/prism.dll'),
    ('dist/setup.ps1', 'MK9Access/setup.ps1'),
    ('dist/mk9update.ps1', 'MK9Access/mk9update.ps1'),
    ('data/fatalities.tsv', 'MK9Access/fatalities.tsv'),
    ('third_party/prism/LICENSE', 'licenses/Prism-MPL-2.0.txt'),
    ('third_party/minhook/LICENSE.txt', 'licenses/MinHook-BSD-2-Clause.txt'),
    ('data/sounds/Kenney-License.txt', 'licenses/Kenney-CC0.txt'),
]
for d, dest in (('out/MK9Access/fatalities', 'MK9Access/fatalities'), ('data/movelists', 'MK9Access/movelists'),
                ('data/sounds', 'MK9Access/sounds'), ('out/MK9Access/story', 'MK9Access/story')):
    for name in sorted(os.listdir(os.path.join(SRC, d))):
        files.append((d + '/' + name, dest + '/' + name))

missing = [s for s, _ in files if not os.path.isfile(os.path.join(SRC, s))]
if missing: sys.exit('missing: ' + ', '.join(missing))
if os.path.exists(out): os.remove(out)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for s, dest in files:
        z.write(os.path.join(SRC, s), dest.replace('\\', '/'))
print(out, len(files), 'files', os.path.getsize(out), 'bytes')
