#!/usr/bin/env python3
"""Check the virtual PC speaker.

1. Against the original: for every machine preset and a few other calibration values, the pulse-width
   table (0x152b7) and the passes the player (0x15616) makes with the speaker on and off, for every
   sample value, must equal what the original code does in the emulator.
2. The renderer against its own arithmetic (there is no original to compare the sound with):
   a steady sample value must come out as a steady level equal to the pulse's duty cycle, with the
   22 kHz carrier gone; a pause must last n * 64 timer ticks.

    tests/check_pcspk.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
DUMP = os.path.join(HERE, "..", "build", "pcspk_dump")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pcspk_oracle  # noqa: E402

EXE = os.path.join(ROOT, "original", "files", "PUHU.EXE")
# (loop_ns, overhead_ns): the presets of src/pcspk.c, then values near the limits
CAL = [(1, 0), (100, 800), (400, 2500), (1100, 7400), (3600, 33000),
       (17, 5), (250, 12345), (2000, 20000), (5000, 40000), (22727, 22726), (40000, 5000), (3, 45000)]


def dump(*args):
    return subprocess.run([DUMP] + [str(a) for a in args], capture_output=True, text=True, check=True).stdout


def main():
    bad = 0
    for loop_ns, ovh in CAL:
        table, per_sample = pcspk_oracle.run(EXE, loop_ns, ovh)
        want = "T " + " ".join(map(str, table)) + "\n" + "".join("b %d %d %d\n" % x for x in per_sample)
        got = dump("table", loop_ns, ovh)
        if got != want:
            bad += 1
            g, w = got.splitlines(), want.splitlines()
            i = next((i for i, (a, b) in enumerate(zip(g, w)) if a != b), 0)
            print("FAIL table loop=%d overhead=%d: line %d got %r, original %r" % (loop_ns, ovh, i, g[i][:60], w[i][:60]))
    print("pulse-width table and player passes: %d calibrations, %d differ from the original" % (len(CAL), bad))

    checks = worst_duty = worst_ripple = 0
    for machine in ("ideal", "486", "386", "at", "xt"):
        for rate in (44100, 48000):
            for value in (1, 40, 100, 128, 150, 200, 255):
                duty, measured, ripple, n, period = map(float, dump("level", machine, rate, value).split())
                checks += 1
                worst_duty = max(worst_duty, abs(duty - measured))
                worst_ripple = max(worst_ripple, ripple)
                expect_n = 11000 * period * 1e-9 * rate + (385 + 8) / 8  # plus the flush of the low-pass
                # ripple: what is left of the pulse rate after the low-pass; 52 of 16384 is -50 dB
                if abs(duty - measured) > 0.002 or ripple > 52.0 or abs(n - expect_n) > 2:
                    bad += 1
                    print("FAIL level %s %d Hz value %d: duty %.4f measured %.4f ripple %.1f samples %d (expected %.1f)" % (
                        machine, rate, value, duty, measured, ripple, n, expect_n))
    print("steady levels: %d checks, largest duty error %.5f, largest ripple %.2f of 16384" % (
        checks, worst_duty, worst_ripple))
    for rate in (44100, 48000):
        for n in (110, 8000, 20000):
            got = int(dump("pause", "ideal", rate, n))
            expect = n * 64 / 1193182 * rate + (385 + 8) / 8  # plus the flush of the low-pass
            if abs(got - expect) > 2:
                bad += 1
                print("FAIL pause %d at %d Hz: %d samples, expected %.1f" % (n, rate, got, expect))
    print("FAILED" if bad else "all PC-speaker checks passed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
