#!/usr/bin/env python3
"""Tests for the release and CI helper scripts in tools/ci, using stand-in gh and objdump."""

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).parent
PREVIOUS_RELEASE = HERE / "previous-release.py"
REQUIRE_QT_CASES = HERE / "require-qt-cases.py"
PACKAGE_ABI = HERE / "package-abi.sh"

SPEC = importlib.util.spec_from_file_location("require_qt_cases", REQUIRE_QT_CASES)
assert SPEC is not None and SPEC.loader is not None
REQUIRE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REQUIRE)

FAKE_GH = """\
#!/usr/bin/env python3
import json, os, sys
arguments = sys.argv[1:]
with open(os.environ["FAKE_GH_LOG"], "a") as log:
    log.write(json.dumps(arguments) + "\\n")
scenario = json.load(open(os.environ["FAKE_GH_SCENARIO"]))
key = arguments[1] if arguments[1] == "list" else "view " + arguments[2]
reply = scenario.get(key, {"exit": 1, "stderr": "unexpected call: " + " ".join(arguments)})
sys.stdout.write(reply.get("stdout", ""))
sys.stderr.write(reply.get("stderr", ""))
sys.exit(reply.get("exit", 0))
"""


def ok(value) -> dict:
    return {"exit": 0, "stdout": json.dumps(value)}


def assets(*names: str) -> dict:
    return ok({"assets": [{"name": name} for name in names]})


