"""Report the first differing tick/field instead of an opaque file mismatch."""
import json
from pathlib import Path
import sys


def difference(expected, actual, path='$'):
    if type(expected) is not type(actual):
        return path, expected, actual
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return path + '.keys', list(expected), list(actual)
        for key in expected:
            result = difference(expected[key], actual[key], path + '.' + key)
            if result:
                return result
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return path + '.length', len(expected), len(actual)
        for index, (left, right) in enumerate(zip(expected, actual)):
            result = difference(left, right, f'{path}[{index}]')
            if result:
                return result
    elif expected != actual:
        return path, expected, actual
    return None


def compare(expected, actual):
    left, right = Path(expected).read_text(encoding='utf-8').splitlines(), Path(actual).read_text(encoding='utf-8').splitlines()
    for index, (a, b) in enumerate(zip(left, right)):
        result = difference(json.loads(a), json.loads(b))
        if result:
            return f'{"header" if index == 0 else "tick " + str(index-1)}: {result[0]}: {result[1]!r} -> {result[2]!r}'
    if len(left) != len(right):
        return f'row count: {len(left)} -> {len(right)}'
    return None


if __name__ == '__main__':
    result = compare(sys.argv[1], sys.argv[2])
    print(result or 'Traces match tick-for-tick.')
    raise SystemExit(1 if result else 0)
