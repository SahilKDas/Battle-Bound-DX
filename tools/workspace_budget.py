"""Fail closed before generated writes reach the workspace budget."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 450_000_000  # decimal MB; leaves 50 MB below the user's ceiling


def check_budget(reserve=0):
    total = sum(p.stat().st_size for p in ROOT.rglob('*') if p.is_file())
    if total + reserve >= LIMIT:
        raise SystemExit(f'Workspace budget exceeded: {total:,} bytes + {reserve:,} reserved >= {LIMIT:,}. No generated write allowed.')
    print(f'Workspace: {total / 1_000_000:.2f} MB / 450 MB tool limit (500 MB ceiling)')
    return total


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--reserve', type=int, default=0)
    args = parser.parse_args()
    if args.reserve < 0:
        parser.error('reserve must be nonnegative')
    check_budget(args.reserve)
