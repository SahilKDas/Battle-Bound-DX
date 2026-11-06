"""Compile Scratch's graph to a compact, dependency-free native runtime image."""
import json
from pathlib import Path
import struct
import hashlib
from workspace_budget import check_budget
from asset_paths import asset_manifest, runtime_path

ROOT = Path(__file__).resolve().parents[1]


def compile_project():
    check_budget(12_000_000)
    project = json.loads((ROOT / 'scratch/original/project.json').read_text(encoding='utf-8'))
    manifest = asset_manifest()
    opcodes = sorted({b['opcode'] for t in project['targets'] for b in t['blocks'].values() if isinstance(b, dict)})
    output = bytearray(b'BBDX\x01\x00\x00\x00')
    def integer(value):
        output.extend(struct.pack('<i', int(value)))
    def number(value):
        output.extend(struct.pack('<d', float(value)))
    def string(value):
        if isinstance(value, bool):
            value = 'true' if value else 'false'
        value = str(value).encode('utf-8')
        integer(len(value))
        output.extend(value)
    def mapping(values):
        integer(len(values))
        for key, value in values.items():
            string(key)
            string(value)
    integer(len(opcodes))
    for op in opcodes:
        string(op)
    integer(len(project['targets']))
    for target in project['targets']:
        blocks = {k: b for k, b in target['blocks'].items() if isinstance(b, dict)}
        ids = {key: index for index, key in enumerate(blocks)}
        string(target['name'])
        for key, default in [('x', 0), ('y', 0), ('size', 100), ('direction', 90), ('volume', 100)]:
            number(target.get(key, default))
        integer(target.get('currentCostume', 0))
        integer(target.get('visible', False))
        integer(target.get('layerOrder', 0))
        string(target.get('rotationStyle', 'all around'))
        integer(len(target.get('variables', {})))
        for key, value in target.get('variables', {}).items():
            string(key); string(value[0]); string(value[1])
        integer(len(target.get('lists', {})))
        for key, value in target.get('lists', {}).items():
            string(key); string(value[0]); integer(len(value[1]))
            for item in value[1]:
                string(item)
        for kind in ['costumes', 'sounds']:
            integer(len(target[kind]))
            for asset in target[kind]:
                string(asset['name'])
                string(runtime_path(asset, manifest))
                raster_scale = 2 if asset['dataFormat'] == 'svg' else 1
                number(asset.get('rotationCenterX', 0) * raster_scale)
                number(asset.get('rotationCenterY', 0) * raster_scale)
                number(asset.get('bitmapResolution', 1) * raster_scale)
        integer(len(blocks))
        for key, block in blocks.items():
            string(key)
            integer(opcodes.index(block['opcode']))
            integer(ids.get(block.get('next'), -1))
            integer(block.get('topLevel', False))
            fields = {k: v[0] for k, v in block.get('fields', {}).items()}
            fields.update({k + '_ID': v[1] for k, v in block.get('fields', {}).items() if len(v) > 1 and v[1] is not None})
            mapping(fields)
            mutation = block.get('mutation', {})
            string(mutation.get('proccode', ''))
            integer(mutation.get('warp') == 'true')
            arguments = json.loads(mutation.get('argumentids', '[]'))
            names = json.loads(mutation.get('argumentnames', '[]'))
            integer(len(arguments))
            for index, arg in enumerate(arguments):
                string(arg); string(names[index] if index < len(names) else '')
            integer(len(block.get('inputs', {})))
            for name, value in block.get('inputs', {}).items():
                string(name)
                primary = value[1]
                if isinstance(primary, str):
                    integer(1); integer(ids.get(primary, -1)); string('')
                elif isinstance(primary, list):
                    integer(2 if primary[0] == 12 else 3 if primary[0] == 13 else 0)
                    integer(-1)
                    string(primary[2] if primary[0] in (12, 13) else primary[1])
                else:
                    integer(0); integer(-1); string('')
    destination = ROOT / 'scratch/project.bbdx'
    destination.write_bytes(output)
    inventory = json.loads((ROOT / 'scratch/inventory.json').read_text(encoding='utf-8'))
    report = {'format': 1, 'sourceSha256': inventory['sha256'], 'projectJsonSha256': hashlib.sha256((ROOT / 'scratch/original/project.json').read_bytes()).hexdigest(), 'targets': len(project['targets']), 'blocks': sum(len(t['blocks']) for t in project['targets']), 'opcodes': opcodes, 'bytes': len(output)}
    (ROOT / 'scratch/compile-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'Compiled {report["blocks"]} blocks, {len(opcodes)} opcodes, {len(output):,} bytes')
    check_budget()


if __name__ == '__main__':
    compile_project()
