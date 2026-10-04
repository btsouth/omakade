#!/usr/bin/env python3
"""Writes themes.js from the installed Omarchy themes, mapped the way OmarchyTheme does."""
import json, pathlib, re

ROOT = pathlib.Path("/usr/share/omarchy/themes")
KEYS = ["accent", "selection", "muted", "background", "dark_background", "darker_background",
        "lighter_background", "foreground", "dark_foreground", "light_foreground",
        "bright_foreground", "red", "yellow", "green", "cyan", "blue", "magenta"]
LEGACY = {"background": "bg", "dark_background": "dark_bg", "darker_background": "darker_bg",
          "lighter_background": "lighter_bg", "foreground": "fg"}


def lum(hex_):
    h = hex_.lstrip("#")[:6]
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    c = [v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


themes = {}
for path in sorted(ROOT.glob("*/colors.toml")):
    values = {}
    for line in path.read_text().splitlines():
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s*=\s*["\']([^"\']+)["\']', line)
        if m:
            values[m.group(1)] = m.group(2)
    for key, old in LEGACY.items():
        values.setdefault(key, values.get(old, ""))
    bg = values.get("background") or "#1a1b26"
    fg = values.get("foreground") or "#c0caf5"
    for key in KEYS:
        values.setdefault(key, "")
    values["background"], values["foreground"] = bg, fg
    for key, fallback in [("dark_background", bg), ("darker_background", bg),
                          ("lighter_background", bg), ("dark_foreground", fg),
                          ("light_foreground", fg), ("bright_foreground", fg),
                          ("accent", values.get("blue") or fg), ("selection", values.get("lighter_background") or bg),
                          ("muted", values.get("dark_foreground") or fg)]:
        if not values[key]:
            values[key] = fallback
    for key in ["red", "yellow", "green", "cyan", "blue", "magenta"]:
        if not values[key]:
            values[key] = values["accent"]
    candidates = [values["muted"], values["dark_foreground"], values["foreground"]]
    muted_text = next((c for c in candidates if min(contrast(c, values[b]) for b in
                       ("background", "dark_background", "darker_background")) >= 3.0), fg)
    name = " ".join(w.capitalize() for w in path.parent.name.split("-"))
    entry = {k.replace("_", ""): values[k] for k in KEYS}
    entry.update({"name": name, "slug": path.parent.name, "mode": values.get("mode", "dark"),
                  "mutedtext": muted_text})
    themes[path.parent.name] = entry

out = pathlib.Path(__file__).with_name("themes.js")
out.write_text(".pragma library\nvar themes = " + json.dumps(themes, indent=1) + ";\n")
print(f"{len(themes)} themes -> {out}")
