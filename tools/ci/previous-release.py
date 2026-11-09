#!/usr/bin/env python3
"""Find the published package the release workflow upgrades from.

Prints one line, "<tag>\\t<asset>", or nothing when there is no baseline. Only two
misses are allowed: no earlier published release, or an earlier release without a
package for this architecture. Any gh or API error fails, so an auth or network
problem can't quietly turn the upgrade test into a clean install.

When the tag being built is a version (v1.18.1), the baseline is the highest
release below it, so a hotfix for an older line upgrades from that line instead of
downgrading from a newer release. Any other ref, such as a manual run on a branch,
uses the newest release.
"""

import argparse
import json
import re
import subprocess
import sys

VERSION_TAG = re.compile(r"v?(\d+(?:\.\d+)*)")


def tag_version(tag: str) -> tuple[int, ...] | None:
    match = VERSION_TAG.fullmatch(tag)
    return tuple(int(part) for part in match.group(1).split(".")) if match else None


def log(message: str) -> None:
    print(message, file=sys.stderr)


def gh_json(arguments: list[str]):
    try:
        result = subprocess.run(["gh", *arguments], capture_output=True, text=True)
    except OSError as error:
        raise SystemExit(f"Could not run gh: {error}")
    if result.returncode != 0:
        log(result.stderr.strip())
        raise SystemExit(f"gh {' '.join(arguments[:2])} failed with exit code {result.returncode}.")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise SystemExit(f"gh {' '.join(arguments[:2])} returned invalid JSON: {error}")


def find_baseline(repo: str, arch: str, current_tag: str) -> tuple[str, str] | None:
    releases = gh_json([
        "release", "list", "--repo", repo, "--exclude-drafts", "--exclude-pre-releases",
        "--limit", "20", "--json", "tagName",
    ])
    if not isinstance(releases, list):
        raise SystemExit("gh release list did not return a list.")
    # Newest first. A rerun after publishing would otherwise upgrade from itself.
    tags = [release.get("tagName") for release in releases if isinstance(release, dict)]
    tags = [tag for tag in tags if isinstance(tag, str) and tag and tag != current_tag]
    current = tag_version(current_tag)
    if current is not None:
        older = [(tag_version(tag), tag) for tag in tags]
        older = [(version, tag) for version, tag in older if version is not None and version < current]
        tags = [tag for _, tag in sorted(older, reverse=True)]
    if not tags:
        log("Upgrade baseline: none, the repository has no earlier published release.")
        return None

    tag = tags[0]
    release = gh_json(["release", "view", tag, "--repo", repo, "--json", "assets"])
    assets = release.get("assets") if isinstance(release, dict) else None
    if not isinstance(assets, list):
        raise SystemExit(f"gh release view {tag} did not return an asset list.")
    suffix = f"-{arch}.pkg.tar.zst"
    names = sorted(
        asset["name"] for asset in assets
        if isinstance(asset, dict) and isinstance(asset.get("name"), str)
        and asset["name"].startswith("omakade-") and asset["name"].endswith(suffix))
    if not names:
        log(f"Upgrade baseline: none, {tag} has no {arch} package.")
        return None
    if len(names) > 1:
        raise SystemExit(f"{tag} has more than one {arch} package: {', '.join(names)}")
    log(f"Upgrade baseline: {names[0]} from {tag}.")
    return tag, names[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--current-tag", default="")
    arguments = parser.parse_args()
    baseline = find_baseline(arguments.repo, arguments.arch, arguments.current_tag)
    if baseline:
        print("\t".join(baseline))
    return 0


if __name__ == "__main__":
    sys.exit(main())
