#!/usr/bin/env python3
"""Run the original PUHU.EXE in a CPU emulator and capture what it sends to its output stage.

The program is executed unmodified on disk; the copy in emulator memory gets
three small patches so that no PC hardware has to be modelled:

  0x15167  CPU-speed calibration (needs a ticking PIT)  -> returns 0 ("fast enough")
  0x152b7  builds the PWM delay table from calibration   -> returns at once
  0x15616  the PC-speaker player (drains the ring buffer) -> drained here instead, returns 0

Everything upstream of the ring buffer (text handling, unit selection, sample
generation, volume) runs as original machine code. What the player would have
consumed is written out as an event list: runs of 8-bit unsigned samples and
"pause n" records, exactly as they sat in the ring buffer.

    puhu_emu.py [--dir original/files] [--out PREFIX] [--trace] -- <DOS command tail>
    puhu_emu.py --out /tmp/x -- "hyvää päivää"

PREFIX.events.json  the event list
PREFIX.u8           samples with each pause expanded to n bytes of 0x80
PREFIX.wav          the same as mono 8-bit WAV at 22000 Hz
PREFIX.stdout.txt   what the program wrote to DOS handles 1 and 2 (decoded from CP437)
PREFIX.screen.txt   the text screen at exit (the program prints through direct video writes)

Needs unicorn (~/tools/mikropuhe-venv on ai). Addresses are linear, image at segment 0x1000.
"""
import argparse
import json
import os
import struct
import sys
import wave

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_HOOK_INTR, UC_MODE_16, Uc, UcError
from unicorn import x86_const as X

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mzdis import LOAD_SEG, load  # noqa: E402

PSP_SEG = LOAD_SEG - 0x10
ENV_SEG = 0x0F00
TOP_SEG = 0xA000
DS_SEG = 0x194A  # the program's data segment (mov ax,0x194a / mov ds,ax at 0x15743)
SAMPLE_RATE = 22000  # nominal: PWM period 45454 ns, see notes

A_CALIBRATE = 0x15167
A_BUILD_TABLE = 0x152B7
A_PLAYER = 0x15616
# ring buffer variables, offsets in DS_SEG
V_BUF_PTR, V_WRITE, V_READ, V_SIZE, V_TOTAL = 0x1DD0, 0x1DCE, 0x1FDE, 0x1FE0, 0x1FE2

CF, ZF = 1, 0x40


class Exit(Exception):
    pass


