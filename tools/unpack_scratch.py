"""Extract the original SB3 and generate searchable, ID-linked script listings."""
import collections
import hashlib
import html
import json
from pathlib import Path
import re
import zipfile
import xml.etree.ElementTree as ET
from workspace_budget import check_budget
from asset_paths import asset_manifest, source_path
from name_assets import TARGETS

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Battle Bound.sb3'
OUT = ROOT / 'scratch'


def main():
    check_budget(60_000_000)
    OUT.mkdir(exist_ok=True)
    project = json.loads((OUT / 'original/project.json').read_text(encoding='utf-8'))
    paths = asset_manifest()
    scripts = OUT / 'scripts'
    scripts.mkdir(exist_ok=True)
    inventory = []
    events = collections.defaultdict(lambda: {'senders': [], 'receivers': []})
    for index, target in enumerate(project['targets']):
        blocks = target['blocks']
        argument_names = {}
        for block in blocks.values():
            if isinstance(block, dict) and block['opcode'] == 'procedures_prototype':
                mutation = block.get('mutation', {})
                argument_names.update(zip(json.loads(mutation.get('argumentids', '[]')), json.loads(mutation.get('argumentnames', '[]'))))
        def expression(value, seen=None):
            seen = set() if seen is None else seen
            if isinstance(value, str) and value in blocks:
                if value in seen:
                    return '<cycle>'
                block = blocks[value]
                if not isinstance(block, dict):
                    return repr(block)
                seen = seen | {value}
                fields = ' '.join(f'{k}={v[0]!r}' for k, v in block.get('fields', {}).items())
                inputs = ' '.join(f'{argument_names.get(k, k)}=({expression(v[1], seen)})' for k, v in block.get('inputs', {}).items() if not k.startswith('SUBSTACK'))
                mutation = block.get('mutation', {}).get('proccode', '')
                return ' '.join(x for x in [block['opcode'], mutation, fields, inputs] if x)
            if isinstance(value, list):
                if value and value[0] in (12, 13):
                    return ('variable:' if value[0] == 12 else 'list:') + repr(value[1])
                return repr(value[1]) if len(value) > 1 else repr(value)
            return repr(value)
        def stack(block_id, indent=0, seen=None):
            seen = set() if seen is None else seen.copy()
            lines = []
            while block_id and block_id in blocks and block_id not in seen:
                seen.add(block_id)
                block = blocks[block_id]
                lines.append('  ' * indent + expression(block_id) + f'  # {block_id}')
                for key, value in block.get('inputs', {}).items():
                    if key.startswith('SUBSTACK'):
                        lines.append('  ' * (indent + 1) + key + ':')
                        lines.extend(stack(value[1], indent + 2, seen))
                block_id = block.get('next')
            return lines
        name = TARGETS[index]
        lines = [f"SPRITE: {target['name']}", 'Notation: raw Scratch opcodes; nested reporter expressions; original block IDs.', '']
        tops = [(key, b) for key, b in blocks.items() if isinstance(b, dict) and b.get('topLevel')]
        for key, block in sorted(tops, key=lambda pair: (pair[1].get('y', 0), pair[1].get('x', 0))):
            lines.extend(stack(key))
            lines.append('')
        (scripts / f'{name}.txt').write_text('\n'.join(lines), encoding='utf-8')
        for key, block in blocks.items():
            if not isinstance(block, dict):
                continue
            op = block['opcode']
            if op == 'event_whenbroadcastreceived':
                event = block['fields']['BROADCAST_OPTION'][0]
                events[event]['receivers'].append({'sprite': target['name'], 'block': key})
            elif op in ('event_broadcast', 'event_broadcastandwait'):
                value = block['inputs']['BROADCAST_INPUT'][1]
                event = value[1] if isinstance(value, list) and value[0] == 11 else expression(value)
                events[event]['senders'].append({'sprite': target['name'], 'block': key, 'wait': op.endswith('andwait')})
        inventory.append({
            'name': target['name'], 'scriptFile': f'scripts/{name}.txt',
            'blocks': len(blocks), 'topLevelStacks': len(tops),
            'variables': target.get('variables', {}), 'lists': target.get('lists', {}),
            'costumes': target.get('costumes', []), 'sounds': target.get('sounds', []),
            'comments': target.get('comments', {}),
            'customBlocks': [b.get('mutation', {}) for b in blocks.values() if isinstance(b, dict) and b['opcode'] == 'procedures_prototype'],
        })
    report = {'source': SOURCE.name, 'sha256': json.loads((OUT / 'inventory.json').read_text(encoding='utf-8'))['sha256'], 'extensions': project.get('extensions', []), 'targets': inventory, 'events': dict(events)}
    (OUT / 'inventory.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    catalog = ['<!doctype html><meta charset="utf-8"><title>Battle Bound assets</title>',
               '<style>body{font:16px system-ui;background:#20242c;color:#eee;margin:32px}section{display:flex;flex-wrap:wrap;gap:12px}figure{margin:0;padding:10px;width:180px;background:#343b47;overflow-wrap:anywhere}img{width:180px;height:120px;object-fit:contain}audio{width:180px}small{font-size:10px}</style>',
               '<h1>Battle Bound original assets</h1><p>Costume names, sprite ownership, and original files. Shared assets appear under each owner.</p>']
    for target in project['targets']:
        catalog.append('<h2>' + html.escape(target['name']) + '</h2><section>')
        for asset in target['costumes'] + target['sounds']:
            filename = asset.get('md5ext', asset['assetId'] + '.' + asset['dataFormat'])
            url = '../' + paths[filename]['sourcePath']
            media = f'<img loading="lazy" src="{url}" alt="">' if asset in target['costumes'] else f'<audio controls preload="none" src="{url}"></audio>'
            catalog.append(f'<figure>{media}<figcaption>{html.escape(asset["name"])}<br><a href="{url}"><small>{filename}</small></a></figcaption></figure>')
        catalog.append('</section>')
    (OUT / 'assets.html').write_text('\n'.join(catalog), encoding='utf-8')
    ui = next(t for t in project['targets'] if t['name'] == '[UI]')
    labels = {}
    for costume in ui['costumes']:
        if costume['name'].endswith(('_name', '_info')) and costume['dataFormat'] == 'svg':
            tree = ET.parse(ROOT / source_path(costume, paths))
            labels[costume['name']] = ' '.join(node.text for node in tree.iter() if node.tag.endswith(('}text', '}tspan')) and node.text).strip()
    stage_lists = {v[0]: v[1] for v in project['targets'][0]['lists'].values()}
    items = []
    for index, code in enumerate(stage_lists['ITEMS']):
        category = next((name for name in ['.Primary weapons', '.Secondary weapons', '.Gadgets'] if code in stage_lists[name]), None)
        items.append({'code': code, 'name': labels.get(code + '_name'), 'description': labels.get(code + '_info'), 'category': category, 'gearPoints': stage_lists['Gear points'][index] if index < len(stage_lists['Gear points']) else None})
    (OUT / 'items.json').write_text(json.dumps(items, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f"Extracted {len(project['targets'])} targets; {sum(t['blocks'] for t in inventory)} blocks; {len(events)} event entries.")
    check_budget()


if __name__ == '__main__':
    main()
