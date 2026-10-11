#!/usr/bin/env python3
"""Take a MikroPuhe voice file (.SMP) apart into editable files, and put one together again.

    tools/smptool.py unpack VOICE.SMP DIR      write DIR/voice.ini, exceptions.tsv, contours.tsv, units/*.wav, raw/*
    tools/smptool.py pack DIR VOICE.SMP        build a voice file from such a directory
    tools/smptool.py roundtrip VOICE.SMP       unpack to a temporary directory, pack, and compare with the original

The directory:
    voice.ini        name, the 14 saved settings, the 32 parameters of block 0
    exceptions.tsv   the exception list: key <TAB> replacement, one per line, in order (the first match wins)
    contours.tsv     the three 256-step pitch contour templates, one column per stress level
    units/           every recorded unit as a WAV file (8-bit unsigned mono, 22000 Hz), named
                     t<table>-u<unit>-<sound>-v<variant>.wav; the numbers decide the place in the file
    raw/             bytes the program does not read, kept so that packing gives the identical file

Text files are UTF-8; the voice file itself is code page 437. In exceptions.tsv a backslash, a tab and
control characters are written as \\\\, \\t and \\xNN. The format is described in docs/voice-format.md.
Needs only the Python standard library.
"""
import configparser
import os
import re
import struct
import sys
import tempfile
import wave

MAGIC = "MikroPuhe - näytetiedosto\r\nTimehouse Oy 1990\r\n".encode("cp437") + b"\x1aTIME"
SETTINGS = ["speed", "pitch", "pitch_variation", "volume", "consonant_emphasis", "first_syllable_stress",
            "speak_underscore", "decimal_dot", "brackets_quieter", "c_as_k", "keyboard_hook", "word11", "word12",
            "sentence_pauses"]
PARAMS = [(0x00, "stop_slot_samples"), (0x02, "doubled_stop_extra_samples"),
          (0x04, "h_repeats"), (0x06, "r_repeats"), (0x08, "s_repeats"),
          (0x0A, "hh_repeats"), (0x0C, "rr_repeats"), (0x0E, "ss_repeats"),
          (0x10, "vowel_periods_stress0"), (0x12, "vowel_periods_stress1"), (0x14, "vowel_periods_stress2"),
          (0x16, "diphthong_glide_periods_stress0"), (0x18, "diphthong_glide_periods_stress1"),
          (0x1A, "diphthong_glide_periods_stress2"),
          (0x1C, "diphthong_second_periods_stress0"), (0x1E, "diphthong_second_periods_stress1"),
          (0x20, "diphthong_second_periods_stress2"),
          (0x22, "consonant_vowel_glide_periods"), (0x24, "voiced_periods_between_vowels"),
          (0x26, "voiced_periods_before_stop"), (0x28, "voiced_periods_word_end"),
          (0x2A, "doubled_voiced_extra_periods"), (0x2C, "voiced_start_pitch"), (0x2E, "voiced_pitch_step"),
          (0x30, "pitch_offset_level1"), (0x32, "pitch_offset_level2"), (0x34, "pitch_offset_level3"),
          (0x36, "period_samples"), (0x38, "voiced_periods_other"),
          (0x3A, "long_vowel_extra_periods_stress0"), (0x3C, "long_vowel_extra_periods_stress1"),
          (0x3E, "long_vowel_extra_periods_stress2")]
SOUNDS = {1: ["b", "d", "f", "g", "k", "p", "t"], 2: ["h", "r", "s"],
          3: ["a", "e", "i", "o", "u", "y", "ae", "oe", "j", "l", "m", "n", "v", "ng"], 4: ["ff"]}
RATE = 22000


def esc(b):
    out = ""
    for ch in b.decode("cp437"):
        if ch == "\\":
            out += "\\\\"
        elif ch == "\t":
            out += "\\t"
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            out += "\\x%02x" % ord(ch)
        else:
            out += ch
    return out


def unesc(s):
    out, i = "", 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            if s[i + 1] == "\\":
                out, i = out + "\\", i + 2
            elif s[i + 1] == "t":
                out, i = out + "\t", i + 2
            elif s[i + 1] == "x":
                out, i = out + chr(int(s[i + 2:i + 4], 16)), i + 4
            else:
                raise ValueError("unknown escape in %r" % s)
        else:
            out, i = out + s[i], i + 1
    return out.encode("cp437")


