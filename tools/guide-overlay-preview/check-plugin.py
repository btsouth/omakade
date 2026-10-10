#!/usr/bin/env python3
"""Drive the omakade.guide card through its fixtures inside an omabox box and
check the focus model, every action and the layout of every state.

    omabox up --plugin "$PWD/omarchy-plugin"
    omabox run -- python3 tools/guide-overlay-preview/check-plugin.py [SCALE...]

Fixture mode logs actions instead of running them; the card's state says
which ran last (`geometry.lastAct`). Exits non-zero on the first failure.
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, 'fixtures')


def shell(*args):
    return subprocess.run(['omarchy-shell', *args], check=True, capture_output=True, text=True).stdout.strip()


def state():
    return json.loads(shell('shell', 'call', 'omakade.guide', 'state', ''))


def press(*actions):
    for action in actions:
        shell('omakade.guide', 'input', action)
    return state()


def summon(fixture, scale=1.25, pad='xbox'):
    shell('shell', 'hide', 'omakade.guide')
    deadline = time.time() + 5
    while state()['opened'] and time.time() < deadline:
        time.sleep(0.05)
    payload = {'fixture': os.path.join(FIXTURES, fixture + '.json'), 'pad': pad, 'scale': scale}
    shell('shell', 'summon', 'omakade.guide', json.dumps(payload))
    deadline = time.time() + 10
    while time.time() < deadline:
        if shell('omakade.guide', 'ready') == 'ready':
            s = state()
            # The fixture file loads after the open; wait for its rows.
            if s['data'].get('clock'):
                time.sleep(0.15)
                return state()
        time.sleep(0.05)
    raise SystemExit(f'{fixture}: the card did not open')


failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print('FAIL', message)


def expect(actual, wanted, message):
    check(actual == wanted, f'{message}: got {actual!r}, wanted {wanted!r}')


def layout(name, s):
    g = s['geometry']
    check(g['fits'], f'{name}: card does not fit {s["surface"]}')
    check(not g['truncated'], f'{name}: truncated {g["truncated"]}')
    x, y, w, h = g['card']
    sw, sh = s['surface']
    check(x >= 0 and y >= 0 and x + w <= sw and y + h <= sh, f'{name}: card {g["card"]} outside {s["surface"]}')
    # The panel sits at the right edge, centred down the screen.
    check(sw - (x + w) < x, f'{name}: card {g["card"]} not at the right edge of {s["surface"]}')
    check(abs((y + h / 2) - sh / 2) <= 2 or y <= 10, f'{name}: card {g["card"]} not centred down {s["surface"]}')
    for key in s['rows']:
        if s['view'] == 'main':
            check(g['rows'].get(key), f'{name}: {key} not shown')
    # Buttons in a row share one width; every icon sits inside its control.
    for group in (('screenshot', 'record', 'replay'), ('desktop', 'retroarch')):
        boxes = [g['rows'][k] for k in group if g['rows'].get(k)]
        if boxes:
            check(max(b[2] for b in boxes) - min(b[2] for b in boxes) <= 1, f'{name}: {group} widths {[b[2] for b in boxes]}')
    for key, box in g['rows'].items():
        icon = g['icons'].get(key)
        if box and icon:
            check(icon[0] >= box[0] and icon[0] + icon[2] <= box[0] + box[2] + 1, f'{name}: {key} icon outside its control')


def reachable(s):
    """Every control is reached from Resume with the D-pad alone."""
    # Walk: down through every row, and left/right along each row.
    seen = set()
    for _ in range(len(s['rows']) + 2):
        cur = press('down')['cursor']
        seen.add(cur)
        for _ in range(5):
            seen.add(press('right')['cursor'])
        for _ in range(5):
            seen.add(press('left')['cursor'])
    return seen


def main(scales):
    # Focus model and every action, at the couch scale.
    s = summon('lantern-road')
    expect(s['cursor'], 'resume', 'opens on Resume')
    expect(s['rows'], ['resume', 'screenshot', 'record', 'volume', 'quit'], 'rows')
    expect(press('up')['cursor'], 'quit', 'up from Resume wraps to Quit')
    expect(press('up', 'up')['cursor'], 'screenshot', 'up to the first tile')
    expect(press('right')['cursor'], 'record', 'along the tiles')
    expect(press('right')['cursor'], 'record', 'tiles stop at the end')
    expect(press('down', 'down')['cursor'], 'quit', 'down through volume to Quit')
    expect(press('left')['cursor'], 'quit', 'left on Quit stays')
    expect(press('down', 'down')['cursor'], 'record', 'Record keeps its side round past Resume')
    expect(reachable(state()), set(state()['rows']), 'every control reachable')

    s = summon('lantern-road')
    press('down', 'down')
    expect(round(press('right')['data']['audio']['volume'], 2), 0.77, 'right raises the volume')
    expect(round(press('left', 'left')['data']['audio']['volume'], 2), 0.67, 'left lowers the volume')
    expect(press('a')['data']['audio']['muted'], True, 'A mutes')
    expect(press('a')['data']['audio']['muted'], False, 'A unmutes')
    expect(press('y')['geometry']['lastAct'], 'screenshot', 'Y takes a screenshot')

    for fixture, path, act in (('lantern-road', ['down', 'a'], 'screenshot'), ('lantern-road', ['down', 'right', 'a'], 'record'),
                               ('desktop-retroarch', ['down', 'a'], 'desktop'),
                               ):
        summon(fixture)
        s = press(*path)
        expect(s['geometry']['lastAct'], act, f'A on {act}')
        if act != 'screenshot':
            expect(s['opened'], False, f'{act} closes the card')
    summon('lantern-road')
    expect(press('a')['opened'], False, 'A on Resume closes the card')
    summon('lantern-road')
    expect(press('b')['opened'], False, 'B resumes')
    summon('lantern-road')
    s = press('up', 'a')
    expect((s['view'], s['confirmChoice']), ('confirm', 0), 'Quit asks, on Keep playing')
    s = press('a')
    expect((s['view'], s['cursor'], s['opened']), ('main', 'quit', True), 'Keep playing returns')
    s = press('a', 'right')
    expect(s['confirmChoice'], 1, 'right to Quit')
    s = press('a')
    expect((s['geometry']['lastAct'], s['opened']), ('quit-confirmed', False), 'Quit quits')
    summon('lantern-road')
    s = press('up', 'a', 'y')
    expect((s['view'], s['geometry']['lastAct']), ('confirm', 'screenshot'), 'Y takes a screenshot in the question')
    s = press('b')
    expect((s['view'], s['opened']), ('main', True), 'B leaves the question')
    s = press('b')
    expect(s['opened'], True, 'a second B at once does not also close')

    s = summon('desktop-retroarch')
    expect(s['rows'], ['resume', 'desktop', 'screenshot', 'record', 'volume', 'quit'], 'Desktop row; RetroArch is a hint')
    expect(press('down', 'down')['cursor'], 'screenshot', 'Desktop leads down to the tiles')

    s = summon('replay-buffer')
    expect(s['rows'][:4], ['resume', 'screenshot', 'record', 'replay'], 'replay is a third tile')
    expect(press('a')['geometry']['lastAct'], 'save-replay', 'A on Save 30 s')

    s = summon('sound-outputs')
    expect(s['cursor'], 'output', 'fixture cursor')
    s = press('right')
    expect([o['current'] for o in s['data']['audio']['outputs']], [False, True, False], 'right picks the next output')
    s = press('a')
    expect([o['current'] for o in s['data']['audio']['outputs']], [False, False, True], 'A picks the next output')
    expect(press('down')['cursor'], 'quit', 'down from the output row')

    s = summon('unavailable')
    expect(s['rows'], ['resume', 'screenshot', 'record', 'quit'], 'no sound rows without audio')
    expect(s['geometry']['readouts'], 0, 'no readings without telemetry')
    s = summon('no-mangohud')
    expect(s['geometry']['readouts'], 2, 'CPU and GPU without MangoHud')
    s = summon('no-game')
    expect((s['rows'], s['cursor']), (['screenshot', 'record', 'volume'], 'screenshot'), 'no game: capture and sound only')

    # Layout of every state at every scale on this screen.
    for scale in scales:
        for fixture in sorted(f[:-5] for f in os.listdir(FIXTURES) if f.endswith('.json')):
            s = summon(fixture, scale)
            layout(f'{fixture}@{scale}', s)

    shell('shell', 'hide', 'omakade.guide')
    print(f'{len(failures)} failure(s)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main([float(a) for a in sys.argv[1:]] or [1, 1.25, 1.7]))
