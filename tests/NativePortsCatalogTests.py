#!/usr/bin/env python3
"""Deterministic maintenance tests. No network or desktop session required."""
import base64
import copy
import importlib.util
import json
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


if __name__ == "__main__":
    unittest.main()
