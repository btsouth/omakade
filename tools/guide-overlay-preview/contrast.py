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

print('| theme | text | quiet text | selected ink on fill | quit ink on fill | fill vs card | focus edge vs card | recording mark |')
print('|---|---|---|---|---|---|---|---|')
for path in sys.argv[1:]:
    p = json.load(open(path))['palette']
    rows = {}
    for frame in ([0, 0, 0], [1, 1, 1]):
        card = over(parse(p['background']), frame)
        fill = over(parse(p['selectedBackground']), card)
        vals = dict(text=ratio(over(parse(p['text']), card), card),
                    quiet=ratio(over(parse(p['quiet']), card), card),
                    sel=ratio(over(parse(p['selectedInk']), fill), fill),
                    quit=ratio(over(parse(p['urgentInk']), fill), fill),
                    fill=ratio(fill, card),
                    edge=ratio(over(parse(p['focusEdge']), card), card) if p.get('focusEdge') else None,
                    rec=min(ratio(over(parse(p['recording']), card), card), ratio(over(parse(p['recording']), fill), fill)))
        for k, v in vals.items():
            if v is not None: rows[k] = min(rows.get(k, 99), v)
    name = path.rsplit('/', 1)[-1].rsplit('.', 1)[0]
    edge = f"{rows['edge']:.1f}" if 'edge' in rows else 'not drawn'
    print(f"| {name} | {rows['text']:.1f} | {rows['quiet']:.1f} | {rows['sel']:.1f} | {rows['quit']:.1f} | {rows['fill']:.2f} | {edge} | {rows['rec']:.1f} |")
