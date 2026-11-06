"""Bounded isolated execution without shell launch dialogs or development PATH."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='bbdx-standalone-') as directory:
    isolated = Path(directory) / 'battle_bound_dx.exe'
    shutil.copyfile(Path(sys.argv[1]).resolve(), isolated)
    environment = dict(os.environ)
    environment['PATH'] = os.environ['SystemRoot'] + '/System32;' + os.environ['SystemRoot']
    result = subprocess.run([str(isolated), '--headless', '--exercise', '--ticks', '300'],
                            cwd=directory, env=environment, capture_output=True,
                            encoding='utf-8', timeout=40)
    if result.returncode:
        raise SystemExit(f'Isolated EXE failed: {result.returncode}\n{result.stdout}\n{result.stderr}')
    print('Isolated EXE passed: empty working folder, no assets or toolchain on PATH.')
