"""Resolve original Scratch asset identities to descriptive source/runtime paths."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def asset_manifest():
    return json.loads((ROOT / 'assets/manifest.json').read_text(encoding='utf-8'))['files']


def source_path(asset, manifest):
    return manifest[asset['md5ext']]['sourcePath']


def runtime_path(asset, manifest):
    return manifest[asset['md5ext']]['runtimePath']
