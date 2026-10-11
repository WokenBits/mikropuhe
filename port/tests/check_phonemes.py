#!/usr/bin/env python3
"""Check the C core's word stage (mp_speak_phonemes and everything below it) against the original.

For every reference case the original is run in the harness with stage tracing; the traced calls
are replayed through build/replay; the two sample streams must be identical, word by word.

    tests/check_phonemes.py [case-id-substring ...]
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from make_reference import cases  # noqa: E402
from puhu_emu import Dos  # noqa: E402

FILES = os.path.join(ROOT, "original", "files")
VOICE = {"jan": "JANNE.SMP", "kri": "KRISTIAN.SMP", "sop": "SOPHIA.SMP"}


def norm(events):
    """Merge adjacent sample runs: [("s", bytes) | ("p", n)]."""
    out = []
    for k, v in events:
        if k == "s" and out and out[-1][0] == "s":
            out[-1] = ("s", out[-1][1] + v)
        elif k == "s" and not v:
            continue
        else:
            out.append((k, v))
    return out


def main():
    want = sys.argv[1:]
    bad = total = 0
    for cid, tail in cases():
        if want and not any(w in cid for w in want):
            continue
        total += 1
        dos = Dos(os.path.join(FILES, "PUHU.EXE"), FILES, (" " + tail).encode("cp437"))
        dos.enable_trace()
        dos.run()
        lines = []
        for c in dos.calls:
            if c[0] in "SGPE":
                lines.append(" ".join(str(x) for x in c))
            elif c[0] in "FW":
                lines.append(c[0] + " " + c[1])
        out = subprocess.run([os.path.join(HERE, "..", "build", "replay"), os.path.join(FILES, VOICE[cid[:3]])],
                             input="\n".join(lines) + "\n", capture_output=True, text=True)
        if out.returncode != 0:
            print("FAIL %s: replay exited %d: %s" % (cid, out.returncode, out.stderr.strip()[:200]))
            bad += 1
            continue
        got, got_r = [], []
        for ln in out.stdout.splitlines():
            f = ln.split(" ")
            if f[0] == "s":
                got.append(("s", bytes.fromhex(f[1])))
            elif f[0] == "p":
                got.append(("p", int(f[1])))
            elif f[0] == "R":
                got_r.append((int(f[1]), f[2], int(f[3])))
        exp_r = [(c[1], c[2], c[3]) for c in dos.calls if c[0] == "R"]
        words = [bytes.fromhex(c[1]).decode("cp437") for c in dos.calls if c[0] == "W"]
        if norm(got) == norm(dos.events) and got_r == exp_r:
            continue
        bad += 1
        first = next((i for i, (a, b) in enumerate(zip(got_r, exp_r)) if a != b), None)
        if first is None:
            print("FAIL %s: words agree (%d), streams differ outside them" % (cid, len(exp_r)))
        else:
            print("FAIL %s: word %d %r: got (ret, left, pos) %s, expected %s" % (
                cid, first, words[first], got_r[first], exp_r[first]))
    print("%d cases, %d failed" % (total, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
