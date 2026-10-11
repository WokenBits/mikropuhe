#!/usr/bin/env python3
"""Dump a MikroPuhe 1.0 voice file (.SMP) completely and account for every byte.

    tools/smpdump.py original/files/JANNE.SMP [--units] [--exceptions] [--wav DIR]

Prints the container, the saved settings, the named parameters of block 0, a summary of the
exception list (block 1) and of the unit tables (block 2), and ends with a coverage report:
which byte ranges are explained by the format as the program reads it (docs/voice-format.md),
and which are not. --units lists every unit variant, --exceptions every exception entry,
--wav writes each unit variant as an 8-bit WAV file for listening.

Needs only the Python standard library.
"""
import os
import struct
import sys
import wave

MAGIC = "MikroPuhe - näytetiedosto\r\nTimehouse Oy 1990\r\n".encode("cp437") + b"\x1aTIME"
SETTINGS = ["speed", "pitch", "pitch variation", "volume", "consonant emphasis", "first-syllable stress",
            "speak lone underscore", "decimal separator is '.'", "bracketed text quieter", "c spoken as k",
            "keyboard hook", "(word 11)", "(word 12)", "sentence-end pauses (set by the program)"]
# block 0 words the code reads: offset -> (name, where it is used)
PARAMS = [
    (0x00, "stop slot, samples", "closure pause = slot - burst length (0x13db4)"),
    (0x02, "doubled-stop extra closure, samples", "0x13d69"),
    (0x04, "h repeats", "noise unit repeat count, scaled by speed (0x13f28)"),
    (0x06, "r repeats", ""),
    (0x08, "s repeats", ""),
    (0x0A, "hh repeats", "doubled (0x13f08)"),
    (0x0C, "rr repeats", ""),
    (0x0E, "ss repeats", ""),
    (0x10, "vowel periods, stress 0", "steady part of a vowel (0x137f0)"),
    (0x12, "vowel periods, stress 1", ""),
    (0x14, "vowel periods, stress 2", ""),
    (0x16, "diphthong glide periods, stress 0", "0x13842"),
    (0x18, "diphthong glide periods, stress 1", ""),
    (0x1A, "diphthong glide periods, stress 2", ""),
    (0x1C, "diphthong second vowel periods, stress 0", "0x13864"),
    (0x1E, "diphthong second vowel periods, stress 1", ""),
    (0x20, "diphthong second vowel periods, stress 2", ""),
    (0x22, "consonant-vowel glide periods", "j l m n v to or from a vowel (0x1398d, 0x13f7a)"),
    (0x24, "j l m n v periods between vowels", "0x140ca"),
    (0x26, "j l m n v periods before b d f g k p t", "0x14108"),
    (0x28, "j l m n v periods at word end", "0x140df"),
    (0x2A, "doubled j l m n v extra periods", "0x1412c"),
    (0x2C, "j l m n v start pitch byte", "when no vowel precedes (0x1409e)"),
    (0x2E, "j l m n v pitch step per period", "0x13f8b"),
    (0x30, "pitch offset, sentence level 1", "added to every pitch byte (0x14ddd)"),
    (0x32, "pitch offset, sentence level 2", ""),
    (0x34, "pitch offset, sentence level 3", ""),
    (0x36, "samples per pitch period", "0x12749"),
    (0x38, "j l m n v periods otherwise", "0x14116"),
    (0x3A, "long vowel extra periods, stress 0", "0x1381d"),
    (0x3C, "long vowel extra periods, stress 1", ""),
    (0x3E, "long vowel extra periods, stress 2", ""),
]
VOWELS = "aeiouyäö"
TABLES = {
    1: ("stops and f: burst by following vowel", ["b", "d", "f", "g", "k", "p", "t"],
        ["no vowel follows"] + ["before " + v for v in VOWELS]),
    2: ("noise", ["h", "r", "s"], ["only"]),
    3: ("pitch periods", list(VOWELS) + ["j", "l", "m", "n", "v", "ng"], ["plain", "after m or n"]),
    4: ("ff (optional)", ["ff"], ["only"]),
}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = set(a for a in sys.argv[1:] if a.startswith("--"))
    wav_dir = None
    if "--wav" in flags:
        wav_dir = args.pop()
        os.makedirs(wav_dir, exist_ok=True)
    path = args[0]
    b = open(path, "rb").read()
    covered = []  # (start, end, what) in file offsets

    def cover(start, end, what):
        covered.append((start, end, what))

    assert b[:51] == MAGIC, "signature mismatch"
    cover(0, 51, "signature")
    n = b[51]
    name = b[52:52 + n].decode("cp437")
    cover(51, 52 + n, "name")
    o = 52 + n
    sett = struct.unpack_from("<14h", b, o)
    pad = b[o + 28:o + 46]
    cover(o, o + 46, "settings block")
    lens = struct.unpack_from("<3I", b, o + 46)
    cover(o + 46, o + 58, "block lengths")
    o += 58
    starts = []
    for ln in lens:
        starts.append(o)
        o += ln + (ln & 1)
    end = o
    print("file        %s, %d bytes" % (os.path.basename(path), len(b)))
    print("voice name  %r" % name)
    print("blocks      0: %d bytes at 0x%x   1: %d at 0x%x   2: %d at 0x%x" % (
        lens[0], starts[0], lens[1], starts[1], lens[2], starts[2]))
    print("\nsaved settings (46-byte block; the last 18 bytes are %s)" % (
        "all 0xFF" if pad == b"\xff" * 18 else pad.hex()))
    for i, v in enumerate(sett):
        print("  %2d  %-42s %d" % (i, SETTINGS[i], v))

    # ---- block 0 ----
    b0 = b[starts[0]:starts[0] + lens[0]]
    print("\nblock 0: parameters (words the program reads)")
    for off, pname, where in PARAMS:
        print("  0x%02x  %-42s %6d   %s" % (off, pname, struct.unpack_from("<h", b0, off)[0], where))
    cover(starts[0], starts[0] + 0x40, "block 0 parameter words")
    for lvl in range(3):
        t = struct.unpack_from("<256b", b0, 0x100 * (lvl + 1))
        print("  0x%03x  pitch contour template, stress %d: 256 signed bytes, min %d max %d, first 16: %s" % (
            0x100 * (lvl + 1), lvl, min(t), max(t), " ".join(map(str, t[:16]))))
    cover(starts[0] + 0x100, starts[0] + 0x400, "block 0 contour templates")
    for lo, hi in ((0x40, 0x100), (0x400, len(b0))):
        rest = b0[lo:hi]
        nz = sum(1 for x in rest if x)
        print("  0x%03x-0x%03x  not read by the program: %d bytes, %d non-zero" % (lo, hi, len(rest), nz))
        if nz == 0:
            cover(starts[0] + lo, starts[0] + hi, "block 0 zero filler")

    # ---- block 1 ----
    b1 = b[starts[1]:starts[1] + lens[1] + (lens[1] & 1)]
    count = struct.unpack_from("<H", b1, 0)[0]
    hi_word = struct.unpack_from("<H", b1, 2)[0]
    p = 4
    entries = []
    for _ in range(count):
        kl = b1[p]
        key = b1[p + 1:p + 1 + kl]
        p += kl + 1
        rl = b1[p]
        rep = b1[p + 1:p + 1 + rl]
        p += rl + 1
        entries.append((key.decode("cp437"), rep.decode("cp437")))
    print("\nblock 1: exception list, %d entries (count word %d, next word %d), parsed %d of %d bytes" % (
        len(entries), count, hi_word, p, lens[1]))
    cover(starts[1], starts[1] + p, "block 1 entries")
    if p < len(b1) and not any(b1[p:]):
        cover(starts[1] + p, starts[1] + len(b1), "block 1 padding")
    if "--exceptions" in flags:
        for k, r in entries:
            print("  %-12r -> %r" % (k, r))
    else:
        print("  first entries: " + ", ".join("%r -> %r" % e for e in entries[:6]) + "  (--exceptions lists all)")

    # ---- block 2 ----
    b2 = b[starts[2]:starts[2] + lens[2] + (lens[2] & 1)]
    offs = struct.unpack_from("<4I", b2, 0)
    cover(starts[2], starts[2] + 16, "block 2 table offsets")
    present = sorted((off, t + 1) for t, off in enumerate(offs) if off)
    print("\nblock 2: unit tables at offsets %s (0 = absent); pitch period %d samples" % (
        list(offs), struct.unpack_from("<h", b0, 0x36)[0]))
    for i, (off, t) in enumerate(present):
        stop = present[i + 1][0] if i + 1 < len(present) else lens[2]
        title, unit_names, var_names = TABLES[t]
        p = off
        units = []
        while p + 2 <= stop:
            cnt = struct.unpack_from("<H", b2, p)[0]
            p += 2
            variants = []
            for _ in range(cnt):
                ln = struct.unpack_from("<H", b2, p)[0]
                variants.append((p + 2, ln))
                p += 2 + ln + (ln & 1)
            units.append(variants)
        exact = "ends exactly at the next table" if p == stop else "ENDS AT 0x%x, EXPECTED 0x%x" % (p, stop)
        print("  table %d (%s): %d units, %d variants, %s" % (
            t, title, len(units), sum(len(u) for u in units), exact))
        cover(starts[2] + off, starts[2] + min(p, stop), "block 2 table %d" % t)
        for ui, variants in enumerate(units):
            uname = unit_names[ui] if ui < len(unit_names) else "?"
            lens_s = " ".join(str(ln) for _, ln in variants)
            if "--units" in flags:
                print("    unit %2d %-3s variants %d, lengths: %s" % (ui + 1, uname, len(variants), lens_s))
            if wav_dir:
                for vi, (dp, ln) in enumerate(variants):
                    fn = os.path.join(wav_dir, "t%d-u%02d-%s-v%d.wav" % (t, ui + 1, uname, vi + 1))
                    with wave.open(fn, "wb") as w:
                        w.setnchannels(1)
                        w.setsampwidth(1)
                        w.setframerate(22000)
                        w.writeframes(b2[dp:dp + ln])
        if "--units" not in flags:
            print("    lengths per unit: " + "; ".join(
                "%s %s" % (unit_names[ui] if ui < len(unit_names) else "?",
                           ",".join(sorted(set(str(ln) for _, ln in v)))) for ui, v in enumerate(units)))
    if end < len(b):
        tail = b[end:]
        print("\nafter the last block: %d bytes: %s" % (len(tail), tail.hex()))

    # ---- coverage ----
    covered.sort()
    explained = sum(e - s for s, e, _ in covered)
    gaps, pos = [], 0
    for s, e, _ in covered:
        if s > pos:
            gaps.append((pos, s))
        pos = max(pos, e)
    if pos < len(b):
        gaps.append((pos, len(b)))
    print("\ncoverage: %d of %d bytes explained" % (explained, len(b)))
    for s, e in gaps:
        chunk = b[s:e]
        print("  unexplained 0x%05x-0x%05x (%d bytes, %d non-zero): %s%s" % (
            s, e, e - s, sum(1 for x in chunk if x), chunk[:16].hex(), "..." if e - s > 16 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
