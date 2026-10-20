#!/usr/bin/env python3
"""Refresh curated catalog display metadata from individual GitHub projects."""
import argparse
import base64
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REPO_URL = re.compile(r"https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)\Z")
MAX_MANIFEST = 128 * 1024
MAX_API_RESPONSE = 2 * 1024 * 1024


def repository(url):
    match = REPO_URL.fullmatch(url) if isinstance(url, str) else None
    if not match:
        raise ValueError(f"Invalid GitHub repository URL: {url!r}")
    return match.group(1)


def validate(catalog):
    if catalog.get("schema") != 1 or not isinstance(catalog.get("ports"), list):
        raise ValueError("Expected catalog schema 1 and ports array")
    ids = set()
    for port in catalog["ports"]:
        for key in ("id", "name", "game", "system"):
            if not isinstance(port.get(key), str) or not port[key].strip():
                raise ValueError(f"Missing nonempty {key}")
        if port["id"] in ids:
            raise ValueError(f"Duplicate port ID: {port['id']}")
        ids.add(port["id"])
        repository(port.get("repo"))
        if not isinstance(port.get("linux"), bool):
            raise ValueError(f"Expected Linux boolean for {port['id']}")
        for key in ("serials", "titles", "files"):
            values = port.get(key)
            if not isinstance(values, list) or any(not isinstance(v, str) or not v.strip() for v in values):
                raise ValueError(f"Expected {key} string array for {port['id']}")


class GitHub:
    def __init__(self, executable="gh"):
        self.executable = executable

    def get(self, endpoint, optional=False):
        result = subprocess.run(
            [self.executable, "api", endpoint], capture_output=True, timeout=30
        )
        if result.returncode:
            if optional and b"(HTTP 404)" in result.stderr:
                return None
            # Do not echo subprocess output, which may contain local auth details.
            raise RuntimeError(f"GitHub request failed: {endpoint}")
        if len(result.stdout) > MAX_API_RESPONSE:
            raise ValueError(f"Oversized GitHub response: {endpoint}")
        return json.loads(result.stdout)


def read_manifest(content):
    if content is None:
        return None
    if not isinstance(content, dict) or content.get("encoding") != "base64":
        raise ValueError("Expected inline base64 manifest")
    size = content.get("size")
    if not isinstance(size, int) or not 0 <= size <= MAX_MANIFEST:
        raise ValueError("Manifest exceeds size limit")
    data = base64.b64decode(content["content"].replace("\n", ""), validate=True)
    if len(data) > MAX_MANIFEST:
        raise ValueError("Manifest exceeds size limit")
    manifest = json.loads(data)
    if not isinstance(manifest, dict):
        raise ValueError("Expected manifest object")
    return manifest


def refresh(catalog, api):
    validate(catalog)
    updated = copy.deepcopy(catalog)
    cache = {}
    changes = []
    for port in updated["ports"]:
        repo = repository(port["repo"])
        if repo not in cache:
            info = api.get(f"repos/{repo}")
            canonical = info["html_url"]
            canonical_repo = repository(canonical)
            manifest = read_manifest(api.get(f"repos/{canonical_repo}/contents/.recomp.json", optional=True))
            cache[repo] = canonical, manifest
        canonical, manifest = cache[repo]
        fields = {"repo": canonical}
        if manifest:
            project = manifest.get("project")
            if isinstance(project, str) and project.strip() and len(project) <= 200 and not any(ord(c) < 32 for c in project):
                fields["name"] = project.strip()
            targets = manifest.get("targets")
            if isinstance(targets, list) and targets and all(isinstance(t, str) and t.strip() for t in targets):
                fields["linux"] = any(t.strip().casefold() == "linux" for t in targets)
        for field, value in fields.items():
            if port[field] != value:
                changes.append((port["id"], field, port[field], value, canonical))
                port[field] = value
    validate(updated)
    return updated, changes


def report(changes):
    lines = ["Refresh the bundled native port catalog from individual project repositories.", ""]
    # JSON quotes/escapes upstream strings. HTML escaping prevents injected markup.
    import html
    for port_id, field, before, after, source in changes:
        old = html.escape(json.dumps(before, ensure_ascii=False)).replace("`", "&#96;")
        new = html.escape(json.dumps(after, ensure_ascii=False)).replace("`", "&#96;")
        lines.append(f"- {port_id}: {field} from {old} to {new}. [Source]({source})")
    lines += ["", "Detection IDs, games, consoles, dump matches and installed filenames remain curated.",
              "No downloads or installs are performed. Review upstream claims before merging.",
              "", "Validation: offline catalog maintenance tests and catalog validation.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=ROOT / "resources/native-ports.json")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--check", action="store_true", help="Validate locally without network or writes")
    parser.add_argument("--gh", default=os.environ.get("GH_BIN", "gh"))
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text())
    validate(catalog)
    if args.check:
        print(f"Validated {len(catalog['ports'])} native ports")
        return
    updated, changes = refresh(catalog, GitHub(args.gh))
    # Fetch everything before writing, so API failures leave the catalog intact.
    if changes:
        args.catalog.write_text(json.dumps(updated, indent=2, ensure_ascii=False) + "\n")
    if args.report:
        args.report.write_text(report(changes))
    print(f"Checked {len(catalog['ports'])} native ports; {len(changes)} metadata changes")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        print(f"Catalog refresh failed: {error}", file=sys.stderr)
        sys.exit(1)
