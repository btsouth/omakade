#!/usr/bin/env python3
"""Run named Qt Test functions and fail unless every one of them passes.

ctest only sees a test binary's exit code, so a QSKIP inside a function still counts
as a pass there. CI uses this for the cases that must really run: the permission
cases that skip as root and the clip trim that skips without ffmpeg.
"""

import argparse
import re
import subprocess
import sys

RESULT = re.compile(r"^(PASS|FAIL!|SKIP|XFAIL|XPASS|BPASS|BFAIL|BXPASS|BXFAIL)\s*: \S+::(\w+)\((.*?)\)")


def check(output: str, cases: list[str]) -> list[str]:
    """Returns the problems found in Qt Test's txt output for the given functions."""
    results: dict[str, list[tuple[str, str]]] = {case: [] for case in cases}
    for line in output.splitlines():
        match = RESULT.match(line)
        if match and match.group(2) in results:
            results[match.group(2)].append((match.group(1), match.group(3)))
    problems = []
    for case, outcomes in results.items():
        if not outcomes:
            problems.append(f"{case} did not run")
        for outcome, row in outcomes:
            if outcome != "PASS":
                problems.append(f"{case}({row}) reported {outcome}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary")
    parser.add_argument("cases", nargs="+")
    parser.add_argument("--timeout", type=int, default=120)
    arguments = parser.parse_args()
    try:
        result = subprocess.run([arguments.binary, *arguments.cases, "-o", "-,txt"],
                                capture_output=True, text=True, timeout=arguments.timeout)
    except subprocess.TimeoutExpired:
        print(f"{arguments.binary} timed out after {arguments.timeout} seconds", file=sys.stderr)
        return 1
    sys.stdout.write(result.stdout)
    sys.stdout.flush()
    sys.stderr.write(result.stderr)
    problems = check(result.stdout, arguments.cases)
    if result.returncode != 0:
        problems.append(f"{arguments.binary} exited with {result.returncode}")
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
