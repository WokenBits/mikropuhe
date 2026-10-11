#!/usr/bin/env python3
"""Drive the original's full-screen interface with scripted keys and show what it does.

    tools/ui_explore.py [--attr] [--raw] KEY ...

Keys: F1 F2 ESC ENTER UP DOWN LEFT RIGHT HOME END BS DEL CTRL-LEFT CTRL-RIGHT CLEAR TAB + - SPACE, or text:word (typed letter by
letter). The original runs in the emulator until it waits for a key that is not there. Printed: the
80x25 screen at that point, the cursor, what was spoken, and how the run ended.
--attr adds the colour attributes per row; --raw prints nothing but JSON (for tests).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from puhu_emu import Dos, Exit  # noqa: E402

NAMED = {"F1": 0x3B00, "F2": 0x3C00, "ESC": 0x011B, "ENTER": 0x1C0D, "UP": 0x4800, "DOWN": 0x5000,
         "LEFT": 0x4B00, "RIGHT": 0x4D00, "HOME": 0x4700, "END": 0x4F00, "BS": 0x0E08, "DEL": 0x5300,
         "TAB": 0x0F09, "+": 0x4E2B, "-": 0x4A2D, "SPACE": 0x3920, "PGUP": 0x4900, "PGDN": 0x5100,
         "INS": 0x5200, "CTRL-LEFT": 0x7300, "CTRL-RIGHT": 0x7400, "CLEAR": 0x0E7F}


LOW = {0: " ", 0x10: "►", 0x11: "◄", 0x18: "↑", 0x19: "↓", 0x1A: "→", 0x1B: "←"}  # CP437 glyphs below 0x20


def keycodes(spec):
    out = []
    for k in spec:
        if k.upper() in NAMED:
            out.append(NAMED[k.upper()])
        elif k.startswith("text:"):
            out += [c for c in k[5:].encode("cp437")]  # scan code 0: the program only looks at the character
        else:
            raise SystemExit("unknown key %r" % k)
    return out


def run(spec, files=None):
    here = os.path.dirname(os.path.abspath(__file__))
    files = files or os.path.join(here, "..", "original", "files")
    dos = Dos(os.path.join(files, "PUHU.EXE"), files, b"", keys=keycodes(spec))
    dos.typed_late = True
    dos.enable_trace()
    end = "exit"
    try:
        dos.run(max_insns=600_000_000)
    except Exit as e:
        end = str(e)
    return dos, end


def main():
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    spec = [a for a in sys.argv[1:] if not a.startswith("--")]
    dos, end = run(spec)
    cells = dos.screen_cells()
    said = []
    for c in dos.calls:
        if c[0] == "W":
            said.append(bytes.fromhex(c[1]).decode("cp437"))
        elif c[0] == "E":
            said.append("|")
    if "--raw" in flags:
        print(json.dumps({"end": end, "exit": dos.exit_code, "cursor": [dos.cursor >> 8, dos.cursor & 0xFF],
                          "chars": [bytes(ch for ch, _ in row).hex() for row in cells],
                          "attrs": [bytes(at for _, at in row).hex() for row in cells], "said": said}))
        return
    print("keys: %s" % " ".join(spec))
    print("+" + "-" * 80 + "+")
    for row in cells:
        text = "".join(chr(ch).encode("latin1").decode("cp437") if ch >= 0x20 else LOW.get(ch, "·") for ch, _ in row)
        print("|" + text + "|")
    print("+" + "-" * 80 + "+")
    if "--attr" in flags:
        for r, row in enumerate(cells):
            if any(at not in (0, 7, 0x0F) for _, at in row):
                print("%2d bg %s" % (r, "".join("%x" % (at >> 4) for _, at in row)))
                print("   fg %s" % "".join("%x" % (at & 15) for _, at in row))
    print("cursor row %d col %d | ended: %s | exit code %s" % (dos.cursor >> 8, dos.cursor & 0xFF, end, dos.exit_code))
    print("said: " + " ".join(said))


if __name__ == "__main__":
    main()