def parse(b):
    """Voice file bytes -> dict."""
    if b[:51] != MAGIC:
        raise ValueError("not a MikroPuhe voice file")
    n = b[51]
    v = {"name": b[52:52 + n]}
    o = 52 + n
    v["settings"] = list(struct.unpack_from("<14h", b, o))
    v["settings_pad"] = b[o + 28:o + 46]
    lens = struct.unpack_from("<3I", b, o + 46)
    o += 58
    blocks = []
    for ln in lens:
        blocks.append(b[o:o + ln])
        o += ln + (ln & 1)
    v["trailer"] = b[o:]
    b0, b1, b2 = blocks
    v["block0"] = b0
    count, v["exc_word2"] = struct.unpack_from("<HH", b1, 0)
    p, exc = 4, []
    for _ in range(count):
        kl = b1[p]
        key = b1[p + 1:p + 1 + kl]
        p += kl + 1
        rl = b1[p]
        exc.append((key, b1[p + 1:p + 1 + rl]))
        p += rl + 1
    if p != len(b1):
        raise ValueError("exception list: %d bytes parsed of %d" % (p, len(b1)))
    v["exceptions"] = exc
    offs = struct.unpack_from("<4I", b2, 0)
    present = sorted((off, t + 1) for t, off in enumerate(offs) if off)
    tables = {}
    for i, (off, t) in enumerate(present):
        stop = present[i + 1][0] if i + 1 < len(present) else len(b2)
        p, units = off, []
        while p + 2 <= stop:
            cnt = struct.unpack_from("<H", b2, p)[0]
            p += 2
            variants = []
            for _ in range(cnt):
                ln = struct.unpack_from("<H", b2, p)[0]
                variants.append(b2[p + 2:p + 2 + ln])
                p += 2 + ln + (ln & 1)
            units.append(variants)
        if p != stop:
            raise ValueError("table %d ends at %d, expected %d" % (t, p, stop))
        tables[t] = units
    v["tables"] = tables
    return v


def build(v):
    """dict -> voice file bytes."""
    b1 = struct.pack("<HH", len(v["exceptions"]), v["exc_word2"])
    for key, rep in v["exceptions"]:
        if len(key) > 255 or len(rep) > 255:
            raise ValueError("exception %r: key and replacement are limited to 255 bytes" % key)
        b1 += bytes([len(key)]) + key + bytes([len(rep)]) + rep
    body, offs = b"", [0, 0, 0, 0]
    for t in sorted(v["tables"]):
        offs[t - 1] = 16 + len(body)
        for variants in v["tables"][t]:
            body += struct.pack("<H", len(variants))
            for data in variants:
                body += struct.pack("<H", len(data)) + data + (b"\x80" if len(data) & 1 else b"")
    b2 = struct.pack("<4I", *offs) + body
    for name, blk in (("block 1", b1), ("block 2", b2)):
        if len(blk) > 0xFFFF:
            raise ValueError("%s is %d bytes; the program addresses a block with 16 bits" % (name, len(blk)))
    out = MAGIC + bytes([len(v["name"])]) + v["name"] + struct.pack("<14h", *v["settings"]) + v["settings_pad"]
    out += struct.pack("<3I", len(v["block0"]), len(b1), len(b2))
    for blk in (v["block0"], b1, b2):
        out += blk + (b"\0" if len(blk) & 1 else b"")
    return out + v["trailer"]