class Dos:
    def __init__(self, exe, files_dir, tail, trace=False, keys=b"", extra_files=None):
        self.trace = trace
        self.files_dir = files_dir
        self.extra_files = {k.upper(): v for k, v in (extra_files or {}).items()}  # DOS name -> host path
        self.stdout = bytearray()
        self.events = []  # ["s", bytes] | ["p", n]
        self.handles = {}
        self.next_handle = 5
        self.exit_code = None
        self.ports = {}
        self.keys = list(keys)
        self.dta = (PSP_SEG, 0x80)
        self.unknown = []
        self.cursor = 0
        self.typed_late = False

        img, hdr = load(exe)
        self.hdr = hdr
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.mem_write(LOAD_SEG * 16, img)

        # every interrupt vector -> an IRET at F000:FF53
        uc.mem_write(0xFFF53, b"\xcf")
        uc.mem_write(0, struct.pack("<HH", 0xFF53, 0xF000) * 256)
        # BIOS data area: 80x25 colour text, 640 KB
        uc.mem_write(0x410, struct.pack("<H", 0x0021))
        uc.mem_write(0x413, struct.pack("<H", 640))
        uc.mem_write(0x449, bytes([3]))
        uc.mem_write(0x44A, struct.pack("<H", 80))
        uc.mem_write(0x463, struct.pack("<H", 0x3D4))
        uc.mem_write(0x484, bytes([24]))

        env = b"COMSPEC=C:\\COMMAND.COM\0PATH=C:\\\0\0" + struct.pack("<H", 1) + b"C:\\PUHU.EXE\0"
        uc.mem_write(ENV_SEG * 16, env)
        psp = bytearray(256)
        psp[0:2] = b"\xcd\x20"
        struct.pack_into("<H", psp, 2, TOP_SEG)
        struct.pack_into("<H", psp, 0x2C, ENV_SEG)
        tail = tail[:126]
        psp[0x80] = len(tail)
        psp[0x81:0x81 + len(tail)] = tail
        psp[0x81 + len(tail)] = 0x0D
        uc.mem_write(PSP_SEG * 16, bytes(psp))

        self.psp_end = TOP_SEG  # the program owns everything until it shrinks its block
        self.blocks = {}  # seg -> paragraphs, allocated top-down below TOP_SEG

        # the three patches described in the module docstring
        uc.mem_write(A_CALIBRATE, b"\x31\xc0\xcb")  # xor ax,ax ; retf
        uc.mem_write(A_BUILD_TABLE, b"\xcb")  # retf
        uc.mem_write(A_PLAYER, b"\x31\xc0\xc3")  # xor ax,ax ; ret
        uc.hook_add(UC_HOOK_CODE, self.on_player, begin=A_PLAYER, end=A_PLAYER)

        uc.hook_add(UC_HOOK_INTR, self.on_int)
        uc.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, X.UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, X.UC_X86_INS_OUT)

        for reg, val in ((X.UC_X86_REG_DS, PSP_SEG), (X.UC_X86_REG_ES, PSP_SEG),
                         (X.UC_X86_REG_SS, hdr["ss"] + LOAD_SEG), (X.UC_X86_REG_SP, hdr["sp"]),
                         (X.UC_X86_REG_CS, hdr["cs"] + LOAD_SEG)):
            uc.reg_write(reg, val)
        self.entry = (hdr["cs"] + LOAD_SEG) * 16 + hdr["ip"]

    # --- helpers -----------------------------------------------------------
    def r(self, reg):
        return self.uc.reg_read(reg)

    def w(self, reg, val):
        self.uc.reg_write(reg, val & 0xFFFF)

    def flag(self, mask, on):
        f = self.r(X.UC_X86_REG_EFLAGS)
        self.uc.reg_write(X.UC_X86_REG_EFLAGS, (f | mask) if on else (f & ~mask))

    def word(self, seg, off):
        return struct.unpack("<H", self.uc.mem_read(seg * 16 + off, 2))[0]

    def setword(self, seg, off, val):
        self.uc.mem_write(seg * 16 + off, struct.pack("<H", val & 0xFFFF))

    def asciiz(self, seg, off):
        out = bytearray()
        while True:
            c = self.uc.mem_read(seg * 16 + off + len(out), 1)[0]
            if not c:
                return bytes(out)
            out.append(c)

    def pc(self):
        return self.r(X.UC_X86_REG_CS) * 16 + self.r(X.UC_X86_REG_IP)

    def fail(self, ax=2):
        self.w(X.UC_X86_REG_AX, ax)
        self.flag(CF, True)

    # --- stage tracing -------------------------------------------------------
    def stack_word(self, n):
        """Word n of the stack at a far function's entry: 0 = return IP, 1 = return CS, 2 = first argument."""
        return self.word(self.r(X.UC_X86_REG_SS), (self.r(X.UC_X86_REG_SP) + 2 * n) & 0xFFFF)

    def enable_trace(self):
        """Record the calls that cross the text / phoneme / device boundaries into self.calls.

        ["S", size, w0..w13]              dev_start (0x1548a): a lib_speak call begins; ring buffer size, active settings
        ["G", level, first, paren, vol]   globals 0x1b13, 0x1d9a, 0x1b47, 0x1b1f at speak_phonemes entry
        ["F", hex, bp]                    stale stack bytes [bp-0x360, bp) under speak_phonemes's frame, and its bp
        ["W", hex]                        speak_phonemes (0x13543) argument string
        ["R", n, hex, total]              its return value, the string it left behind, samples + pause units so far
        ["P", n]                          dev_pause (0x1551d) called from outside speak_phonemes
        ["E"]                             dev_stop (0x154e1)
        """
        self.calls = []
        self.stack_image = None
        self.start_sp = None
        self.depth = 0
        self.phon_ptr = None
        hooks = {0x1548A: self.t_start, 0x154E1: self.t_stop, 0x13543: self.t_phon_in,
                 0x142DD: self.t_phon_out, 0x1551D: self.t_pause}
        for addr, fn in hooks.items():
            self.uc.hook_add(UC_HOOK_CODE, fn, begin=addr, end=addr)

    def t_start(self, uc, address, size, user):
        self.calls.append(["S", self.stack_word(4)] + [self.word(DS_SEG, 0x1B19 + 2 * i) for i in range(14)])
        if self.stack_image is None:  # the stack as the first speak call finds it, and where that call sits
            ss = self.r(X.UC_X86_REG_SS)
            self.stack_image = bytes(uc.mem_read(ss * 16, 0x2000))
            self.start_sp = self.r(X.UC_X86_REG_SP)

    def t_stop(self, uc, address, size, user):
        self.calls.append(["E"])

    def t_phon_in(self, uc, address, size, user):
        self.depth += 1
        self.phon_ptr = (self.stack_word(3), self.stack_word(2))
        self.calls.append(["G", self.word(DS_SEG, 0x1B13), uc.mem_read(DS_SEG * 16 + 0x1D9A, 1)[0],
                           uc.mem_read(DS_SEG * 16 + 0x1B47, 1)[0], self.word(DS_SEG, 0x1B1F)])
        # the 0x360 bytes its stack frame is about to occupy: it reads parts of them before writing them
        bp = (self.r(X.UC_X86_REG_SP) - 2) & 0xFFFF
        self.calls.append(["F", bytes(uc.mem_read(self.r(X.UC_X86_REG_SS) * 16 + bp - 0x360, 0x360)).hex(), bp])
        self.calls.append(["W", self.asciiz(*self.phon_ptr).hex()])

    def t_phon_out(self, uc, address, size, user):
        self.depth -= 1
        self.drain()  # bring the event list up to date, so the stream position of this word is known
        self.calls.append(["R", self.r(X.UC_X86_REG_AX), self.asciiz(*self.phon_ptr).hex(),
                           sum(len(v) if k == "s" else v for k, v in self.events)])

    def t_pause(self, uc, address, size, user):
        if self.depth == 0:
            self.calls.append(["P", self.stack_word(2)])

    # --- the output stage ----------------------------------------------------
    def on_player(self, uc, address, size, user):
        self.drain()

    def drain(self):
        uc = self.uc
        seg = self.word(DS_SEG, V_BUF_PTR + 2)
        base = seg * 16 + self.word(DS_SEG, V_BUF_PTR)
        rd, wr, cap = (self.word(DS_SEG, v) for v in (V_READ, V_WRITE, V_SIZE))
        total = 0
        while rd != wr:
            end = wr if wr > rd else cap
            chunk = bytes(uc.mem_read(base + rd, end - rd))
            i = 0
            run = bytearray()
            while i < len(chunk):
                if chunk[i]:
                    run.append(chunk[i])
                    i += 1
                else:
                    if run:
                        self.events.append(["s", bytes(run)])
                        run = bytearray()
                    n = struct.unpack_from("<H", chunk, i + 1)[0]
                    self.events.append(["p", n])
                    total += n
                    i += 3
            if run:
                self.events.append(["s", bytes(run)])
            total += i
            rd += i
            if rd >= cap:
                rd = 0
        self.setword(DS_SEG, V_READ, rd)
        t = self.word(DS_SEG, V_TOTAL) | (self.word(DS_SEG, V_TOTAL + 2) << 16)
        t += total
        self.setword(DS_SEG, V_TOTAL, t)
        self.setword(DS_SEG, V_TOTAL + 2, t >> 16)

    def on_in(self, uc, port, size, user):
        n = self.ports[("in", port)] = self.ports.get(("in", port), 0) + 1
        if port == 0x3DA:  # CGA status: alternate "in retrace" and "not in retrace"
            return 0x09 if n & 1 else 0x00
        return 0

    def on_out(self, uc, port, size, value, user):
        self.ports[("out", port)] = self.ports.get(("out", port), 0) + 1

    # --- interrupts ----------------------------------------------------------
    def on_int(self, uc, intno, user):
        ax = self.r(X.UC_X86_REG_AX)
        ah, al = ax >> 8, ax & 0xFF
        if intno == 0x21:
            return self.int21(ah, al)
        if intno == 0x20:
            self.exit_code = 0
            uc.emu_stop()
        elif intno == 0x10:
            if ah == 0x0F:  # video mode: 80x25 colour text, page 0
                self.w(X.UC_X86_REG_AX, (80 << 8) | 3)
                self.w(X.UC_X86_REG_BX, self.r(X.UC_X86_REG_BX) & 0x00FF)
            elif ah == 0x02:  # set cursor position
                self.cursor = self.r(X.UC_X86_REG_DX)
            elif ah == 0x03:  # cursor position
                self.w(X.UC_X86_REG_DX, self.cursor)
                self.w(X.UC_X86_REG_CX, 0x0607)
            elif ah in (0x06, 0x07):  # scroll a window up or down; AL = 0 clears it
                self.scroll(ah == 0x06, al, self.r(X.UC_X86_REG_BX) >> 8, self.r(X.UC_X86_REG_CX),
                            self.r(X.UC_X86_REG_DX))
            elif ah == 0x08:  # read character and attribute at the cursor
                off = 0xB8000 + 2 * ((self.cursor >> 8) * 80 + (self.cursor & 0xFF))
                ch, at = uc.mem_read(off, 2)
                self.w(X.UC_X86_REG_AX, (at << 8) | ch)
            elif ah in (0x09, 0x0A):  # write character (and attribute) at the cursor, CX times
                off = 0xB8000 + 2 * ((self.cursor >> 8) * 80 + (self.cursor & 0xFF))
                for i in range(self.r(X.UC_X86_REG_CX)):
                    uc.mem_write(off + 2 * i, bytes([al]))
                    if ah == 0x09:
                        uc.mem_write(off + 2 * i + 1, bytes([self.r(X.UC_X86_REG_BX) & 0xFF]))
            elif ah == 0x12:  # EGA information: report "not EGA" by leaving BL unchanged
                pass
        elif intno == 0x11:
            self.w(X.UC_X86_REG_AX, 0x0021)
        elif intno == 0x12:
            self.w(X.UC_X86_REG_AX, 640)
        elif intno == 0x16:
            if ah in (0x01, 0x11):
                # typed_late: a key shows up only once the program sits waiting for it, as when a
                # person types after the program has finished speaking. The interface empties the
                # keyboard buffer before it reads, so keys that are "already there" would be lost.
                waiting = bool(self.keys) and not self.typed_late
                self.flag(ZF, not waiting)
                if waiting:
                    self.w(X.UC_X86_REG_AX, self.keys[0])
            elif ah in (0x00, 0x10):
                if not self.keys:
                    raise Exit("program waits for a key and none is queued (pc 0x%05x)" % self.pc())
                self.w(X.UC_X86_REG_AX, self.keys.pop(0))
            elif ah == 0x02:
                self.w(X.UC_X86_REG_AX, 0)
        elif intno == 0x1A:
            if ah == 0:
                self.w(X.UC_X86_REG_CX, 0)
                self.w(X.UC_X86_REG_DX, 0)
                self.w(X.UC_X86_REG_AX, 0)
        elif intno == 0:
            raise Exit("divide error at pc 0x%05x" % self.pc())
        else:
            self.unknown.append("int %02x ax=%04x pc=%05x" % (intno, ax, self.pc()))

    def host_path(self, name):
        base = name.decode("cp437").replace("/", "\\").split("\\")[-1].upper()
        if base in self.extra_files:
            return self.extra_files[base]
        for f in os.listdir(self.files_dir):
            if f.upper() == base:
                return os.path.join(self.files_dir, f)
        return None

    def int21(self, ah, al):
        uc = self.uc
        bx, cx, dx = (self.r(x) for x in (X.UC_X86_REG_BX, X.UC_X86_REG_CX, X.UC_X86_REG_DX))
        ds, es = self.r(X.UC_X86_REG_DS), self.r(X.UC_X86_REG_ES)
        self.flag(CF, False)
        if self.trace:
            print("int21 ax=%02x%02x bx=%04x cx=%04x dx=%04x pc=%05x" % (ah, al, bx, cx, dx, self.pc()),
                  file=sys.stderr)
        if ah == 0x30:
            self.w(X.UC_X86_REG_AX, 0x1E03)  # DOS 3.30
            self.w(X.UC_X86_REG_BX, 0)
            self.w(X.UC_X86_REG_CX, 0)
        elif ah == 0x25:
            uc.mem_write(al * 4, struct.pack("<HH", dx, ds))
        elif ah == 0x35:
            off, seg = struct.unpack("<HH", uc.mem_read(al * 4, 4))
            self.w(X.UC_X86_REG_BX, off)
            self.w(X.UC_X86_REG_ES, seg)
        elif ah == 0x4A:
            if es == PSP_SEG:
                limit = min(self.blocks) if self.blocks else TOP_SEG
                if PSP_SEG + bx <= limit:
                    self.psp_end = PSP_SEG + bx
                else:
                    self.w(X.UC_X86_REG_BX, limit - PSP_SEG)
                    self.fail(8)
            elif es in self.blocks and bx <= self.blocks[es]:
                self.blocks[es] = bx
            else:
                self.w(X.UC_X86_REG_BX, self.blocks.get(es, 0))
                self.fail(8)
        elif ah == 0x48:
            top = min(self.blocks) if self.blocks else TOP_SEG
            if top - bx - 1 >= self.psp_end:
                seg = top - bx - 1
                self.blocks[seg] = bx
                self.w(X.UC_X86_REG_AX, seg)
            else:
                self.w(X.UC_X86_REG_BX, max(0, top - 1 - self.psp_end))
                self.fail(8)
        elif ah == 0x49:
            self.blocks.pop(es, None)
        elif ah == 0x3D:
            name = self.asciiz(ds, dx)
            path = self.host_path(name)
            if path is None:
                self.fail(2)
            else:
                h = self.next_handle
                self.next_handle += 1
                self.handles[h] = open(path, "rb")  # originals are never written
                self.w(X.UC_X86_REG_AX, h)
            if self.trace:
                print("  open %r -> %s" % (name, path), file=sys.stderr)
        elif ah == 0x3E:
            f = self.handles.pop(bx, None)
            if f:
                f.close()
        elif ah == 0x3F:
            if bx in self.handles:
                data = self.handles[bx].read(cx)
                uc.mem_write(ds * 16 + dx, data)
                self.w(X.UC_X86_REG_AX, len(data))
            else:
                self.w(X.UC_X86_REG_AX, 0)  # console: end of input
        elif ah == 0x40:
            data = bytes(uc.mem_read(ds * 16 + dx, cx))
            if bx in (1, 2):
                self.stdout += data
                self.w(X.UC_X86_REG_AX, cx)
            else:
                self.fail(5)  # no writes to files from this harness
        elif ah == 0x42:
            if bx in self.handles:
                pos = self.handles[bx].seek((cx << 16) | dx, al)
                self.w(X.UC_X86_REG_AX, pos)
                self.w(X.UC_X86_REG_DX, pos >> 16)
            else:
                self.fail(6)
        elif ah == 0x44:
            if al == 0:
                self.w(X.UC_X86_REG_DX, 0x80D3 if bx < 5 else 0x0000)
            else:
                self.fail(1)
        elif ah == 0x4C:
            self.exit_code = al
            uc.emu_stop()
        elif ah == 0x02:
            self.stdout.append(dx & 0xFF)
        elif ah == 0x09:
            s = bytearray()
            while (c := uc.mem_read(ds * 16 + dx + len(s), 1)[0]) != 0x24:
                s.append(c)
            self.stdout += s
        elif ah == 0x0B:
            self.w(X.UC_X86_REG_AX, 0x0B00)  # no character waiting
        elif ah == 0x19:
            self.w(X.UC_X86_REG_AX, 0x1902)  # drive C:
        elif ah == 0x1A:
            self.dta = (ds, dx)
        elif ah == 0x2A:
            self.w(X.UC_X86_REG_CX, 1990)
            self.w(X.UC_X86_REG_DX, 0x0101)
        elif ah == 0x2C:
            self.w(X.UC_X86_REG_CX, 0)
            self.w(X.UC_X86_REG_DX, 0)
        elif ah == 0x33:
            self.w(X.UC_X86_REG_DX, 0)
        elif ah == 0x43:
            if self.host_path(self.asciiz(ds, dx)) is None:
                self.fail(2)
            else:
                self.w(X.UC_X86_REG_CX, 0x20)
        elif ah == 0x47:
            uc.mem_write(ds * 16 + self.r(X.UC_X86_REG_SI), b"\0")
        elif ah == 0x4E:
            self.fail(18)
        elif ah == 0x62:
            self.w(X.UC_X86_REG_BX, PSP_SEG)
        else:
            raise Exit("unhandled int 21h ah=%02x al=%02x at pc 0x%05x" % (ah, al, self.pc()))

    def run(self, max_insns=200_000_000):
        try:
            self.uc.emu_start(self.entry, 0, count=max_insns)
        except UcError as e:
            raise Exit("%s at pc 0x%05x" % (e, self.pc()))
        if self.exit_code is None:
            raise Exit("instruction budget exhausted at pc 0x%05x" % self.pc())


    def scroll(self, up, lines, attr, cx, dx):
        top, left, bottom, right = cx >> 8, cx & 0xFF, min(dx >> 8, 24), min(dx & 0xFF, 79)
        cells = self.screen_cells()
        height = bottom - top + 1
        if lines == 0 or lines >= height:
            lines = height
        for r in range(top, bottom + 1):
            src = r + lines if up else r - lines
            for c in range(left, right + 1):
                ch, at = cells[src][c] if top <= src <= bottom else (0x20, attr)
                self.uc.mem_write(0xB8000 + 2 * (r * 80 + c), bytes([ch, at]))

    def screen(self):
        """The 80x25 text screen as the program left it (it prints through direct video writes)."""
        raw = bytes(self.uc.mem_read(0xB8000, 80 * 25 * 2))[0::2]
        rows = [raw[i:i + 80].decode("cp437").replace("\0", " ").rstrip() for i in range(0, 2000, 80)]
        while rows and not rows[-1]:
            rows.pop()
        return "\n".join(rows)

    def screen_cells(self):
        """The 80x25 text screen as (character, attribute) rows, straight from video memory."""
        raw = bytes(self.uc.mem_read(0xB8000, 80 * 25 * 2))
        return [[(raw[2 * (r * 80 + c)], raw[2 * (r * 80 + c) + 1]) for c in range(80)] for r in range(25)]


