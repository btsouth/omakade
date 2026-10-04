#!/usr/bin/env python3
"""Report the shared QML audit and spot-check exported 8-bit glass surfaces.

The QML audit covers worst-case black/white input before every protective layer.
Pixel samples check that the actual layered renderer preserves those floors.
No game artwork or captured text is used to infer contrast.
"""
import argparse
import csv
import json
from pathlib import Path

from PIL import Image


def luminance(color):
    if isinstance(color, str):
        color = [int(color[i:i + 2], 16) for i in (1, 3, 5)]
    channels = [v / 255 for v in color[:3]]
    linear = [v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4 for v in channels]
    return sum(c * w for c, w in zip(linear, (0.2126, 0.7152, 0.0722)))


def contrast(a, b):
    low, high = sorted((luminance(a), luminance(b)))
    return (high + 0.05) / (low + 0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audit_log", type=Path)
    parser.add_argument("black_dir", type=Path)
    parser.add_argument("white_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = json.loads(args.audit_log.read_text().split("CONTRAST_AUDIT ", 1)[1].splitlines()[0])
    source = Path(__file__).with_name("themes.js").read_text()
    themes = json.loads(source.split("var themes = ", 1)[1].removesuffix(";\n"))
    assert len(data) == len(themes) == 22
    assert {row["slug"] for row in data} == set(themes)
    keys = {"body": "foreground", "bright": "brightforeground", "muted": "mutedtext", "accent": "accent"}
    # Empty patches inside header, each action, capture captions, device card and clock.
    # These coordinates are for the 1920x1080 home-page export, not arbitrary layouts.
    patches = [(630, 200), (630, 836), (630, 970), (1860, 64)]
    patches += [(630, 400 + i * 43) for i in range(8)]
    details = []
    for theme in data:
        for frame, directory in (("black", args.black_dir), ("white", args.white_dir)):
            with Image.open(directory / (theme["slug"] + ".png")) as image:
                assert image.size == (1920, 1080)
                pixels = [image.getpixel(patch) for patch in patches]
            for role in theme["roles"]:
                rendered = min(contrast(themes[theme["slug"]][keys[role["role"]]], pixel) for pixel in pixels)
                role["rendered_" + frame] = rendered
                assert rendered >= role["floor"], (theme["slug"], frame, role["role"], rendered, role["floor"])
                assert role[frame] >= role["floor"]
                details.append({"theme": theme["theme"], "alpha": theme["alpha"], "frame": frame,
                                "role": role["role"], "analytic": role[frame], "rendered": rendered,
                                "floor": role["floor"], "pass": True})
        for role in theme["roles"]:
            role["verified_min"] = min(role["min"], role["rendered_black"], role["rendered_white"])
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "contrast.json").write_text(json.dumps(data, indent=2) + "\n")
    with (args.output / "contrast.csv").open("w") as output:
        writer = csv.DictWriter(output, fieldnames=details[0].keys())
        writer.writeheader()
        writer.writerows(details)
    lines = ["Each cell is minimum / required contrast. Minimum includes both black and white frames,",
             "the shared analytical calculation, and sampled 8-bit renders. All 176 role/frame checks pass.",
             "Floors: body/bright min(7, 90% of native theme contrast), muted min(4.5, 85%), accent min(3, 85%).",
             "The unchanged 0.6 glass base precedes the computed tint. Alpha includes blend-rounding bounds.", "",
             "| Theme | Tint | Body | Bright | Muted | Accent | Result |",
             "| --- | ---: | ---: | ---: | ---: | ---: | --- |"]
    for theme in data:
        cells = [f"{role['verified_min']:.2f} / {role['floor']:.2f}" for role in theme["roles"]]
        lines.append(f"| {theme['theme']} | {theme['alpha']:.2f} | " + " | ".join(cells) + " | PASS |")
    (args.output / "contrast.md").write_text("\n".join(lines) + "\n")
    print("PASS: 22 themes, 176 role/frame checks; analytical and rendered floors met.")


if __name__ == "__main__":
    main()
