"""Integration checks run in a bounded temporary directory, never the repo."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from compare_traces import compare

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('--update-baseline', action='store_true')
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixture = ROOT / 'tests/fixtures/gameplay.replay'
    baseline = ROOT / 'tests/fixtures/gameplay.sha256.json'
    golden = ROOT / 'tests/fixtures/gameplay.trace.jsonl'
    with tempfile.TemporaryDirectory(prefix='bbdx-replay-') as directory:
        temporary = Path(directory)
        def run(*flags, success=True, env=None):
            result = subprocess.run([str(executable), '--headless', *map(str, flags)],
                                    cwd=temporary, capture_output=True, text=True,
                                    timeout=40, env=env, encoding='utf-8')
            assert (result.returncode == 0) == success, (result.returncode, result.stdout, result.stderr)
            return result
        tape, first, second = [temporary / name for name in ['gameplay.replay', 'first.jsonl', 'second.jsonl']]
        run('--exercise', '--ticks', '300', '--epoch', '1793966460', '--seed', '3005', '--record', tape, '--trace', first)
        run('--replay', tape, '--trace', second)
        assert first.read_bytes() == second.read_bytes(), compare(first, second)
        environment = dict(os.environ, TZ='UTC+12')
        run('--replay', tape, '--trace', temporary / 'timezone.jsonl', env=environment)
        assert first.read_bytes() == (temporary / 'timezone.jsonl').read_bytes()
        rows = [json.loads(line) for line in first.read_text(encoding='utf-8').splitlines()]
        assert len(rows) == 301 and all(row['scene'] == 'game' for row in rows[1:])
        assert rows[-1]['bulletClones'] > 0 and rows[-1]['rngDraws'] > 0
        assert any(event['kind'] == 'random' for row in rows[1:] for event in row['events'])
        assert any(event['kind'] == 'shot' and event['block'] for row in rows[1:] for event in row['events'])
        # The source's voting phase must come from injected time, not today's clock.
        vote = temporary / 'vote.replay'
        vote.write_text(tape.read_text(encoding='utf-8').replace('epoch 1793966460', 'epoch 1793966400'), encoding='utf-8')
        run('--replay', vote, '--trace', temporary / 'vote.jsonl')
        voting = [json.loads(line) for line in (temporary / 'vote.jsonl').read_text(encoding='utf-8').splitlines()][1:]
        assert all(row['scene'] == 'vote' for row in voting)
        # A changed seed must affect simulated random events, not just the header.
        changed_seed = temporary / 'seed.replay'
        changed_seed.write_text(tape.read_text(encoding='utf-8').replace('seed 3005', 'seed 3006'), encoding='utf-8')
        run('--replay', changed_seed, '--trace', temporary / 'seed.jsonl')
        seeded = [json.loads(line) for line in (temporary / 'seed.jsonl').read_text(encoding='utf-8').splitlines()]
        assert rows[1:] != seeded[1:]
        # A changed input produces an actionable first-divergence report.
        changed_input = temporary / 'input.replay'
        lines = tape.read_text(encoding='utf-8').splitlines()
        index = next(i for i, line in enumerate(lines) if line.startswith('65\t'))
        lines[index] = lines[index].replace('"d"', '"a"')
        changed_input.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        run('--replay', changed_input, '--trace', temporary / 'input.jsonl')
        assert compare(first, temporary / 'input.jsonl').startswith('tick 65:')
        changed_rows = [json.loads(line) for line in (temporary / 'input.jsonl').read_text(encoding='utf-8').splitlines()]
        assert any(a['vx'] != b['vx'] or a['x'] != b['x'] for a, b in zip(rows[66:76], changed_rows[66:76]))
        invalid = temporary / 'invalid.replay'
        invalid.write_text(tape.read_text(encoding='utf-8').replace('BBDX_REPLAY 1', 'BBDX_REPLAY 99'), encoding='utf-8')
        run('--replay', invalid, success=False)
        run('--ticks', '10001', success=False)
        run('--ticks', '300garbage', success=False)
        run('--seed', '-1', success=False)
        run('--replay', tape, '--seed', '4', success=False)
        run('--replay', tape, '--trace', tape, success=False)
        hashes = {'format': 1, 'traceSha256': hashlib.sha256(first.read_bytes()).hexdigest(),
                  'sourceSha256': rows[0]['sourceSha256'], 'rowSha256': [hashlib.sha256(line).hexdigest() for line in first.read_bytes().splitlines()]}
        if args.update_baseline:
            from workspace_budget import check_budget
            check_budget(1_000_000)
            fixture.parent.mkdir(parents=True, exist_ok=True)
            fixture.write_bytes(tape.read_bytes())
            golden.write_bytes(first.read_bytes())
            baseline.write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8', newline='\n')
        else:
            assert golden.read_bytes() == first.read_bytes(), compare(golden, first)
            assert fixture.read_bytes() == tape.read_bytes(), 'Recorded input fixture changed'
            expected = json.loads(baseline.read_text(encoding='utf-8'))
            if expected != hashes:
                first_bad = next((i for i, pair in enumerate(zip(expected['rowSha256'], hashes['rowSha256'])) if pair[0] != pair[1]), None)
                raise AssertionError(f'Gameplay baseline changed at row {first_bad}; review behavior before updating the baseline')
        print(f'Replay checks passed: 300 ticks, byte-identical processes/timezones, seed/time/input sensitivity, invalid-input rejection. Trace {first.stat().st_size:,} bytes.')


if __name__ == '__main__':
    main()
