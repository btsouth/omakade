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
    "qrc:/qt/qml/Omakade/qml/Main.qml:40: RangeError: Maximum call stack size exceeded.",
    "QQuickItem::polish() loop",
    "qrc:/qt/qml/Omakade/qml/Main.qml:88: TypeError: Cannot assign to read-only property \"count\"",
    "qrc:/qt/qml/Omakade/qml/Main.qml:7:1: Cannot assign to non-existent property \"foo\"",
    "qrc:/qt/qml/Omakade/qml/Main.qml:2666:5: StatCard is not a type",
    'qrc:/qt/qml/Omakade/qml/Main.qml:3:1: module "QtCharts" is not installed',
    "qrc:/qt/qml/Omakade/qml/Main.qml:51: Error: Could not convert argument 0 at",
    'qrc:/qt/qml/Omakade/qml/Main.qml:9: Error: Invalid write to global property "total"',
]

# App messages that are not QML errors and must not fail a test.
CLEAN = [
    "Heroic is not installed.",
    "Card export: interactive, the app keeps running after a save",
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

    def test_every_pattern_matches_a_sample(self):
        for pattern in PATTERNS:
            self.assertTrue(any(re.search(pattern, sample) for sample in SAMPLES), pattern)

    def test_patterns_leave_app_messages_alone(self):
        for line in CLEAN:
            self.assertFalse(any(re.search(pattern, line) for pattern in PATTERNS), line)

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
        self.assertIn("omakade_card_export_error", checked)


if __name__ == "__main__":
    unittest.main()
