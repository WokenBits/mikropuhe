#!/usr/bin/env python3
"""Check the full-screen interface of the port against the original's.

Both are driven with the same keys. The original runs in the emulator (tools/ui_explore.py) until it
waits for a key that is not there; the port runs with --ui-script. Compared at that point: all 2000
character cells of the screen, their colour attributes, the cursor position, how the run ended, and
every word that was handed to the speech core on the way.

    tests/check_ui.py [--random N] [--seed S] [name-substring ...]
    tests/check_ui.py --keys "F2 DOWN + ENTER text:moi ENTER"
"""
import json
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import ui_explore  # noqa: E402

FILES = os.path.join(ROOT, "original", "files")
PUHU = os.path.join(HERE, "..", "build", "puhu")

T = "text:"
SCENARIOS = {
    "start": "",
    "help": "F1",
    "speak a line": T + "hyvää ENTER".replace(" ", " "),
    "type and edit": [T + "kaksi sanaa ja kolmas", "LEFT", "LEFT", "LEFT", "BS", "BS", T + "X", "HOME", "DEL",
                      "END", T + "!", "CTRL-LEFT", "CTRL-LEFT", T + "uusi ", "CTRL-RIGHT", "DEL"],
    "long line scrolls": [T + "tämä on niin pitkä lause että se ei mahdu kenttään kerralla vaan vierii",
                          "HOME", "RIGHT", "RIGHT", "END", "CTRL-LEFT", "CTRL-LEFT", "CTRL-LEFT", "BS"],
    "scroll back and forth": [T + "a" * 50, T + "b" * 20] + ["LEFT"] * 30 + ["RIGHT"] * 12 + ["HOME", "END"],
    "clear line": [T + "pois tämä", "CLEAR", T + "uusi"],
    "history": [T + "yksi", "ENTER", T + "kaksi", "ENTER", T + "kolme", "ENTER", "UP", "UP", "DOWN", "UP", "UP",
                "UP", "UP"],
    "history down clears": [T + "yksi", "ENTER", T + "kaksi", "ENTER", "UP", "DOWN", "DOWN", T + "x", "DOWN"],
    "history overflow": sum([[T + "rivi %d" % i, "ENTER"] for i in range(12)], []) + ["UP"] * 11,
    "empty enter": ["ENTER", "ENTER", T + " ", "ENTER"],
    "settings": "F2",
    "settings walk": ["F2"] + ["DOWN"] * 10 + ["UP"] * 10,
    "settings top and bottom": ["F2", "UP"] + ["DOWN"] * 9 + ["DOWN"],
    "settings numbers": ["F2", "+", "+", "-", "DOWN", "+", "+", "+", "+", "+", "DOWN", "-", "-", "DOWN"] + ["-"] * 9,
    "settings flags": ["F2"] + ["DOWN"] * 5 + ["-", "+", "DOWN", "-", "DOWN", "+", "DOWN", "+", "-", "DOWN", "+",
                                             "-", "+"],
    "settings space and help": ["F2", "SPACE", "DOWN", "SPACE", "F1"],
    "settings leave and return": ["F2", "DOWN", "DOWN", "+", "ENTER", T + "nyt", "F2", "-", "ENTER", T + " taas"],
    "settings affect speech": ["F2", "+", "+", "+", "+", "ENTER", T + "nopeammin", "ENTER"],
    "voice change": ["F2", "DOWN", "DOWN", "DOWN", "DOWN", "+", "+", "+", "-", "ENTER", T + "hei", "ENTER"],
    "save fails": ["F2", "F2"],
    "save fails then on": ["F2", "DOWN", "F2", "SPACE", "ENTER", "DOWN", "+"],
    "quit": "ESC",
    "quit from settings": ["F2", "DOWN", "ESC"],
    "quit after text": [T + "hei", "ENTER", T + "jää", "ESC"],
    "other keys ignored": ["TAB", "PGUP", "PGDN", "INS", T + "a", "TAB", "F1"],
}
MAIN_KEYS = [T + "a", T + "ä", T + "sana", T + " ", T + "kaksi sanaa", T + "12", T + ".", T + ",", T + "pitkähkö sana tässä",
             "LEFT", "LEFT", "RIGHT", "HOME", "END", "BS", "BS", "DEL", "CTRL-LEFT", "CTRL-RIGHT", "CLEAR", "ENTER",
             "ENTER", "UP", "UP", "DOWN", "F2", "TAB"]
