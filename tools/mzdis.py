#!/usr/bin/env python3
"""Disassemble a linear address range of a DOS MZ program (Intel syntax).

The image is loaded at segment 0x1000 with relocations applied, so addresses
and far-call segments match the Ghidra/REA coordinates used in the notes.

    tools/mzdis.py original/files/PUHU.EXE 0x15167 0x152b7
    tools/mzdis.py original/files/PUHU.EXE 0x15167            # up to the first RETF/RET/IRET

Needs capstone (~/tools/mikropuhe-venv on ai).
"""
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_16, Cs

LOAD_SEG = 0x1000


def load(path, load_seg=LOAD_SEG):
    """Return (image bytes with relocations applied, MZ header dict)."""
    b = open(path, "rb").read()
    (sig, last, pages, nreloc, hdrpar, minalloc, maxalloc, ss, sp, csum, ip, cs,
     reloff, ovl) = struct.unpack_from("<2s13H", b, 0)
    assert sig == b"MZ"
    hdr = hdrpar * 16
    size = pages * 512 - (512 - last if last else 0)
    img = bytearray(b[hdr:size])
    for i in range(nreloc):
        off, seg = struct.unpack_from("<HH", b, reloff + 4 * i)
        p = seg * 16 + off
        struct.pack_into("<H", img, p, (struct.unpack_from("<H", img, p)[0] + load_seg) & 0xFFFF)
    return bytes(img), dict(hdr=hdr, ss=ss, sp=sp, ip=ip, cs=cs, minalloc=minalloc,
                            maxalloc=maxalloc, nreloc=nreloc, size=size)


def disasm(img, start, end=None, base=LOAD_SEG * 16):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    out = []
    off = start - base
    stop = (end - base) if end else len(img)
    for ins in md.disasm(img[off:stop], start):
        note = ""
        if ins.mnemonic in ("lcall", "ljmp"):
            parts = ins.op_str.replace(",", ":").split(":")
            if len(parts) == 2 and all(x.strip().startswith("0x") for x in parts):
                seg, o = (int(x, 16) for x in parts)
                note = "   ; -> 0x%05x" % (seg * 16 + o)
        out.append("%05x: %-7s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
        if end is None and ins.mnemonic in ("retf", "ret", "iret"):
            break
    return out


if __name__ == "__main__":
    image, _ = load(sys.argv[1])
    a = int(sys.argv[2], 16)
    e = int(sys.argv[3], 16) if len(sys.argv) > 3 else None
    print("\n".join(disasm(image, a, e)))