def unpack(src, dst):
    v = parse(open(src, "rb").read())
    os.makedirs(os.path.join(dst, "units"), exist_ok=True)
    os.makedirs(os.path.join(dst, "raw"), exist_ok=True)
    b0 = v["block0"]
    with open(os.path.join(dst, "voice.ini"), "w", encoding="utf-8") as f:
        f.write("; MikroPuhe voice, unpacked by tools/smptool.py. Meanings: docs/voice-format.md\n")
        f.write("[voice]\nname = %s\n\n[settings]\n" % v["name"].decode("cp437"))
        f.write("; speed, pitch, pitch_variation, volume: 0..8. The rest: 0 or 1.\n")
        for k, val in zip(SETTINGS, v["settings"]):
            f.write("%s = %d\n" % (k, val))
        f.write("\n[parameters]\n; durations are in pitch periods unless the name says samples\n")
        for off, k in PARAMS:
            f.write("%s = %d\n" % (k, struct.unpack_from("<h", b0, off)[0]))
    with open(os.path.join(dst, "exceptions.tsv"), "w", encoding="utf-8", newline="\n") as f:
        for key, rep in v["exceptions"]:
            f.write("%s\t%s\n" % (esc(key), esc(rep)))
    with open(os.path.join(dst, "contours.tsv"), "w") as f:
        f.write("stress0\tstress1\tstress2\n")
        cols = [struct.unpack_from("<256b", b0, 0x100 * (i + 1)) for i in range(3)]
        for row in zip(*cols):
            f.write("%d\t%d\t%d\n" % row)
    for t, units in v["tables"].items():
        for ui, variants in enumerate(units):
            sound = SOUNDS[t][ui] if ui < len(SOUNDS[t]) else "x"
            for vi, data in enumerate(variants):
                with wave.open(os.path.join(dst, "units", "t%d-u%02d-%s-v%d.wav" % (t, ui + 1, sound, vi + 1)), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(1)
                    w.setframerate(RATE)
                    w.writeframes(data)
    raw = {"block0-0x040-0x100.bin": b0[0x40:0x100], "block0-0x400-end.bin": b0[0x400:],
           "settings-pad.bin": v["settings_pad"], "trailer.bin": v["trailer"],
           "exceptions-word2.bin": struct.pack("<H", v["exc_word2"])}
    for name, data in raw.items():
        open(os.path.join(dst, "raw", name), "wb").write(data)
    return v


def pack(src, dst):
    cp = configparser.ConfigParser(comment_prefixes=(";",), interpolation=None)
    cp.read(os.path.join(src, "voice.ini"), encoding="utf-8")
    raw = lambda n: open(os.path.join(src, "raw", n), "rb").read()  # noqa: E731
    v = {"name": cp["voice"]["name"].encode("cp437"),
         "settings": [cp["settings"].getint(k) for k in SETTINGS],
         "settings_pad": raw("settings-pad.bin"), "trailer": raw("trailer.bin"),
         "exc_word2": struct.unpack("<H", raw("exceptions-word2.bin"))[0]}
    if len(v["name"]) > 255:
        raise ValueError("voice name too long")
    b0 = bytearray(0x400)
    for off, k in PARAMS:
        struct.pack_into("<h", b0, off, cp["parameters"].getint(k))
    b0[0x40:0x100] = raw("block0-0x040-0x100.bin")
    rows = [ln.split("\t") for ln in open(os.path.join(src, "contours.tsv")).read().splitlines()[1:]]
    if len(rows) != 256:
        raise ValueError("contours.tsv must have 256 rows after the header, has %d" % len(rows))
    for lvl in range(3):
        struct.pack_into("<256b", b0, 0x100 * (lvl + 1), *[int(r[lvl]) for r in rows])
    v["block0"] = bytes(b0) + raw("block0-0x400-end.bin")
    exc = []
    for n, line in enumerate(open(os.path.join(src, "exceptions.tsv"), encoding="utf-8").read().split("\n"), 1):
        if line == "":
            continue
        if line.count("\t") != 1:
            raise ValueError("exceptions.tsv line %d: want key<TAB>replacement" % n)
        key, rep = line.split("\t")
        exc.append((unesc(key), unesc(rep)))
    v["exceptions"] = exc
    period = cp["parameters"].getint("period_samples")
    tables = {}
    for name in sorted(os.listdir(os.path.join(src, "units"))):
        m = re.match(r"t(\d+)-u(\d+)-.*-v(\d+)\.wav$", name)
        if not m:
            continue
        t, u, var = (int(x) for x in m.groups())
        with wave.open(os.path.join(src, "units", name), "rb") as w:
            if (w.getnchannels(), w.getsampwidth()) != (1, 1):
                raise ValueError("%s: units must be 8-bit mono WAV" % name)
            data = w.readframes(w.getnframes())
        if t == 3 and len(data) != period:
            raise ValueError("%s: a table 3 unit must be exactly one pitch period, %d samples; this has %d" % (
                name, period, len(data)))
        units = tables.setdefault(t, {})
        units.setdefault(u, {})[var] = data
    v["tables"] = {}
    for t, units in tables.items():
        if sorted(units) != list(range(1, len(units) + 1)):
            raise ValueError("table %d: unit numbers must run from 1 without gaps" % t)
        v["tables"][t] = []
        for u in sorted(units):
            if sorted(units[u]) != list(range(1, len(units[u]) + 1)):
                raise ValueError("table %d unit %d: variant numbers must run from 1 without gaps" % (t, u))
            v["tables"][t].append([units[u][k] for k in sorted(units[u])])
    out = build(v)
    open(dst, "wb").write(out)
    return out


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "unpack":
        v = unpack(a[1], a[2])
        print("%s -> %s: %d exceptions, %d unit files" % (a[1], a[2], len(v["exceptions"]),
                                                         sum(len(x) for u in v["tables"].values() for x in u)))
    elif len(a) == 3 and a[0] == "pack":
        out = pack(a[1], a[2])
        print("%s -> %s: %d bytes" % (a[1], a[2], len(out)))
    elif len(a) == 2 and a[0] == "roundtrip":
        orig = open(a[1], "rb").read()
        with tempfile.TemporaryDirectory() as tmp:
            unpack(a[1], os.path.join(tmp, "v"))
            out = pack(os.path.join(tmp, "v"), os.path.join(tmp, "out.smp"))
        same = out == orig
        print("%s: unpack + pack gives %s (%d bytes)" % (os.path.basename(a[1]),
                                                          "the identical file" if same else "A DIFFERENT FILE", len(out)))
        return 0 if same else 1
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
