#!/usr/bin/env python3
"""Deterministic maintenance tests. No network or desktop session required."""
import base64
import copy
import importlib.util
import json
import os
import textwrap
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("catalog", ROOT / "scripts/update-native-ports.py")
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


def content(manifest):
    data = json.dumps(manifest).encode()
    return {"size": len(data), "encoding": "base64", "content": base64.b64encode(data).decode()}


class FakeAPI:
    def __init__(self, manifest=None):
        self.manifest = manifest
        self.calls = []

    def get(self, endpoint, optional=False):
        self.calls.append((endpoint, optional))
        if endpoint.endswith("/.recomp.json"):
            return content(self.manifest) if self.manifest is not None else None
        return {"html_url": "https://github.com/new/port"}


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.data = {"schema": 1, "ports": [{
            "id": "port", "name": "Port", "game": "Game", "system": "ps4",
            "repo": "https://github.com/old/port", "linux": True,
            "serials": ["CUSA00001"], "titles": ["Game"], "files": ["port"]}]}

    def test_bundled_catalog(self):
        catalog.validate(json.loads((ROOT / "resources/native-ports.json").read_text()))

    def test_only_safe_fields_change_and_no_input_mutation(self):
        original = copy.deepcopy(self.data)
        api = FakeAPI({"project": "New Port", "targets": ["Windows"],
                       "game": "Other", "system": "PSP", "original": {"serial": "BAD"},
                       "id": "bad", "files": ["bad"], "titles": ["bad"]})
        result, changes = catalog.refresh(self.data, api)
        self.assertEqual(self.data, original)
        expected = dict(original["ports"][0], repo="https://github.com/new/port", name="New Port", linux=False)
        self.assertEqual(result["ports"][0], expected)
        self.assertEqual({c[1] for c in changes}, {"repo", "name", "linux"})
        self.assertEqual(api.calls[-1], ("repos/new/port/contents/.recomp.json", True))

    def test_missing_manifest_and_missing_or_invalid_fields_preserve_values(self):
        for manifest in (None, {}, {"targets": []}, {"targets": "Linux"},
                         {"targets": ["Windows", None]}, {"project": "  "}, {"project": "bad\nname"}):
            with self.subTest(manifest=manifest):
                result, changes = catalog.refresh(self.data, FakeAPI(manifest))
                self.assertEqual(result["ports"][0]["name"], "Port")
                self.assertTrue(result["ports"][0]["linux"])
                self.assertEqual(len(changes), 1)

    def test_linux_target_case_and_whitespace(self):
        self.data["ports"][0]["linux"] = False
        result, _ = catalog.refresh(self.data, FakeAPI({"targets": ["Windows", " linux "]}))
        self.assertTrue(result["ports"][0]["linux"])

    def test_no_churn_and_shared_repository_cache(self):
        self.data["ports"][0]["repo"] = "https://github.com/new/port"
        second = dict(self.data["ports"][0], id="second", files=[])
        self.data["ports"].append(second)
        api = FakeAPI({"project": "Port", "targets": ["Linux"]})
        result, changes = catalog.refresh(self.data, api)
        self.assertEqual(result, self.data)
        self.assertEqual(changes, [])
        self.assertEqual(len(api.calls), 2)

    def test_manifest_limits_and_json_shape(self):
        for data in ({"size": catalog.MAX_MANIFEST + 1}, content([]),
                     {"size": 1, "encoding": "base64", "content": "!"}):
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    catalog.read_manifest(data)

    def test_invalid_repository_and_duplicate_ids(self):
        for value in ("https://example.com/owner/repo", "https://github.com/owner/repo/extra", None):
            with self.assertRaises(ValueError):
                catalog.repository(value)
        self.data["ports"].append(copy.deepcopy(self.data["ports"][0]))
        with self.assertRaises(ValueError):
            catalog.validate(self.data)

    def test_api_errors_are_not_missing_manifests(self):
        api = catalog.GitHub()
        for status, optional, missing in ((404, True, True), (404, False, False), (403, True, False), (500, True, False)):
            result = subprocess.CompletedProcess([], 1, b"", f"gh: error (HTTP {status})".encode())
            with self.subTest(status=status, optional=optional), patch.object(catalog.subprocess, "run", return_value=result):
                if missing:
                    self.assertIsNone(api.get("repos/owner/repo/contents/.recomp.json", optional=True))
                else:
                    with self.assertRaises(RuntimeError):
                        api.get("repos/owner/repo", optional=optional)

    def test_failed_refresh_leaves_catalog_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "catalog.json"
            original = json.dumps(self.data)
            path.write_text(original)
            with patch.object(catalog.sys, "argv", ["update", "--catalog", str(path)]), \
                 patch.object(catalog.GitHub, "get", side_effect=RuntimeError("unavailable")):
                with self.assertRaises(RuntimeError):
                    catalog.main()
            self.assertEqual(path.read_text(), original)

    def test_report_escapes_upstream_markup(self):
        text = catalog.report([("port", "name", "Old", "<script>@x`", "https://github.com/new/port")])
        self.assertNotIn("<script>", text)
        self.assertNotIn("`", text)


