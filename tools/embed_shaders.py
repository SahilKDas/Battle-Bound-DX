"""Embed compiled SPIR-V into C++; no shader files are needed at runtime."""
from pathlib import Path
import struct
import sys

output = '#pragma once\n#include <cstdint>\nnamespace bb::gfx {\n'
for name, path in [('vertexShader', sys.argv[1]), ('fragmentShader', sys.argv[2])]:
    data = Path(path).read_bytes()
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    output += f'inline constexpr std::uint32_t {name}[] = {{\n'
    for index in range(0, len(words), 8):
        output += '    ' + ', '.join(f'0x{word:08x}' for word in words[index:index + 8]) + ',\n'
    output += '};\n'
output += '}\n'
Path(sys.argv[3]).write_text(output)
