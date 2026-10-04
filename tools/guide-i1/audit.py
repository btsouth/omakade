#!/usr/bin/env python3
"""Runtime contrast audit of the guide's applied tokens in an isolated omabox."""
import json
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
def run(*args):
    return subprocess.check_output(['omabox', *args], cwd=repo, text=True)
themes = sorted(p.name.removesuffix('-game.png') for p in Path('/home/bts/Projects/_evidence/omakade/guide-v2-d2-2026-10-04/themes').glob('*-game.png'))
if len(themes) != 22:
    raise SystemExit('Expected the 22 approved D2 themes')
fixture = str(repo / 'tools/guide-overlay-preview/fixtures/lantern-road.json')
run('run', '--', 'omarchy-shell', 'shell', 'summon', 'omakade.guide', json.dumps({'fixture': fixture, 'pad': 'xbox'}))
for theme in themes:
    run('run', '--', 'omarchy-theme-set', theme)
    run('wait', 'still')
    run('wait', 'cmd', '--', 'bash', '-c', 'test "$(omarchy-shell omakade.guide ready)" = ready')
    run('run', '--', 'omarchy-shell', 'shell', 'summon', 'omakade.guide', json.dumps({'audit': theme}))
    run('shot', '-o', str(out / (theme + '.png')))
log = run('log', 'shell')
(out / 'shell.log').write_text(log)
rows = {}
for line in log.splitlines():
    if 'GUIDE_AUDIT ' in line:
        data = json.loads(line.split('GUIDE_AUDIT ', 1)[1])
        if data['theme'] in themes: rows[data['theme']] = data
assert sorted(rows) == themes, (sorted(rows), themes)
checks = 0
for theme, row in rows.items():
    for role in row['audit']['roles'] + row['pairings']:
        assert role['pass'], (theme, role)
        checks += 1
    for glyph in row['glyphs']:
        assert glyph['contrast'] >= 3, (theme, glyph)
        checks += 1
result = f'PASS: {len(rows)} runtime themes, {checks}/{checks} legibility and glyph checks.'
(out / 'contrast.json').write_text(json.dumps({'result': result, 'themes': list(rows.values())}, indent=2) + '\n')
(out / 'contrast.md').write_text(result + '\n\nApproved D2 floors unchanged. Audit uses applied runtime tokens over worst-case black and white frames.\n')
print(result)