class PreviousReleaseTests(unittest.TestCase):
    def run_lookup(self, scenario: dict, current_tag: str = "v1.19.0", arch: str = "x86_64"):
        with tempfile.TemporaryDirectory() as directory:
            bin_dir = Path(directory) / "bin"
            bin_dir.mkdir()
            gh = bin_dir / "gh"
            gh.write_text(FAKE_GH)
            gh.chmod(0o755)
            scenario_file = Path(directory) / "scenario.json"
            scenario_file.write_text(json.dumps(scenario))
            log_file = Path(directory) / "calls"
            log_file.touch()
            env = dict(os.environ, PATH=f"{bin_dir}{os.pathsep}{os.environ['PATH']}",
                       FAKE_GH_SCENARIO=str(scenario_file), FAKE_GH_LOG=str(log_file))
            result = subprocess.run(
                [sys.executable, str(PREVIOUS_RELEASE), "--repo", "btsouth/omakade",
                 "--arch", arch, "--current-tag", current_tag],
                capture_output=True, text=True, env=env)
            calls = [json.loads(line) for line in log_file.read_text().splitlines()]
        return result, calls

    def test_finds_the_previous_package_for_the_architecture(self) -> None:
        result, calls = self.run_lookup({
            "list": ok([{"tagName": "v1.18.0"}, {"tagName": "v1.17.0"}]),
            "view v1.18.0": assets("omakade-1.18.0.tar.gz", "omakade-1.18.0-1-aarch64.pkg.tar.zst",
                                   "omakade-1.18.0-1-x86_64.pkg.tar.zst", "SHA256SUMS"),
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "v1.18.0\tomakade-1.18.0-1-x86_64.pkg.tar.zst\n")
        self.assertIn("Upgrade baseline: omakade-1.18.0-1-x86_64.pkg.tar.zst from v1.18.0.",
                      result.stderr)
        self.assertIn("--exclude-drafts", calls[0])
        self.assertIn("--exclude-pre-releases", calls[0])

    def test_no_releases_is_an_allowed_miss(self) -> None:
        result, _ = self.run_lookup({"list": ok([])})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("no earlier published release", result.stderr)

    def test_the_release_being_built_is_not_its_own_baseline(self) -> None:
        result, calls = self.run_lookup({
            "list": ok([{"tagName": "v1.19.0"}, {"tagName": "v1.18.0"}]),
            "view v1.18.0": assets("omakade-1.18.0-1-x86_64.pkg.tar.zst"),
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "v1.18.0\tomakade-1.18.0-1-x86_64.pkg.tar.zst\n")
        self.assertEqual(calls[1][:3], ["release", "view", "v1.18.0"])

        result, _ = self.run_lookup({"list": ok([{"tagName": "v1.19.0"}])})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_a_release_without_this_architecture_is_an_allowed_miss(self) -> None:
        result, _ = self.run_lookup({
            "list": ok([{"tagName": "v1.18.0"}]),
            "view v1.18.0": assets("omakade-1.18.0-1-x86_64.pkg.tar.zst"),
        }, arch="aarch64")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("v1.18.0 has no aarch64 package", result.stderr)

    def test_a_failed_release_list_fails(self) -> None:
        result, _ = self.run_lookup({"list": {
            "exit": 1, "stderr": "HTTP 401: Bad credentials (https://api.github.com/graphql)\n"}})
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("Bad credentials", result.stderr)

    def test_a_failed_release_view_fails(self) -> None:
        result, _ = self.run_lookup({
            "list": ok([{"tagName": "v1.18.0"}]),
            "view v1.18.0": {"exit": 1, "stderr": "HTTP 502: Bad Gateway\n"},
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("Bad Gateway", result.stderr)

    def test_unreadable_replies_fail(self) -> None:
        for scenario in (
            {"list": {"exit": 0, "stdout": "<html>rate limited</html>"}},
            {"list": ok({"message": "not a list"})},
            {"list": ok([{"tagName": "v1.18.0"}]), "view v1.18.0": ok({"assets": None})},
        ):
            with self.subTest(scenario=scenario):
                result, _ = self.run_lookup(scenario)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")

    def test_two_packages_for_one_architecture_fail(self) -> None:
        result, _ = self.run_lookup({
            "list": ok([{"tagName": "v1.18.0"}]),
            "view v1.18.0": assets("omakade-1.18.0-1-x86_64.pkg.tar.zst",
                                   "omakade-1.18.0-2-x86_64.pkg.tar.zst"),
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("more than one x86_64 package", result.stderr)

    def test_missing_gh_fails(self) -> None:
        env = dict(os.environ, PATH="/nonexistent")
        result = subprocess.run(
            [sys.executable, str(PREVIOUS_RELEASE), "--repo", "r", "--arch", "x86_64"],
            capture_output=True, text=True, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not run gh", result.stderr)


QT_OUTPUT = textwrap.dedent("""\
    ********* Start testing of SaveSetTests *********
    Config: Using QtTest library 6.12.0
    PASS   : SaveSetTests::initTestCase()
    PASS   : SaveSetTests::unreadableFoldersRefuseSnapshotAndRestore(save-root)
    {second}
    PASS   : SaveSetTests::cleanupTestCase()
    Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 3ms
    ********* Finished testing of SaveSetTests *********
    """)


class RequireQtCasesTests(unittest.TestCase):
    CASE = "unreadableFoldersRefuseSnapshotAndRestore"

    def test_every_row_passing_is_accepted(self) -> None:
        output = QT_OUTPUT.format(
            second=f"PASS   : SaveSetTests::{self.CASE}(nested-save-folder)")
        self.assertEqual(REQUIRE.check(output, [self.CASE]), [])

    def test_a_skipped_row_is_rejected(self) -> None:
        output = QT_OUTPUT.format(second=(
            f"SKIP   : SaveSetTests::{self.CASE}(nested-save-folder) "
            "This user bypasses directory read permissions\n"
            "   Loc: [SaveSetTests.cpp(108)]"))
        self.assertEqual(REQUIRE.check(output, [self.CASE]),
                         [f"{self.CASE}(nested-save-folder) reported SKIP"])

    def test_a_failed_row_is_rejected(self) -> None:
        output = QT_OUTPUT.format(
            second=f"FAIL!  : SaveSetTests::{self.CASE}(nested-save-folder) 'x' returned FALSE.")
        self.assertEqual(REQUIRE.check(output, [self.CASE]),
                         [f"{self.CASE}(nested-save-folder) reported FAIL!"])

    def test_a_case_that_never_ran_is_rejected(self) -> None:
        output = QT_OUTPUT.format(second="")
        self.assertEqual(REQUIRE.check(output, [self.CASE, "missingCase"]),
                         ["missingCase did not run"])

    def test_runs_only_the_named_cases_and_uses_the_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "fake_tests"
            arguments_file = Path(directory) / "arguments"
            binary.write_text(textwrap.dedent(f"""\
                #!/bin/sh
                printf '%s\\n' "$@" > '{arguments_file}'
                echo 'PASS   : FakeTests::trims()'
                exit "${{FAKE_EXIT:-0}}"
                """))
            binary.chmod(0o755)
            run = lambda exit_code: subprocess.run(
                [sys.executable, str(REQUIRE_QT_CASES), str(binary), "trims"],
                capture_output=True, text=True, env=dict(os.environ, FAKE_EXIT=exit_code))
            result = run("0")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(arguments_file.read_text().split(), ["trims", "-o", "-,txt"])
            result = run("1")
            self.assertEqual(result.returncode, 1)
            self.assertIn("exited with 1", result.stderr)


FAKE_OBJDUMP = """\
#!/bin/sh
case "$2" in
  *broken*) echo "objdump: $2: file format not recognized" >&2; exit 1 ;;
  *omakade) printf '%s\\n' \\
    '0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.34) __libc_start_main' \\
    '0000000000000000      DF *UND*  0000000000000000 (Qt_6.9) _ZN7QString8fromUtf8E' \\
    '0000000000000000      DF *UND*  0000000000000000 (Qt_6_PRIVATE_API) _ZN15QGuiApplication' \\
    '0000000000000000      DF *UND*  0000000000000000  GLIBC_2.2.5 memcpy' ;;
  *helper) printf '%s\\n' \\
    '0000000000000000      DF *UND*  0000000000000000 (GLIBC_2.41) pidfd_spawn' \\
    '0000000000000000      DF *UND*  0000000000000000 (Qt_6.10) _ZN7QString3argE' ;;
  *) ;;
esac
"""


class PackageAbiTests(unittest.TestCase):
    def run_abi(self, files: dict[str, bytes]):
        with tempfile.TemporaryDirectory() as directory:
            bin_dir = Path(directory) / "bin"
            bin_dir.mkdir()
            objdump = bin_dir / "objdump"
            objdump.write_text(FAKE_OBJDUMP)
            objdump.chmod(0o755)
            root = Path(directory) / "pkg"
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            env = dict(os.environ, PATH=f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
            return subprocess.run(["bash", str(PACKAGE_ABI), str(root)],
                                  capture_output=True, text=True, env=env)

    def test_reports_the_newest_versions_any_elf_file_needs(self) -> None:
        result = self.run_abi({
            "usr/bin/omakade": b"\x7fELF binary",
            "usr/lib/omakade/helper": b"\x7fELF binary",
            "usr/share/doc/README": b"not an ELF file, so objdump never sees it",
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "glibc=2.41\nqt=6.10\nelf=2\n")

    def test_an_unreadable_elf_file_fails(self) -> None:
        result = self.run_abi({
            "usr/bin/omakade": b"\x7fELF binary",
            "usr/bin/broken": b"\x7fELF truncated",
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("could not read", result.stderr)

    def test_a_package_without_symbol_versions_fails(self) -> None:
        for files in ({"usr/share/doc/README": b"text"}, {"usr/bin/other": b"\x7fELF binary"}):
            with self.subTest(files=list(files)):
                result = self.run_abi(files)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
