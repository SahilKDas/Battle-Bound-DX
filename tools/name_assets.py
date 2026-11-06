"""One-time, byte-preserving migration from hash filenames to source asset names."""
from pathlib import Path
import hashlib
import json
import re
import unicodedata
from workspace_budget import ROOT, check_budget

TARGETS = ['stage', 'notes', 'player', 'weapons', 'remote-players', 'network',
           'collision-map', 'scenery', 'effects', 'menus', 'hud', 'save-data',
           'runtime-checks', 'introduction', 'title-graphics']
ITEMS = ['hands', 'pistol', 'axe', 'smg', 'bow', 'grenade', 'shotgun', 'sniper',
         'pet-cube', 'rocket-launcher', 'shurikens', 'knife', 'shield', 'machine-gun',
         'assault-rifle', 'heavy-pistol', 'katana', 'semi-auto-rifle', 'light-smg',
         'taser', 'medic-gun', 'medkit', 'nunchaku', 'smoke-bomb', 'flamethrower']


def slug(name):
    if name and not any(character.isalnum() for character in name):
        symbols = {'`': 'backtick', ':': 'colon', '%': 'percent', '/': 'slash',
                   '_': 'underscore', '-': 'minus', '.': 'dot', ' ': 'space'}
        return '-'.join(symbols.get(character, f'u{ord(character):04x}') for character in name)
    name = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode().lower()
    return re.sub(r'[^a-z0-9]+', '-', name).strip('-') or 'unnamed'


def move_checked(source, destination):
    source, destination = source.resolve(), destination.resolve()
    assert source.is_relative_to(ROOT) and destination.is_relative_to(ROOT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source == destination:
        return
    if destination.exists():
        assert source.read_bytes() == destination.read_bytes()
        source.unlink()
    else:
        source.replace(destination)


def main():
    check_budget(2_000_000)
    project = json.loads((ROOT / 'scratch/original/project.json').read_text(encoding='utf-8'))
    destination = ROOT / 'assets/manifest.json'
    previous = json.loads(destination.read_text(encoding='utf-8'))['files'] if destination.exists() else {}
    files, used = {}, set()
    for target_index, target in enumerate(project['targets']):
        for kind in ['costumes', 'sounds']:
            for asset in target[kind]:
                original = asset['md5ext']
                alias = {'target': target['name'], 'kind': kind, 'name': asset['name']}
                if original in files:
                    files[original]['aliases'].append(alias)
                    continue
                name = asset['name']
                match = re.fullmatch(r'([a-y])_(item|name|info)', name)
                if target['name'] == '[UI]' and match:
                    name = ITEMS[ord(match[1]) - ord('a')] + '-' + {'item': 'icon', 'name': 'label', 'info': 'description'}[match[2]]
                base = f"{TARGETS[target_index]}/{'sounds' if kind == 'sounds' else 'costumes'}/{slug(name)}"
                relative = f"assets/source/{base}.{asset['dataFormat']}"
                suffix = 2
                while relative.casefold() in used:
                    relative = f"assets/source/{base}-{suffix}.{asset['dataFormat']}"
                    suffix += 1
                used.add(relative.casefold())
                source = ROOT / previous.get(original, {}).get('sourcePath', 'scratch/original/' + original)
                data = source.read_bytes()
                checksum = hashlib.sha256(data).hexdigest()
                if original in previous:
                    assert checksum == previous[original]['sha256']
                move_checked(source, ROOT / relative)
                runtime = relative if asset['dataFormat'] != 'svg' else f'assets/raster/{base}.png'
                if suffix > 2 and asset['dataFormat'] == 'svg':
                    runtime = str(Path(relative.replace('assets/source/', 'assets/raster/')).with_suffix('.png')).replace('\\', '/')
                old_raster = ROOT / previous.get(original, {}).get('runtimePath', 'scratch/raster/' + Path(original).stem + '.png')
                if asset['dataFormat'] == 'svg' and old_raster.exists():
                    move_checked(old_raster, ROOT / runtime)
                files[original] = {'sourcePath': relative, 'runtimePath': runtime,
                                   'sha256': checksum, 'aliases': [alias]}
    quality = ROOT / 'scratch/raster/quality.json'
    if quality.exists():
        move_checked(quality, ROOT / 'assets/raster/quality.json')
    destination.parent.mkdir(exist_ok=True)
    destination.write_text(json.dumps({'format': 1, 'files': files}, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'Named {len(files)} unique assets; original bytes verified, shared aliases retained.')
    check_budget()


if __name__ == '__main__':
    main()
