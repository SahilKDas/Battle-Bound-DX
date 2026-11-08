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
    systems = Path(directory) / 'vulkan-systems.jsonl'
    for cap in (30, 60, 144):
        result = subprocess.run([str(executable), '--root', str(ROOT), '--exercise', '--render-smoke',
                                 '--ticks', '300', '--fps', str(cap), '--trace', str(trace), '--systems-trace', str(systems)], cwd=directory,
                                capture_output=True, encoding='utf-8', timeout=60)
        if result.returncode:
            raise SystemExit(f'Vulkan replay failed: {result.returncode}\n{result.stdout}\n{result.stderr}')
        expected = ROOT / 'tests/fixtures/gameplay.trace.jsonl'
        mismatch = compare(expected, trace)
        if mismatch:
            raise SystemExit('Vulkan/headless replay divergence: ' + mismatch)
        assert expected.read_bytes() == trace.read_bytes()
        assert f'Rendered frames: {300 * cap // 30}' in result.stdout, result.stdout
        print(f'{cap} FPS render schedule matched the golden trace.')
    headless = Path(directory) / 'headless-systems.jsonl'
    result = subprocess.run([str(executable), '--replay', str(ROOT / 'tests/fixtures/gameplay.replay'),
                             '--systems-trace', str(headless)], cwd=directory,
                            capture_output=True, encoding='utf-8', timeout=40)
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert systems.read_bytes() == headless.read_bytes(), compare(headless, systems)
    print('Vulkan and headless traces match byte-for-byte over 300 gameplay ticks.')