PIT_HZ = 1193182
# On the PC the player times a pause of n with the timer chip: n * 64 PIT ticks (0x156df),
# i.e. 53.6 us per unit against 45.45 us per sample. In samples at 22 kHz that is n * 1.18.
PAUSE_SCALE_PC = 64 * SAMPLE_RATE / PIT_HZ


def expand(events, pause_scale=1.0):
    """Events -> 8-bit unsigned PCM. pause_scale=1.0 is the nominal reading (pause n = n samples)."""
    out = bytearray()
    carry = 0.0  # fractions of a sample are carried to the next pause, as port/src/puhu.c does
    for kind, v in events:
        if kind == "s":
            out += v
        else:
            exact = v * pause_scale + carry
            carry = exact - int(exact)
            out += b"\x80" * int(exact)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--dir", default=os.path.join(here, "..", "original", "files"))
    ap.add_argument("--out", help="output prefix; without it only a summary is printed")
    ap.add_argument("--trace", action="store_true", help="log DOS calls to stderr")
    ap.add_argument("--calls", action="store_true", help="print the stage trace (see Dos.enable_trace)")
    ap.add_argument("--file", help="host text file (CP437) offered to the program as INPUT.TXT; "
                    "speak it with the tail ':INPUT.TXT'")
    ap.add_argument("tail", nargs="+", help="DOS command tail (UTF-8; converted to CP437)")
    a = ap.parse_args()

    tail = (" " + " ".join(a.tail)).encode("cp437")
    dos = Dos(os.path.join(a.dir, "PUHU.EXE"), a.dir, tail, trace=a.trace,
              extra_files={"INPUT.TXT": a.file} if a.file else None)
    if a.calls:
        dos.enable_trace()
    err = None
    try:
        dos.run()
    except Exit as e:
        err = str(e)
    if a.calls:
        for c in dos.calls:
            if c[0] == "F":
                print("F bp=%04x" % c[2])
                continue
            print(c[0], *[bytes.fromhex(x).decode("cp437") if isinstance(x, str) else x for x in c[1:]])
    pcm = expand(dos.events)
    n_s = sum(len(v) for k, v in dos.events if k == "s")
    n_p = sum(v for k, v in dos.events if k == "p")
    text = dos.stdout.decode("cp437")
    print("exit code   :", dos.exit_code, "" if err is None else "  STOPPED: " + err)
    print("stdout      :", repr(text))
    print("screen      :", repr(dos.screen()))
    print("events      : %d (%d samples in runs, %d pause samples in %d pauses)" % (
        len(dos.events), n_s, n_p, sum(1 for k, _ in dos.events if k == "p")))
    print("duration    : %.3f s at %d Hz" % (len(pcm) / SAMPLE_RATE, SAMPLE_RATE))
    print("port access :", {"%s %03x" % k: v for k, v in sorted(dos.ports.items())})
    if dos.unknown:
        print("unhandled   :", dos.unknown[:20])
    if a.out:
        with open(a.out + ".events.json", "w") as f:
            json.dump({"tail": tail.decode("cp437"), "exit_code": dos.exit_code, "error": err,
                       "sample_rate_nominal": SAMPLE_RATE,
                       "events": [[k, v.hex() if k == "s" else v] for k, v in dos.events]}, f)
        open(a.out + ".u8", "wb").write(pcm)
        open(a.out + ".stdout.txt", "w").write(text)
        open(a.out + ".screen.txt", "w").write(dos.screen() + "\n")
        with wave.open(a.out + ".wav", "wb") as wv:
            wv.setnchannels(1)
            wv.setsampwidth(1)
            wv.setframerate(SAMPLE_RATE)
            wv.writeframes(pcm)
    return 0 if err is None else 1


if __name__ == "__main__":
    sys.exit(main())
