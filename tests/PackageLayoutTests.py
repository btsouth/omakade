#!/usr/bin/env python3
"""Installs the build into a scratch prefix and checks what the package ships."""

import json
import re
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

CMAKE, BUILD, VERSION, LIBDIR, DATADIR, BINDIR, SOURCE = sys.argv[1:8]
del sys.argv[1:8]

LINK = re.compile(r"\]\(<?([^)\s>]+)>?(?:\s+\"[^\"]*\")?\)|<img\s[^>]*src=\"([^\"]+)\"|^\[[^\]]+\]:\s*(\S+)",
                  re.MULTILINE)


def without_code_blocks(path: Path) -> str:
    return re.sub(r"^\s*```.*?^\s*```", "", path.read_text(), flags=re.MULTILINE | re.DOTALL)


def markdown_text(path: Path) -> str:
    """The page without code, where link-like text is not a link."""
    return re.sub(r"`[^`\n]*`", "", without_code_blocks(path))


def anchors(path: Path) -> set[str]:
    """Heading anchors as GitHub makes them, plus explicit ids."""
    found: set[str] = set()
    counts: dict[str, int] = {}
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", without_code_blocks(path), re.MULTILINE):
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        found.add(f"{slug}-{counts[slug]}" if slug in counts else slug)
        counts[slug] = counts.get(slug, 0) + 1
    found.update(re.findall(r"<a\s+(?:id|name)=\"([^\"]+)\"", path.read_text()))
    return found


class PackageLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.scratch = tempfile.TemporaryDirectory()
        cls.prefix = Path(cls.scratch.name) / "usr"
        subprocess.run([CMAKE, "--install", BUILD, "--prefix", str(cls.prefix)],
                       check=True, stdout=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.scratch.cleanup()

    def test_user_services_are_not_enabled_for_every_account(self) -> None:
        units = self.prefix / LIBDIR / "systemd" / "user"
        self.assertTrue((units / "omakade-sessiond.service").is_file())
        self.assertTrue((units / "omakade-guide-button.service").is_file())
        self.assertEqual([str(path) for path in units.glob("*.wants")], [])

    def test_user_services_start_binaries_from_the_configured_prefix(self) -> None:
        units = self.prefix / LIBDIR / "systemd" / "user"
        for name in ("omakade-sessiond", "omakade-guide-button"):
            lines = (units / f"{name}.service").read_text().splitlines()
            exec_lines = [line for line in lines if line.startswith("ExecStart=")]
            self.assertEqual(exec_lines, [f"ExecStart={BINDIR}/{name}"])

    def test_omastore_services_match_the_packaged_units(self) -> None:
        manifest = tomllib.loads((Path(SOURCE) / "omastore.toml").read_text())
        services = {service["unit"]: service for service in manifest["services"].values()}
        units = self.prefix / LIBDIR / "systemd" / "user"
        self.assertEqual(sorted(services), sorted(path.name for path in units.glob("*.service")))
        for unit, service in services.items():
            self.assertTrue((self.prefix / "bin" / Path(service["exec"]).name).is_file(), unit)
            self.assertEqual(service["exec"], "usr/bin/" + unit.removesuffix(".service"))
        # Settings turns the Home button on; installing must not.
        self.assertFalse(services["omakade-guide-button.service"]["enable"])
        self.assertFalse(services["omakade-guide-button.service"]["start"])

    def test_plugin_manifest_matches_the_package(self) -> None:
        path = self.prefix / DATADIR / "omakade" / "omarchy-plugin" / "manifest.json"
        manifest = json.loads(path.read_text())
        self.assertEqual(manifest["id"], "omakade.guide")
        self.assertEqual(manifest["version"], VERSION)
        self.assertEqual(manifest["license"], "GPL-3.0-or-later")
        self.assertEqual(manifest["entryPoints"], {"overlay": "Guide.qml"})

    def test_installed_documentation_links_resolve(self) -> None:
        docs = self.prefix / DATADIR / "doc" / "omakade"
        pages = sorted(docs.rglob("*.md"))
        self.assertIn(docs / "docs" / "TROUBLESHOOTING.md", pages)
        broken = []
        for page in pages:
            for match in LINK.finditer(markdown_text(page)):
                link = next(group for group in match.groups() if group)
                if re.match(r"[a-z][a-z0-9+.-]*:", link):
                    continue
                target, _, anchor = link.partition("#")
                path = (page.parent / target) if target else page
                if not path.exists():
                    broken.append(f"{page.relative_to(docs)}: {link}")
                elif anchor and path.suffix == ".md" and anchor not in anchors(path):
                    broken.append(f"{page.relative_to(docs)}: {link} (no such heading)")
        self.assertEqual(broken, [])

    def test_package_ships_the_app_without_its_test_harness(self) -> None:
        binaries = self.prefix / "bin"
        self.assertEqual(sorted(path.name for path in binaries.iterdir()),
                         ["omakade", "omakade-guide-button", "omakade-guide-guard", "omakade-sessiond"])
        # The options only omakade-uitest reads. Qt keeps QStringLiteral text as UTF-16.
        app = (binaries / "omakade").read_bytes()

        def mentions(option: str) -> bool:
            return option.encode() in app or option.encode("utf-16-le") in app

        self.assertTrue(mentions("--smoke-test"))
        harness = ["--render-screenshot", "--render-size", "--render-overlay", "--render-game-info-file",
                   "--export-card", "--stats-fixture", "--repair-navigation-fixture",
                   "--restore-startup-test", "--gog-settings-test", "--linked-preference-test",
                   "--backup-editor-test", "--bulk-editor-test", "--saved-filter-test",
                   "--random-selection-test", "--launching-navigation-test", "--stale-selection-test",
                   "--game-mode-test", "--filter-back-test", "--artwork-editor-test",
                   "--manual-editor-test", "--couch-navigation-test", "--couch-navigation-contract",
                   "--couch-navigation-sweep", "--controller-navigation-test", "--startup-navigation-test",
                   "--startup-grid", "--startup-empty", "--startup-delayed", "--startup-controller"]
        self.assertEqual([option for option in harness if mentions(option)], [])

    def test_installed_documentation_names_no_private_machines(self) -> None:
        docs = self.prefix / DATADIR / "doc" / "omakade"
        for page in docs.rglob("*.md"):
            self.assertNotRegex(page.read_text(), r"(?i)\bdevbox\b", str(page))


if __name__ == "__main__":
    unittest.main()
