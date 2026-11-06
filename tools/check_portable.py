"""Reject development DLL dependencies in the final Windows PE import table."""
from pathlib import Path
import re
import subprocess
import sys

executable = Path(sys.argv[1])
output = subprocess.check_output(['objdump', '-p', str(executable)], text=True)
imports = re.findall(r'DLL Name:\s*(\S+)', output)
allowed = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'opengl32.dll', 'winmm.dll',
           'shell32.dll', 'advapi32.dll', 'ole32.dll', 'oleaut32.dll', 'comdlg32.dll',
           'comctl32.dll', 'msvcrt.dll', 'ucrtbase.dll', 'ws2_32.dll', 'imm32.dll',
           'version.dll', 'setupapi.dll', 'dwmapi.dll', 'ntdll.dll'}
unexpected = [name for name in imports if name.lower() not in allowed and not name.lower().startswith(('api-ms-win-', 'ext-ms-win-'))]
if not imports or unexpected:
    raise SystemExit(f'Unverified PE imports: {unexpected or "missing import table"}')
print('Windows system DLL imports only: ' + ', '.join(imports))
