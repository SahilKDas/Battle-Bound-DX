"""Rasterize original SVGs once; the native game has no SVG/Python dependency."""
from pathlib import Path
import json
import pymupdf as fitz
from workspace_budget import check_budget
from asset_paths import asset_manifest

ROOT = Path(__file__).resolve().parents[1]


def main():
    check_budget(100_000_000)
    project = json.loads((ROOT / 'scratch/original/project.json').read_text(encoding='utf-8'))
    output = ROOT / 'assets/raster'
    output.mkdir(exist_ok=True)
    manifest = output / 'quality.json'
    current_quality = {'scale': 2, 'scenery_renderer': 'resvg-py 0.5.0'}
    previous_quality = json.loads(manifest.read_text()) if manifest.exists() else {}
    rebuild = previous_quality.get('scale') != current_quality['scale']
    paths = asset_manifest()
    names = {a['md5ext'] for t in project['targets'] for a in t['costumes'] if a['dataFormat'] == 'svg'}
    written = 0
    reserved_bytes = 0
    for name in sorted(names):
        source = ROOT / paths[name]['sourcePath']
        target = ROOT / paths[name]['runtimePath']
        target.parent.mkdir(parents=True, exist_ok=True)
        scenery = source.parent.parent.name == 'scenery' and 'stop-opacity=' in source.read_text(encoding='utf-8')
        refresh_scenery = scenery and previous_quality.get('scenery_renderer') != current_quality['scenery_renderer']
        if not rebuild and not refresh_scenery and target.exists() and target.stat().st_mtime >= source.stat().st_mtime:
            continue
        with fitz.open(source) as doc:
            rect = doc[0].rect
            if rect.width * rect.height * 4 > 64_000_000:
                raise SystemExit(f'Asset too large for bounded rasterization: {name}')
            if rect.width <= 0 or rect.height <= 0:
                pixmap = fitz.Pixmap(fitz.csRGB, fitz.IRect(0, 0, 1, 1), True)
                pixmap.clear_with(0)
            else:
                pixmap = doc[0].get_pixmap(matrix=fitz.Matrix(2, 2), alpha=True)
            png = pixmap.tobytes('png')
        if scenery and rect.width > 0 and rect.height > 0:
            try:
                from resvg_py import svg_to_bytes
            except ImportError as error:
                raise SystemExit('Install resvg-py==0.5.0 to rebuild SVG scenery.') from error
            # Preserve the existing pixel dimensions and Scratch costume centers.
            svg = source.read_text(encoding='utf-8')
            png = svg_to_bytes(svg_string=svg, zoom=2, skip_system_fonts=True)
            rendered = fitz.Pixmap(png)
            if (rendered.width, rendered.height) != (pixmap.width, pixmap.height):
                png = svg_to_bytes(svg_string=svg, width=pixmap.width, height=pixmap.height,
                                   skip_system_fonts=True)
        reserved_bytes += len(png)
        if reserved_bytes > 100_000_000:
            raise SystemExit('Raster cache exceeded its preflight reservation')
        target.write_bytes(png)
        written += 1
    manifest.write_text(json.dumps(current_quality))
    print(f'Raster cache: {len(names)} SVGs, {written} rebuilt, {sum(p.stat().st_size for p in output.rglob("*.png")) / 1_000_000:.2f} MB')
    check_budget()


if __name__ == '__main__':
    main()
