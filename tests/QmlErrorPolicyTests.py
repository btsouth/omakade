#!/usr/bin/env python3
"""Every test that should pass also fails on QML runtime errors in its output.

Arguments: <ctest> <build dir> <pattern>..., where the patterns are OMAKADE_TEST_QML_ERRORS.
"""
import json
import re
import subprocess
import sys
import unittest

CTEST, BUILD_DIR, *PATTERNS = sys.argv[1:]
del sys.argv[1:]

# Lines Qt prints for real QML mistakes, which the patterns have to catch.
SAMPLES = [
    "qrc:/qt/qml/Omakade/qml/Main.qml:2492: ReferenceError: button is not defined",
    "qrc:/qt/qml/Omakade/qml/Main.qml:12: TypeError: Cannot read property 'x' of null",
    "file:///src/qml/components/GameCard.qml:306:13: Unable to assign [undefined] to int",
    'qrc:/qt/qml/Omakade/qml/Main.qml:10:5: QML Item: Binding loop detected for property "width"',
]


def registered_tests():
    output = subprocess.run([CTEST, "--test-dir", BUILD_DIR, "--show-only=json-v1"],
                            check=True, capture_output=True, text=True).stdout
    for test in json.loads(output)["tests"]:
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        yield test["name"], properties


class QmlErrorPolicy(unittest.TestCase):
    def test_patterns_catch_qml_errors(self):
        for sample in SAMPLES:
            self.assertTrue(any(re.search(pattern, sample) for pattern in PATTERNS), sample)

    def test_every_passing_test_checks_for_qml_errors(self):
        checked = []
        missing = []
        for name, properties in registered_tests():
            # A match in a WILL_FAIL test would count as its expected failure, so the policy
            # leaves those alone.
            if properties.get("WILL_FAIL"):
                continue
            failures = properties.get("FAIL_REGULAR_EXPRESSION", [])
            if isinstance(failures, str):
                failures = [failures]
            (checked if set(PATTERNS) <= set(failures) else missing).append(name)
        self.assertEqual(missing, [])
        self.assertIn("omakade_render_smoke", checked)
        self.assertIn("omakade_status_pill_ui_tests", checked)


if __name__ == "__main__":
    unittest.main()