class ProposalWorkflowTests(unittest.TestCase):
    """Execute the proposal step with hermetic git/gh commands, without a remote."""

    def propose(self, *, same=True, open_pr="", branch=True, changed=True):
        workflow = (ROOT / ".github/workflows/native-ports.yml").read_text()
        step = workflow.split("      - name: Propose catalog changes\n", 1)[1]
        script = textwrap.dedent(step.split("        run: |\n", 1)[1])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "resources").mkdir()
            (root / "resources/native-ports.json").write_text("current proposal\n")
            fake = root / "bin"
            fake.mkdir()
            command = r"""#!/usr/bin/python3
import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['COMMAND_LOG'], 'a') as log:
    log.write(json.dumps([name, *args]) + '\n')
if name == 'git':
    if args[:2] == ['diff', '--quiet']: sys.exit(1 if os.environ['CHANGED'] == '1' else 0)
    if args[0] == 'ls-remote': sys.exit(0 if os.environ['BRANCH'] == '1' else 2)
    if args[0] == 'show': print('current proposal' if os.environ['SAME'] == '1' else 'older proposal')
if name == 'gh' and args[:2] == ['pr', 'list']:
    print(os.environ['OPEN_PR'])
"""
            for name in ("git", "gh"):
                path = fake / name
                path.write_text(command)
                path.chmod(0o755)
            log = root / "commands.jsonl"
            env = dict(os.environ, PATH=str(fake) + os.pathsep + os.environ["PATH"],
                       RUNNER_TEMP=str(root), BASE_BRANCH="main", COMMAND_LOG=str(log),
                       SAME="1" if same else "0", OPEN_PR=open_pr,
                       BRANCH="1" if branch else "0", CHANGED="1" if changed else "0")
            result = subprocess.run(["bash", "-e", "-o", "pipefail", "-c", script],
                                    cwd=root, env=env, text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return [json.loads(line) for line in log.read_text().splitlines()]

    def test_closed_proposal_with_identical_branch_is_proposed_again(self):
        calls = self.propose(same=True, open_pr="")
        self.assertTrue(any(c[:3] == ["gh", "pr", "create"] for c in calls))
        self.assertTrue(any(c[:2] == ["git", "push"] for c in calls))

    def test_identical_open_proposal_has_no_churn(self):
        calls = self.propose(same=True, open_pr="123")
        self.assertFalse(any(c[:2] == ["git", "push"] for c in calls))
        self.assertFalse(any(c[:3] in (["gh", "pr", "create"], ["gh", "pr", "edit"]) for c in calls))

    def test_changed_open_proposal_is_updated(self):
        calls = self.propose(same=False, open_pr="123")
        self.assertTrue(any(c[:4] == ["gh", "pr", "edit", "123"] for c in calls))
        self.assertFalse(any(c[:3] == ["gh", "pr", "create"] for c in calls))

    def test_new_branch_creates_proposal(self):
        calls = self.propose(branch=False)
        self.assertTrue(any(c[:3] == ["gh", "pr", "create"] for c in calls))

    def test_unchanged_catalog_leaves_proposals_alone(self):
        calls = self.propose(changed=False)
        self.assertEqual(calls, [["git", "diff", "--quiet", "--", "resources/native-ports.json"]])


if __name__ == "__main__":
    unittest.main()
