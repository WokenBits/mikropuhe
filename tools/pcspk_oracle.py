#!/usr/bin/env python3
"""Run the original's PC-speaker code for given calibration values and report what it does.

    tools/pcspk_oracle.py LOOP_NS OVERHEAD_NS

Two routines of PUHU.EXE are executed unmodified in the emulator:
  0x152b7  builds the pulse-width table from the calibration values at DS:0x1dd4 and DS:0x1dd8
  0x15616  the player, here fed one sample of every value 1..255

Output: one line "T" with the 257 table words, then one line per sample value
"b <value> <passes with the speaker on> <passes with it off>", counted by watching the two
LOOP instructions (0x1565c, 0x15667) between the writes to port 0x61.

port/tests/check_pcspk.py compares this with the C port.
"""
import os
import struct
import sys

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_MODE_16, Uc
from unicorn import x86_const as X

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mzdis import LOAD_SEG, load  # noqa: E402

DS = 0x194A
STACK_SEG, STOP = 0x8000, 0x0500  # a scratch stack and a far address to return to


def machine(exe):
    img, _ = load(exe)
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x110000)
    uc.mem_write(LOAD_SEG * 16, img)
    uc.reg_write(X.UC_X86_REG_DS, DS)
    uc.reg_write(X.UC_X86_REG_SS, STACK_SEG)
    return uc


def call(uc, cs, ip, far):
    """Call cs:ip with an empty argument list and run until it returns."""
    sp = 0xFFF0
    ret = struct.pack("<HH", STOP, 0) if far else struct.pack("<H", STOP)
    cs_ret = 0 if far else cs
    sp -= len(ret)
    uc.mem_write(STACK_SEG * 16 + sp, ret)
    uc.reg_write(X.UC_X86_REG_SP, sp)
    uc.reg_write(X.UC_X86_REG_CS, cs)
    uc.emu_start(cs * 16 + ip, cs_ret * 16 + STOP, count=500_000_000)


def run(exe, loop_ns, overhead_ns):
    uc = machine(exe)
    uc.mem_write(DS * 16 + 0x1DD4, struct.pack("<II", loop_ns, overhead_ns))
    call(uc, 0x1516, 0x0157, far=True)  # 0x152b7
    table = struct.unpack("<257H", uc.mem_read(DS * 16 + 0x1DDC, 514))

    # the player: a ring buffer at 7000:0000 holding the sample values 1..255
    buf_seg, n = 0x7000, 255
    uc.mem_write(buf_seg * 16, bytes(range(1, 256)))
    uc.mem_write(DS * 16 + 0x1DD0, struct.pack("<HH", 0, buf_seg))
    uc.mem_write(DS * 16 + 0x1FDE, struct.pack("<H", 0))       # read index
    uc.mem_write(DS * 16 + 0x1DCE, struct.pack("<H", n))       # write index
    uc.mem_write(DS * 16 + 0x1FE0, struct.pack("<H", 0x4000))  # size
    uc.mem_write(DS * 16 + 0x1DB4, b"\0")                      # no abort
    state = {"loops": 0, "writes": []}

    def on_loop(uc, addr, size, user):
        state["loops"] += 1

    def on_out(uc, port, size, value, user):
        if port == 0x61:
            state["writes"].append((value & 2, state["loops"]))
            state["loops"] = 0

    uc.hook_add(UC_HOOK_CODE, on_loop, begin=0x1565C, end=0x1565C)
    uc.hook_add(UC_HOOK_CODE, on_loop, begin=0x15667, end=0x15667)
    uc.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, X.UC_X86_INS_OUT)
    call(uc, 0x1561, 0x0006, far=False)  # 0x15616
    # writes: on, off, on, off, ..., and one final "off" when the buffer is empty
    w = state["writes"]
    per_sample = []
    for i in range(n):
        assert w[2 * i][0] == 2 and w[2 * i + 1][0] == 0, "unexpected port write order"
        on = w[2 * i + 1][1]
        off = w[2 * i + 2][1]
        per_sample.append((i + 1, on, off))
    return table, per_sample


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    table, per_sample = run(os.path.join(here, "..", "original", "files", "PUHU.EXE"),
                            int(sys.argv[1]), int(sys.argv[2]))
    print("T " + " ".join(map(str, table)))
    for b, on, off in per_sample:
        print("b %d %d %d" % (b, on, off))


if __name__ == "__main__":
    main()
