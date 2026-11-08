#!/usr/bin/env python3
"""Drive a headless VitruvianOS VM started by qemu-headless.sh.

  vmctl.py send TEXT [SECONDS]   type TEXT on the serial console, print
                                 the output seen during SECONDS (default 2)
  vmctl.py shot FILE.png         QMP screendump of the guest display
  vmctl.py hmp "COMMAND"         HMP monitor command (e.g. "sendkey ret",
                                 "info status", "quit")
  vmctl.py move X Y              put the pointer at screen pixel X,Y
  vmctl.py click X Y [BUTTON]    move there and click (left, right, middle)
  vmctl.py drag X1 Y1 X2 Y2 [KEY...]
                                 left-drag, holding QEMU qcodes (e.g.
                                 meta_l, the Option key for Stack & Tile)

The guest's QEMU mouse is a VMware VMMouse, an absolute device: HMP
mouse_move (relative) does not move it, so move/click send absolute QMP
input events scaled from $VOS_DBG_SCREEN (default 1280x800).

TEXT understands \\r (Enter), \\n, \\t, \\\\ and \\xHH (e.g. \\x1b for Esc,
\\x05 for Ctrl-E). Everything else is sent literally, so shell code such as
"$@" needs no extra escaping. ANSI escape sequences are stripped from the
printed output.

The sockets live in $VOS_DBG_DIR (default /tmp/vosdbg).
"""

import json
import os
import re
import socket
import sys
import time

DBG_DIR = os.environ.get("VOS_DBG_DIR", "/tmp/vosdbg")
SERIAL_SOCK = os.path.join(DBG_DIR, "ser.sock")
QMP_SOCK = os.path.join(DBG_DIR, "qmp.sock")

_ESCAPES = {"r": "\r", "n": "\n", "t": "\t", "\\": "\\"}


def unescape(text):
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            if nxt in _ESCAPES:
                out.append(_ESCAPES[nxt])
                i += 2
                continue
            if nxt == "x" and re.fullmatch(r"[0-9a-fA-F]{2}", text[i + 2:i + 4]):
                out.append(chr(int(text[i + 2:i + 4], 16)))
                i += 4
                continue
        out.append(c)
        i += 1
    return "".join(out).encode("latin-1")


def send(text, seconds):
    s = socket.socket(socket.AF_UNIX)
    s.connect(SERIAL_SOCK)
    s.settimeout(0.3)
    for byte in unescape(text):
        # The guest console drops characters when flooded.
        s.send(bytes([byte]))
        time.sleep(0.01)
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        try:
            out += s.recv(65536)
        except socket.timeout:
            pass
    s.close()
    print(re.sub(rb"\x1b\[[0-9;?=]*[A-Za-z]", b"", out).decode("utf-8",
        "replace"))


def qmp(command, **arguments):
    s = socket.socket(socket.AF_UNIX)
    s.connect(QMP_SOCK)
    f = s.makefile("rw")

    def reply():
        while True:
            msg = json.loads(f.readline())
            if "event" not in msg:
                return msg

    reply()  # greeting
    for cmd, args in (("qmp_capabilities", {}), (command, arguments)):
        req = {"execute": cmd}
        if args:
            req["arguments"] = args
        f.write(json.dumps(req) + "\n")
        f.flush()
        result = reply()
    s.close()
    return result


def _abs_events(x, y):
    w, h = (int(v) for v in os.environ.get("VOS_DBG_SCREEN",
        "1280x800").split("x"))
    return [
        {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (w - 1))}},
        {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (h - 1))}},
    ]


def move(x, y):
    return qmp("input-send-event", events=_abs_events(x, y))


def click(x, y, button="left"):
    move(x, y)
    time.sleep(0.1)
    for down in (True, False):
        qmp("input-send-event", events=[{"type": "btn",
            "data": {"button": button, "down": down}}])
        time.sleep(0.1)


def _key(qcode, down):
    qmp("input-send-event", events=[{"type": "key", "data": {"down": down,
        "key": {"type": "qcode", "data": qcode}}}])


def drag(x1, y1, x2, y2, keys=()):
    for k in keys:
        _key(k, True)
    move(x1, y1)
    time.sleep(1.0)  # app_server picks up input lazily under load
    qmp("input-send-event", events=[{"type": "btn",
        "data": {"button": "left", "down": True}}])
    steps = 12
    for i in range(1, steps + 1):
        time.sleep(0.08)
        move(x1 + (x2 - x1) * i // steps, y1 + (y2 - y1) * i // steps)
    time.sleep(0.5)
    qmp("input-send-event", events=[{"type": "btn",
        "data": {"button": "left", "down": False}}])
    for k in keys:
        _key(k, False)


def main(argv):
    if len(argv) >= 2 and argv[0] == "send":
        send(argv[1], float(argv[2]) if len(argv) > 2 else 2.0)
    elif len(argv) == 2 and argv[0] == "shot":
        result = qmp("screendump", filename=os.path.abspath(argv[1]),
            format="png")
        if "error" in result:
            sys.exit(result["error"].get("desc", "screendump failed"))
    elif len(argv) == 3 and argv[0] == "move":
        move(int(argv[1]), int(argv[2]))
    elif len(argv) in (3, 4) and argv[0] == "click":
        click(int(argv[1]), int(argv[2]), argv[3] if len(argv) == 4 else "left")
    elif len(argv) >= 5 and argv[0] == "drag":
        drag(*(int(v) for v in argv[1:5]), keys=argv[5:])
    elif len(argv) == 2 and argv[0] == "hmp":
        result = qmp("human-monitor-command", **{"command-line": argv[1]})
        print(result.get("return", result))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
