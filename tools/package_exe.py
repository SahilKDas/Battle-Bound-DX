"""Append a read-only asset package to a PE executable; no runtime extraction."""
from pathlib import Path
import json
import struct
import sys
from workspace_budget import check_budget
from asset_paths import asset_manifest, runtime_path

ROOT = Path(__file__).resolve().parents[1]
executable = Path(sys.argv[1]).resolve()
project = json.loads((ROOT / 'scratch/original/project.json').read_text(encoding='utf-8'))
manifest = asset_manifest()
names = {'assets/manifest.json', 'scratch/project.bbdx', 'LICENSE', 'CREDITS.md', 'licenses/raylib.txt', 'licenses/glfw.txt'}
for target in project['targets']:
    for asset in target['costumes'] + target['sounds']:
        names.add(runtime_path(asset, manifest))
size = 4 + sum(12 + len(name.encode('utf-8')) + (ROOT / name).stat().st_size for name in names) + 16
check_budget(size if executable.is_relative_to(ROOT) else 0)
with executable.open('r+b') as stream:
    stream.seek(0, 2)
    end = stream.tell()
    if end >= 16:
        stream.seek(-16, 2)
        previous, magic = struct.unpack('<Q8s', stream.read(16))
        if magic == b'BBDXPAK1':
            stream.truncate(previous)
    stream.seek(0, 2)
    start = stream.tell()
    stream.write(struct.pack('<I', len(names)))
    for name in sorted(names):
        encoded = name.encode('utf-8')
        data = (ROOT / name).read_bytes()
        stream.write(struct.pack('<I', len(encoded)) + encoded + struct.pack('<Q', len(data)) + data)
    stream.write(struct.pack('<Q8s', start, b'BBDXPAK1'))
print(f'Bundled {len(names)} assets, {size / 1_000_000:.2f} MB; EXE {executable.stat().st_size / 1_000_000:.2f} MB')