SET_KEYS = ["UP", "DOWN", "DOWN", "DOWN", "+", "+", "-", "-", "SPACE", "ENTER", "LEFT", T + "x"]


def random_keys(rng):
    out, in_settings = [], False
    for _ in range(rng.randint(8, 40)):
        k = rng.choice(SET_KEYS if in_settings else MAIN_KEYS)
        out.append(k)
        if k == "F2":
            in_settings = True
        elif k == "ENTER":
            in_settings = False
    return out


def port_codes(spec):
    return [(ax & 0xFF) or 0x100 + (ax >> 8) for ax in ui_explore.keycodes(spec)]


def said_from_trace(path):
    said = []
    for ln in open(path).read().splitlines():
        if ln.startswith("W "):
            said.append(bytes.fromhex(ln[2:]).decode("cp437"))
        elif ln == "E":
            said.append("|")
    return said


def compare(name, spec):
    dos, end = ui_explore.run(spec)
    cells = dos.screen_cells()
    want = {"end": "exit" if dos.exit_code is not None else "waits",
            "cursor": [dos.cursor >> 8, dos.cursor & 0xFF],
            "chars": [bytes(ch for ch, _ in row).hex() for row in cells],
            "attrs": [bytes(at for _, at in row).hex() for row in cells]}
    if dos.exit_code is None and "waits for a key" not in end:
        return "the original stopped oddly: %s" % end
    want_said = []
    for c in dos.calls:
        if c[0] == "W":
            want_said.append(bytes.fromhex(c[1]).decode("cp437"))
        elif c[0] == "E":
            want_said.append("|")
    with tempfile.TemporaryDirectory() as tmp:
        trace = os.path.join(tmp, "trace")
        out = subprocess.run([PUHU, "--voices-dir", FILES, "--trace", trace, "--ui-script",
                              " ".join(str(k) for k in port_codes(spec))], capture_output=True)
        if out.returncode != 0:
            return "puhu exited %d: %s" % (out.returncode, out.stderr.decode()[:200])
        got = json.loads(out.stdout)
        got_said = said_from_trace(trace)
    problems = []
    if got["end"] != want["end"]:
        problems.append("ended %r, original %r" % (got["end"], want["end"]))
    for what in ("chars", "attrs"):
        for r in range(25):
            if got[what][r] != want[what][r]:
                g, w = bytes.fromhex(got[what][r]), bytes.fromhex(want[what][r])
                c = next(i for i in range(80) if g[i] != w[i])
                if what == "chars":
                    problems.append("row %d col %d: text %r, original %r" % (
                        r, c, g[c:c + 24].decode("cp437"), w[c:c + 24].decode("cp437")))
                else:
                    problems.append("row %d col %d: attribute %02x, original %02x" % (r, c, g[c], w[c]))
                break
    if got["cursor"] != want["cursor"]:
        problems.append("cursor %s, original %s" % (got["cursor"], want["cursor"]))
    if got_said != want_said:
        i = next((i for i, (a, b) in enumerate(zip(got_said, want_said)) if a != b), min(len(got_said), len(want_said)))
        problems.append("speech differs at word %d: %r, original %r" % (
            i, " ".join(got_said[i:i + 6]), " ".join(want_said[i:i + 6])))
    return "; ".join(problems[:4]) if problems else None


def main():
    argv = sys.argv[1:]
    todo = []
    if "--keys" in argv:
        todo = [("keys", argv[argv.index("--keys") + 1].split())]
    else:
        names = [a for a in argv if not a.startswith("--") and (argv.index(a) == 0 or argv[argv.index(a) - 1] not in ("--random", "--seed"))]
        for name, spec in SCENARIOS.items():
            if names and not any(n in name for n in names):
                continue
            todo.append((name, spec.split() if isinstance(spec, str) else spec))
        if "--random" in argv:
            rng = random.Random(int(argv[argv.index("--seed") + 1]) if "--seed" in argv else 1990)
            for i in range(int(argv[argv.index("--random") + 1])):
                todo.append(("random %d" % i, random_keys(rng)))
    bad = 0
    for name, spec in todo:
        spec = [k for k in spec if k]
        result = compare(name, spec)
        if result:
            bad += 1
            print("FAIL %s: %s\n     keys: %s" % (name, result, " ".join(spec)))
    print("%d key sequences, %d differ from the original" % (len(todo), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
