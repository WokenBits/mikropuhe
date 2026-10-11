#!/usr/bin/env python3
"""End-to-end check of the native port against the original.

For every case the original runs in the harness with stage tracing, and build/puhu runs on the
same command tail. Compared: (1) the sequence of stage calls (speak start, each word handed to the
word stage with the globals it depends on, pauses, speak end) and (2) the sample stream.

The port is started with the stack bytes the original had at its first speak call
(--stack-image), because the original reads uninitialised stack in one place (see synth.c).
With --no-stack the port starts from zeros instead, which shows how much that matters.

    tests/check_e2e.py [--no-stack] [--extra] [case-id-substring ...]
    tests/check_e2e.py --tail "/KRI/NOP5 jokin teksti"
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from make_reference import cases  # noqa: E402
from puhu_emu import Dos, Exit  # noqa: E402

FILES = os.path.join(ROOT, "original", "files")
PUHU = os.path.join(HERE, "..", "build", "puhu")

# inputs beyond the reference set: numbers, specials, inline options, odd text
EXTRA = [
    "/JAN 1 2 3 9 10 11 12 13 19 20 21 30 99 100 101 110 111 119 120 199 200 999",
    "/JAN 1000 1001 1010 1100 1999 2000 2001 10000 12345 100000 999999 1000000 2000000",
    "/JAN 1000000000 2000000000 1000000000000 123456789012345 1234567890123456",
    "/JAN 3,14 0,5 12,50 1.5 1.2.1990 3. 4.- 5- -6 7,- 12,- 100,-",
    "/JAN puhelin 123 456 ja 0400 123",
    "/JAN 5 % 10 $ 20 £ 30° 12:30 (3) 2+2=4 1/2",
    "/KRI wc x-akseli quiz zeppeliini C-kasetti Åke ÄITI Öljy über",
    "/SOP esim. ns. oy ky ab jne. yms. klo 12 mm. n.s.",
    "/JAN (suluissa oleva teksti) ja _alaviiva_ ja # risuaita & et-merkki",
    "/JAN hei! Mitä? Niin... joo; ei: kyllä, ehkä.",
    "/JAN kaksi /NOP8 sanaa /KOR8/KRV8 korkealta /VOI4 hiljaa /KNP0 ja /ETP0 loppu",
    "/JAN minä /KRI sinä /SOP hän /JAN me",
    "/JAN lyhyt. Toinen lause tulee tässä! Kolmas? Neljäs: viides.",
    "/JAN ng kangas kengät lanka anna alla amma arra assa appi akka atto",
    "/JAN jäljellä ylläpitää miljoona nainen mies lumi vanha jono",
    "/SOP b d f g h j k l m n p r s t v aa ee ii oo uu yy ää öö",
    "/KRI moi-moi tosi-iso e-mail a_b a#b a&b",
    "/JAN " + "pitkäsana" * 14,
    "/JAN a",
    "/JAN !",
    "/JAN 0",
    "/JAN 007 00 0,0",
]


def norm(events):
    out = []
    for k, v in events:
        if k == "s" and not v:
            continue
        if k == "s" and out and out[-1][0] == "s":
            out[-1] = ("s", out[-1][1] + v)
        else:
            out.append((k, v))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    no_stack = "--no-stack" in sys.argv
    todo = list(cases())
    if "--extra" in sys.argv:
        todo += [("extra-%02d" % i, t) for i, t in enumerate(EXTRA)]
    if "--tail" in sys.argv:  # one ad-hoc input: --tail "/KRI/NOP5 teksti"
        todo = [("tail", sys.argv[sys.argv.index("--tail") + 1])]
        args = []
    bad = total = skipped = 0
    for cid, tail in todo:
        if args and not any(a in cid for a in args):
            continue
        total += 1
        dos = Dos(os.path.join(FILES, "PUHU.EXE"), FILES, (" " + tail).encode("cp437"))
        dos.enable_trace()
        try:
            dos.run(max_insns=300_000_000)
        except Exit as e:
            skipped += 1
            print("SKIP %s: the original did not finish in the harness (%s)" % (cid, e))
            continue
        want_trace = []
        for c in dos.calls:
            if c[0] in "SGPE":
                want_trace.append(" ".join(str(x) for x in c))
            elif c[0] == "W":
                want_trace.append("W " + c[1])
        with tempfile.TemporaryDirectory() as tmp:
            cmd = [PUHU, "--voices-dir", FILES, "--events", "--trace", os.path.join(tmp, "trace")]
            if dos.stack_image is not None and not no_stack:
                open(os.path.join(tmp, "stack"), "wb").write(dos.stack_image)
                cmd += ["--stack-image", os.path.join(tmp, "stack"), "--stack-bp", str(dos.start_sp - 0x12)]
            out = subprocess.run(cmd + ["--", tail], capture_output=True)
            got_trace = open(os.path.join(tmp, "trace")).read().splitlines() if out.returncode == 0 else []
        got = []
        for ln in out.stdout.decode().splitlines():
            f = ln.split(" ")
            got.append(("s", bytes.fromhex(f[1])) if f[0] == "s" else ("p", int(f[1])))
        problems = []
        if out.returncode != 0:
            problems.append("puhu exited %d: %s" % (out.returncode, out.stderr.decode()[:120].strip()))
        if got_trace != want_trace:
            i = next((i for i, (a, b) in enumerate(zip(got_trace, want_trace)) if a != b),
                     min(len(got_trace), len(want_trace)))
            def show(t):
                if i >= len(t):
                    return "(end)"
                f = t[i].split(" ", 1)
                return "W " + repr(bytes.fromhex(f[1]).decode("cp437")) if f[0] == "W" else t[i]
            problems.append("stage call %d: got %s, expected %s" % (i, show(got_trace), show(want_trace)))
        elif norm(got) != norm(dos.events):
            a = b"".join(v if k == "s" else b"\x80" * v for k, v in norm(got))
            b = b"".join(v if k == "s" else b"\x80" * v for k, v in norm(dos.events))
            i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            problems.append("same stage calls, stream differs from sample %d of %d (got %d)" % (i, len(b), len(a)))
        if problems:
            bad += 1
            print("FAIL %s: %s" % (cid, "; ".join(problems)))
    print("%d cases, %d failed, %d skipped%s" % (total, bad, skipped,
                                                 " (port started from a zeroed stack)" if no_stack else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
