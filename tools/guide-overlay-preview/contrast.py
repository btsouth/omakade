#!/usr/bin/env python3
"""WCAG contrast of the colours the card draws, from `state` JSON (palette key).
The card is composited over a black and a white game frame; the lower ratio is reported."""
import json, sys

def parse(s):
    s = s.lstrip('#')
    if len(s) == 8: a, s = int(s[:2], 16) / 255, s[2:]
    else: a = 1.0
    return [int(s[i:i + 2], 16) / 255 for i in (0, 2, 4)], a

def over(top, base):
    (c, a) = top
    return [c[i] * a + base[i] * (1 - a) for i in range(3)]

def lum(c):
    f = lambda v: v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2])

def ratio(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)

print('| theme | text | dim text | quiet text | accent ink on its tint | urgent ink on its tint | focus ring vs card |')
print('|---|---|---|---|---|---|---|')
for path in sys.argv[1:]:
    p = json.load(open(path))['palette']
    rows = {}
    for frame in ([0, 0, 0], [1, 1, 1]):
        card = over(parse(p['background']), frame)
        accent, urgent = parse(p['accentInk']), parse(p['urgentInk'])
        accent_tint = over((accent[0], 0.12), card)
        urgent_tint = over((urgent[0], 0.08), card)
        vals = dict(text=ratio(over(parse(p['text']), card), card),
                    dim=ratio(over(parse(p['dim']), card), card),
                    quiet=ratio(over(parse(p['quiet']), card), card),
                    accent=ratio(over(accent, accent_tint), accent_tint),
                    urgent=ratio(over(urgent, urgent_tint), urgent_tint),
                    ring=ratio(over((accent[0], 0.85), card), card))
        for k, v in vals.items():
            rows[k] = min(rows.get(k, 99), v)
    name = path.rsplit('/', 1)[-1].rsplit('.', 1)[0]
    print(f"| {name} | {rows['text']:.1f} | {rows['dim']:.1f} | {rows['quiet']:.1f} | {rows['accent']:.1f} | {rows['urgent']:.1f} | {rows['ring']:.1f} |")
