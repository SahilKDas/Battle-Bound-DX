"""Audit descriptive paths, shared aliases, and original source bytes."""
import hashlib
import json
from pathlib import Path
from asset_paths import ROOT, asset_manifest


def main():
    files = asset_manifest()
    project = json.loads((ROOT / 'scratch/original/project.json').read_text(encoding='utf-8'))
    aliases = 0
    seen = set()
    for original, entry in files.items():
        source = (ROOT / entry['sourcePath']).resolve()
        runtime = (ROOT / entry['runtimePath']).resolve()
        assert source.is_relative_to(ROOT) and runtime.is_relative_to(ROOT)
        assert source.is_file() and runtime.is_file(), original
        assert source.name != original, 'Asset is still hash-named'
        assert entry['sourcePath'].casefold() not in seen
        seen.add(entry['sourcePath'].casefold())
        data = source.read_bytes()
        assert hashlib.sha256(data).hexdigest() == entry['sha256'], source
        assert hashlib.md5(data).hexdigest() == Path(original).stem, 'Original Scratch identity differs'
    for target in project['targets']:
        for kind in ['costumes', 'sounds']:
            for asset in target[kind]:
                assert {'target': target['name'], 'kind': kind, 'name': asset['name']} in files[asset['md5ext']]['aliases']
                aliases += 1
    print(f'Asset audit passed: {len(files)} descriptive source paths, {aliases} original aliases; every source checksum matches.')


if __name__ == '__main__':
    main()
