#!/usr/bin/env python3
"""Classify a WLCS gtest XML report for the Koan Wayland server.

    docker/wlcs/classify.py generated.wlcs/wlcs.xml [--json OUT.json]

gtest only knows passed/failed/skipped. WLCS reports a protocol Koan does
not advertise as an exception, which gtest records as a failure. This
script maps every test to one status and a reason, so that nothing is
counted as a pass unless it passed:

  PASS             the test passed
  FAIL             the test failed for a reason not listed below
  SKIPPED          gtest skipped or disabled it
  NOT_IMPLEMENTED  it needs a protocol Koan does not advertise, or touch
                   input (Koan's seat has no touch capability)
  BLOCKED          the test itself breaks the protocol, so it cannot judge
                   a conforming compositor (each rule names the spec text)

Prints a per-status summary, the FAIL list grouped by suite, and the
NOT_IMPLEMENTED / BLOCKED reasons with counts. Exit status 1 if any FAIL.
"""

import collections
import json
import re
import sys
import xml.etree.ElementTree as ET

# Suites that only exercise touch input.
TOUCH_SUITES = re.compile(r"(^|/)(TouchTest|TouchInputSubsurfaces/)")

BLOCKED_RULES = [
    (
        re.compile(r"test_bad_buffer.cpp.*Expected protocol error not raised",
                   re.S),
        "test requires an error for a pool truncated after mmap; wayland.xml "
        "defines wl_shm.invalid_fd only for a failed mmap. wlroots "
        "zero-fills instead (compositor policy, not conformance)",
    ),
    (
        re.compile(r'protocol error: 3 on interface xdg_surface'),
        "test attaches a buffer before the first xdg_surface.configure "
        "(xdg-shell.xml: 'attempts by a client to attach or manipulate a "
        "buffer prior to the first xdg_surface.configure call must also be "
        "treated as errors'); wlroots raises unconfigured_buffer",
    ),
]

UNSUPPORTED = re.compile(
    r'Extension: (\S+) version >= \d+ not supported by compositor under test')


def uses_touch(testcase):
    """Touch-only suites, touch-named tests, and parameterised tests whose
    input method is touch. gtest prints parameters as hex bytes, so the
    method name is found by decoding them."""
    if TOUCH_SUITES.search(testcase.get("classname")):
        return True
    if re.search(r"(^|_)touch(_|$)", testcase.get("name")):
        return True
    param = testcase.get("value_param") or ""
    raw = bytes.fromhex("".join(re.findall(r"\b[0-9A-F]{2}\b", param)))
    return b"touch\0" in raw or param.endswith("touch")


# Not about the compositor: WLCS checking its own machinery.
WLCS_SELF_TESTS = {
    "SelfTest.xfail_failure_is_noted":
        "WLCS self-test of its expected-failure marker (fails by design)",
}


def classify(testcase):
    name = testcase.get("classname") + "." + testcase.get("name")
    failure = testcase.find("failure")
    if testcase.get("status") == "notrun" or testcase.find("skipped") is not None:
        return name, "SKIPPED", "skipped or disabled by gtest"
    if name in WLCS_SELF_TESTS:
        return name, "SKIPPED", WLCS_SELF_TESTS[name]
    message = failure.get("message") or "" if failure is not None else ""
    m = UNSUPPORTED.search(message)
    if m:
        return name, "NOT_IMPLEMENTED", "protocol not advertised: " + m.group(1)
    # Touch is checked before PASS: with no touch device, a test that
    # expects *no* input passes trivially, which proves nothing.
    if uses_touch(testcase):
        return name, "NOT_IMPLEMENTED", "touch input (seat has no touch)"
    if failure is None:
        return name, "PASS", ""
    for rule, reason in BLOCKED_RULES:
        if rule.search(message):
            return name, "BLOCKED", reason
    first = message.strip().splitlines()
    return name, "FAIL", " | ".join(line.strip() for line in first[:3])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = ET.parse(sys.argv[1]).getroot()
    results = [classify(tc) for tc in root.iter("testcase")]

    counts = collections.Counter(status for _, status, _ in results)
    print("WLCS|total=%d|%s" % (len(results), "|".join(
        "%s=%d" % (s, counts.get(s, 0))
        for s in ("PASS", "FAIL", "SKIPPED", "NOT_IMPLEMENTED", "BLOCKED"))))

    for status in ("NOT_IMPLEMENTED", "BLOCKED"):
        reasons = collections.Counter(r for _, s, r in results if s == status)
        for reason, n in reasons.most_common():
            print("%s|%d|%s" % (status, n, reason))

    fails = collections.defaultdict(list)
    for name, status, reason in results:
        if status == "FAIL":
            fails[name.split(".")[0]].append((name, reason))
    for suite in sorted(fails):
        print("FAIL-SUITE|%s|%d" % (suite, len(fails[suite])))
        for name, reason in fails[suite]:
            print("  FAIL|%s|%s" % (name, reason[:200]))

    if "--json" in sys.argv:
        out = sys.argv[sys.argv.index("--json") + 1]
        with open(out, "w") as f:
            json.dump([{"test": n, "status": s, "reason": r}
                       for n, s, r in results], f, indent=1)
    return 1 if counts.get("FAIL") else 0


if __name__ == "__main__":
    sys.exit(main())
