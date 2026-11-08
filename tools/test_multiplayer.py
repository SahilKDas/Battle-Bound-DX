"""Run two independent game processes over TCP and inspect their replay traces."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
TICKS = 245


def play(exe, folder, name, mode):
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = str(probe.getsockname()[1])

    def launch(side):
        username = 'Host' if side == 0 else 'Guest'
        header = (ROOT / 'tests/fixtures/gameplay.replay').read_text(encoding='utf-8').splitlines()[:6]
        header[3] = f'username "{username}"'
        header[5] = f'frames {TICKS}'
        rows = []
        for tick in range(TICKS):
            distance = 15 if mode in ('melee', 'hands', 'axe', 'katana', 'nunchaku', 'taser') else (
                35 if mode in ('shuriken', 'rock', 'grenade') else 68)
            keys = 'a' if side == 1 and tick < distance else ''
            if side == 0 and tick == 125:
                if mode in ('melee', 'hands', 'axe', 'katana', 'nunchaku', 'taser'):
                    keys = '2'
                elif mode in ('grenade', 'shuriken', 'smoke', 'rock'):
                    keys = '3'
            aim_y = (-12, 0, 12, 24)[max(0, tick - 135) // 15 % 4]
            aim_x = -120 if side == 0 else 120
            if mode == 'grenade':
                aim_x, aim_y = -5, -110
            firing = ((135 <= tick < 185 if side == 0 else 195 <= tick < 240) if mode == 'bullet'
                      else side == 0 and (135 <= tick < 157 or 175 <= tick < 197 or 210 <= tick < 232))
            if mode in ('grenade', 'smoke'):
                firing = side == 0 and 135 <= tick < 145
            rows.append(f'{tick}\t"{keys}"\t{int(firing)}\t{aim_x}\t{aim_y}\t0')
        replay = folder / f'{name}-{side}.replay'
        trace = folder / f'{name}-{side}.jsonl'
        replay.write_text('\n'.join(header + rows) + '\n', encoding='utf-8')
        endpoint = ['--host', port] if side == 0 else ['--join', '127.0.0.1', port]
        loadout = {'arrow': 'e', 'rocket': 'j', 'flame': 'y', 'grenade': 'f', 'shuriken': 'k',
                   'melee': 'l', 'hands': 'a', 'axe': 'c', 'katana': 'q', 'nunchaku': 'w',
                   'taser': 't', 'smoke': 'x', 'rock': 'i'}.get(mode)
        equip = ['--equip', loadout] if side == 0 and loadout else []
        render = []
        if mode == 'bullet':
            client_root = folder / username
            (client_root / 'scratch').mkdir(parents=True)
            render = ['--root', str(client_root), '--render-smoke', '--fps', str(60 if side == 0 else 144)]
        process = subprocess.Popen([str(exe), '--headless', '--replay', str(replay), '--trace',
                                    str(trace), *endpoint, *equip, *render],
                                   cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, encoding='utf-8')
        return process, trace

    host, host_trace = launch(0)
    guest = None
    try:
        time.sleep(.75)
        guest, guest_trace = launch(1)
        results = [(*process.communicate(timeout=40), process.returncode) for process in (guest, host)]
        assert all(code == 0 for _, _, code in results), results
        if mode == 'bullet':
            assert f'Rendered frames: {TICKS * 144 // 30}' in results[0][0]
            assert f'Rendered frames: {TICKS * 60 // 30}' in results[1][0]
    finally:
        for process in (host, guest):
            if process and process.poll() is None:
                process.kill()
                process.communicate()

    result = []
    for trace in (host_trace, guest_trace):
        ticks = [json.loads(line) for line in trace.read_text(encoding='utf-8').splitlines()][1:]
        assert len(ticks) == TICKS
        events = [dict(event, tick=tick['tick']) for tick in ticks for event in tick['events']
                  if event['sprite'] == '[Network Peer]' or
                  (event['kind'] == 'effect' and event['block'] == 'grenade-blast')]
        result.append(events)
    return result


def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='bbdx-two-client-') as temp:
        folder = Path(temp)
        bullets = play(exe, folder, 'smg', 'bullet')
        arrows = play(exe, folder, 'bow', 'arrow')
        rockets = play(exe, folder, 'rocket', 'rocket')
        flames = play(exe, folder, 'flame', 'flame')
        print('Network hit counts (host, guest):', *(tuple(sum(event["kind"] == "::Spiller-liv" for event in events) for events in hits) for hits in (bullets, arrows, rockets, flames)))
        assert all(any(event['block'] == 'bullet' and float(event['before']) - float(event['after']) in (7, 14)
                       for event in hits) for hits in bullets)
        assert any(event['block'] == 'arrow' and float(event['before']) - float(event['after']) == 30
                   for event in arrows[1]), arrows
        assert any(event['block'] == 'explosion' and float(event['before']) > float(event['after'])
                   for event in rockets[1]), rockets
        assert any(event['block'] == 'flame' and .049 < float(event['before']) - float(event['after']) < .301
                   for event in flames[1]), flames

        host, guest = play(exe, folder, 'grenade', 'grenade')
        local_blast, = [event for event in host if event['block'] == 'grenade-blast']
        remote_blast, = [event for event in guest if event['block'] == 'grenade-blast']
        assert local_blast['tick'] == remote_blast['tick'], (local_blast, remote_blast)
        for coordinate in ('before', 'after'):
            assert abs(float(local_blast[coordinate]) - float(remote_blast[coordinate])) < 1, (
                local_blast, remote_blast)
        assert any(event['block'] == 'explosion' and float(event['before']) > float(event['after'])
                   for event in guest), guest

        cases = [('shuriken', 'shuriken', 15), ('rock', 'rock', 30),
                 ('melee', 'melee', 20), ('hands', 'melee', 3), ('axe', 'melee', 21),
                 ('katana', 'melee', 25), ('nunchaku', 'melee', 2), ('taser', 'taser', 2.5)]
        for mode, block, expected in cases:
            _, guest = play(exe, folder, mode, mode)
            assert any(event['block'] == block and float(event['before']) - float(event['after']) == expected
                       for event in guest), (mode, guest)

        _, guest = play(exe, folder, 'smoke', 'smoke')
        smoke, = guest
        assert smoke['kind'] == 'effect' and smoke['block'] == 'smoke', guest
        print('Remote grenade fuse, splash, shuriken, rock, melee, taser and smoke checks passed.')


if __name__ == '__main__':
    main()
