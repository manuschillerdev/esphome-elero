"""Run the offline reply-matching experiment with pinned upstream Java classes.

Requires Python, network access, and an installed Java JDK. No serial access.
Downloaded classes retain their upstream licence headers in a temporary folder.
"""
import hashlib
import json
import shutil
import subprocess
import tempfile
import urllib.request
from pathlib import Path

here = Path(__file__).resolve().parent
manifest = json.loads((here / 'manifest.json').read_text())
base = (
    'https://raw.githubusercontent.com/vbier/openhab-addons/'
    + manifest['revision']
    + '/bundles/org.openhab.binding.elerotransmitterstick/src/main/java/'
    + 'org/openhab/binding/elerotransmitterstick/internal/stick/'
)
with tempfile.TemporaryDirectory(prefix='elero-bier-replay-') as folder:
    root = Path(folder)
    source = root / 'src'
    package = source / 'org/openhab/binding/elerotransmitterstick/internal/stick'
    package.mkdir(parents=True)
    for name, digest in manifest['classes'].items():
        data = urllib.request.urlopen(base + name, timeout=30).read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError('Upstream source hash differs: ' + name)
        (package / name).write_bytes(data)
    stub = source / 'org/openhab/core/util/HexUtils.java'
    stub.parent.mkdir(parents=True)
    shutil.copyfile(here / 'HexUtils.java', stub)
    shutil.copyfile(here / 'Replay.java', source / 'Replay.java')
    classes = root / 'classes'
    classes.mkdir()
    subprocess.run(['javac', '-d', str(classes), *map(str, source.rglob('*.java'))], check=True)
    subprocess.run(['java', '-cp', str(classes), 'Replay', str(here / 'fixtures.tsv')], check=True)
