"""Exercise GPU rendering with the same deterministic input and compare traces."""
from pathlib import Path
import subprocess
import sys
import tempfile
from compare_traces import compare

ROOT = Path(__file__).resolve().parents[1]
executable = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='bbdx-render-trace-') as directory:
    trace = Path(directory) / 'vulkan.jsonl'
    result = subprocess.run([str(executable), '--root', str(ROOT), '--exercise', '--render-smoke',
                             '--ticks', '300', '--trace', str(trace)], cwd=directory,
                            capture_output=True, encoding='utf-8', timeout=60)
    if result.returncode:
        raise SystemExit(f'Vulkan replay failed: {result.returncode}\n{result.stdout}\n{result.stderr}')
    expected = ROOT / 'tests/fixtures/gameplay.trace.jsonl'
    mismatch = compare(expected, trace)
    if mismatch:
        raise SystemExit('Vulkan/headless replay divergence: ' + mismatch)
    assert expected.read_bytes() == trace.read_bytes()
    print('Vulkan and headless traces match byte-for-byte over 300 gameplay ticks.')
