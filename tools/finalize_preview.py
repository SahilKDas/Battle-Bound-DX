"""Losslessly encode the native framebuffer without an extra image library."""
from pathlib import Path
import struct
import zlib
from workspace_budget import check_budget

root = Path(__file__).resolve().parents[1]
check_budget(5_000_000)
source = root / 'scratch/native-preview.ppm'
data = source.read_bytes()
magic, dimensions, maximum, pixels = data.split(b'\n', 3)
width, height = map(int, dimensions.split())
assert magic == b'P6' and maximum == b'255' and len(pixels) == width * height * 3


def chunk(kind, content):
    return struct.pack('>I', len(content)) + kind + content + struct.pack('>I', zlib.crc32(kind + content))


rows = b''.join(b'\x00' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
(root / 'scratch/native-preview.png').write_bytes(png)
source.unlink()
print('Saved scratch/native-preview.png')
