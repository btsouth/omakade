#!/usr/bin/env python3
"""Write the bundled console card art from the PNG masters in resources/consoles.

Only consoles in ConsoleCatalog get a card. Each one is scaled to the largest size a card is
drawn at (the Game Mode cover on a 4K screen) and saved as WebP in resources/consoles/cards,
the only folder the build bundles. Needs Pillow with WebP support.
"""
import argparse
from pathlib import Path
import re
import sys

from PIL import Image, features

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "src" / "library" / "ConsoleCatalog.cpp"
MASTERS = ROOT / "resources" / "consoles"
CARDS = MASTERS / "cards"
SIZE = (768, 1152)
QUALITY = 90

# Catalog IDs whose master is named after an older system ID.
MASTER_NAMES = {"ds": "nds"}


def catalog_ids():
    ids = re.findall(r'\.id = QStringLiteral\("([a-z0-9]+)"\)', CATALOG.read_text())
    if not ids:
        raise SystemExit(f"No console IDs found in {CATALOG}")
    return ids


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    if not features.check("webp"):
        raise SystemExit("This Pillow has no WebP support")

    CARDS.mkdir(exist_ok=True)
    written = set()
    missing = []
    ids = catalog_ids()
    for system in ids:
        master = MASTERS / f"{MASTER_NAMES.get(system, system)}.png"
        if not master.is_file():
            missing.append(system)
            continue
        with Image.open(master) as image:
            card = image.convert("RGB").resize(SIZE, Image.Resampling.LANCZOS)
        target = CARDS / f"{system}.webp"
        card.save(target, "WEBP", quality=QUALITY, method=6)
        written.add(target.name)

    # Keep a card whose master is missing; drop only cards for IDs outside the catalog.
    known = {f"{system}.webp" for system in ids}
    for stale in CARDS.glob("*.webp"):
        if stale.name not in known:
            stale.unlink()

    total = sum((CARDS / name).stat().st_size for name in written)
    print(f"Wrote {len(written)} cards, {total / 1024 / 1024:.1f} MiB")
    if missing:
        print("No master PNG for: " + ", ".join(missing), file=sys.stderr)


if __name__ == "__main__":
    main()
