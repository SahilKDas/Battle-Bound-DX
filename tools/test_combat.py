"""Replay the SMG through depletion, reload and slot switches against Scratch."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from compare_traces import compare

ROOT = Path(__file__).resolve().parents[1]


def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='bbdx-combat-') as temporary:
        folder = Path(temporary)
        header = (ROOT / 'tests/fixtures/gameplay.replay').read_text(encoding='utf-8').splitlines()[:6]
        header[-1] = 'frames 330'
        tape = folder / 'combat.replay'
        rows = []
        for tick in range(330):
            keys = 'r' if tick == 225 else '2' if tick == 295 else '1' if tick == 310 else ''
            fire = 35 <= tick < 145 or 205 <= tick < 225 or 312 <= tick
            rows.append(f'{tick}\t"{keys}"\t{int(fire)}\t120\t-20\t0')
        tape.write_text('\n'.join(header + rows) + '\n', encoding='utf-8')
        outputs = [folder / 'native.jsonl', folder / 'scratch.jsonl']
        for flags, trace in zip([[], ['--legacy-combat']], outputs):
            result = subprocess.run([str(exe), '--replay', str(tape), '--trace', str(trace), *flags],
                                    cwd=folder, capture_output=True, encoding='utf-8', timeout=55)
            assert result.returncode == 0, (result.stdout, result.stderr)
        assert outputs[0].read_bytes() == outputs[1].read_bytes(), compare(outputs[1], outputs[0])
        ticks = [json.loads(line) for line in outputs[0].read_text(encoding='utf-8').splitlines()][1:]
        smg = [row for row in ticks if row['weapon'] == 'd']
        assert any(row['ammo'] == '0' for row in smg)
        assert any(row['ammo'] == '24' for row in smg if 145 <= row['tick'] < 205)
        assert ticks[225]['ammo'] == '0' and ticks[290]['ammo'] == '24'
        assert ticks[295]['weapon'] != 'd' and ticks[310]['weapon'] == 'd'
        shots = [row['tick'] for row in smg if row['tick'] and
                 row['bulletClones'] > ticks[row['tick'] - 1]['bulletClones']]
        assert len(shots) >= 24 and all(b - a >= 3 for a, b in zip(shots, shots[1:]))
        print('Combat replay matched across 330 ticks with SMG cadence, depletion, reload and swapping.')


if __name__ == '__main__':
    main()
