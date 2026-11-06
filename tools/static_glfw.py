"""Bind the installed raylib archive's GLFW import pointers to static GLFW."""
from pathlib import Path
import re
import subprocess
import sys

symbols = subprocess.check_output(['nm', '--undefined-only', sys.argv[1]], text=True)
functions = sorted(set(re.findall(r'__imp_(glfw\w+)', symbols)))
if not functions:
    raise SystemExit('Expected GLFW import references in this raylib archive')
source = '#define GLFW_INCLUDE_NONE\n#define GLFW_EXPOSE_NATIVE_WIN32\n#include <GLFW/glfw3.h>\n#include <GLFW/glfw3native.h>\nextern "C" {\n'
source += ''.join(f'decltype(&{name}) __imp_{name} = &{name};\n' for name in functions)
source += '}\n'
Path(sys.argv[2]).write_text(source)
print(f'Static GLFW bridge: {len(functions)} imported functions')
