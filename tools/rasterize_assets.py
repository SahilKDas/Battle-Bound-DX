"""Rasterize original SVGs once; the native game has no SVG/Python dependency."""
from pathlib import Path
import json
import pymupdf as fitz
from workspace_budget import check_budget

ROOT = Path(__file__).resolve().parents[1]


def main():
    check_budget(100_000_000)
    project = json.loads((ROOT / 'scratch/original/project.json').read_text(encoding='utf-8'))
    output = ROOT / 'scratch/raster'
    output.mkdir(exist_ok=True)
    manifest = output / 'quality.json'
    current_quality = {'scale': 2}
    rebuild = not manifest.exists() or json.loads(manifest.read_text()) != current_quality
    names = {a['md5ext'] for t in project['targets'] for a in t['costumes'] if a['dataFormat'] == 'svg'}
    written = 0
    reserved_bytes = 0
    for name in sorted(names):
        source = ROOT / 'scratch/original' / name
        target = output / (Path(name).stem + '.png')
        if not rebuild and target.exists() and target.stat().st_mtime >= source.stat().st_mtime:
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
        reserved_bytes += len(png)
        if reserved_bytes > 100_000_000:
            raise SystemExit('Raster cache exceeded its preflight reservation')
        target.write_bytes(png)
        written += 1
    manifest.write_text(json.dumps(current_quality))
    print(f'Raster cache: {len(names)} SVGs, {written} rebuilt, {sum(p.stat().st_size for p in output.glob("*.png")) / 1_000_000:.2f} MB')
    check_budget()


if __name__ == '__main__':
    main()
