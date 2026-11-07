"""Compare typed input/camera state and gameplay against the retained script path."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from compare_traces import compare

ROOT = Path(__file__).resolve().parents[1]


def main():
    executable = Path(sys.argv[1]).resolve()
    fixture = ROOT / 'tests/fixtures/gameplay.replay'
    with tempfile.TemporaryDirectory(prefix='bbdx-input-camera-') as directory:
        work = Path(directory)
        def run(*arguments):
            result = subprocess.run([str(executable), '--headless', *map(str, arguments)],
                                    cwd=work, capture_output=True, encoding='utf-8', timeout=45)
            assert result.returncode == 0, (result.stdout, result.stderr)
            return result.stdout
        check = run('--camera-check')
        assert '104 original-expression comparisons passed' in check

        varied = work / 'varied.replay'
        lines = fixture.read_text(encoding='utf-8').splitlines()
        header = lines[:6]
        header[-1] = 'frames 600'
        inputs = []
        for tick in range(600):
            keys = ['a' if tick < 120 else 'd' if tick < 300 else 'a']
            if tick % 80 < 5: keys.append('w')
            if 180 <= tick < 210: keys.append('space')
            if 320 <= tick < 370: keys.append('s')
            if 460 <= tick < 470: keys.append('r')
            if tick == 240: keys.append('2')
            if tick == 300: keys.append('1')
            wheel = 1 if tick == 400 else -1 if tick == 430 else 0
            inputs.append(f'{tick}\t"{";".join(sorted(keys))}"\t{int(60 <= tick < 500)}\t{(tick % 97)-48.25}\t{(tick % 67)-33.75}\t{wheel}')
        varied.write_text('\n'.join(header + inputs) + '\n', encoding='utf-8')

        for name, tape in [('fixture', fixture), ('varied', varied)]:
            traces, systems = {}, {}
            for backend in ['native', 'legacy']:
                traces[backend] = work / f'{name}-{backend}.jsonl'
                systems[backend] = work / f'{name}-{backend}-systems.jsonl'
                flags = ['--legacy-camera', '--legacy-movement'] if backend == 'legacy' else []
                run('--replay', tape, '--trace', traces[backend], '--systems-trace', systems[backend], *flags)
            for paths in [traces, systems]:
                assert paths['native'].read_bytes() == paths['legacy'].read_bytes(), compare(paths['legacy'], paths['native'])
            if name == 'fixture':
                golden = ROOT / 'tests/fixtures/gameplay.trace.jsonl'
                assert golden.read_bytes() == traces['native'].read_bytes(), compare(golden, traces['native'])
            rows = [json.loads(line) for line in systems['native'].read_text(encoding='utf-8').splitlines()][1:]
            frames = list(csv.reader(tape.read_text(encoding='utf-8').splitlines()[6:], delimiter='\t'))
            previous = [False] * 15
            for index, (frame, row) in enumerate(zip(frames, rows)):
                keys = set(frame[1].split(';')) if frame[1] else set()
                fire, wheel = bool(int(frame[2])), int(frame[5])
                held = ['w' in keys, 'a' in keys, 'd' in keys, 's' in keys, fire,
                        'space' in keys, 'r' in keys, 't' in keys, 'm' in keys,
                        wheel > 0, wheel < 0, '1' in keys, '2' in keys, '3' in keys, 'q' in keys]
                assert row['horizontal'] == int('d' in keys) - int('a' in keys)
                for action, active in enumerate(held):
                    # Wheel bindings are instantaneous events, not latched buttons.
                    expected = [int(active), int(active and not previous[action]), int(not active and previous[action])]
                    if action in (9, 10): expected = [int(active), int(active), 0]
                    assert row['actions'][action] == expected, (name, index, action, row['actions'][action], expected)
                previous = held
                if index:
                    assert row['aimWorld'] == [rows[index-1]['camera'][0] + float(frame[3]),
                                                rows[index-1]['camera'][1] + float(frame[4])]
        print('Input/camera/movement parity passed: unchanged golden trace, 900 native/legacy ticks, typed action edges/aim, 104 source-expression checks.')


if __name__ == '__main__':
    main()
