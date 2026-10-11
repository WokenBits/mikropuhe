# Tools

Scripts written for this target. They are run from the target directory (`mikropuhe-1.0/`). The ones marked *venv* need the Python environment with Unicorn and capstone:

```bash
P=~/tools/mikropuhe-venv/bin/python          # on ai; create with:
uv venv ~/tools/mikropuhe-venv && uv pip install --python ~/tools/mikropuhe-venv/bin/python unicorn capstone numpy
```

| Tool | Needs | Purpose |
|---|---|---|
| `puhu_emu.py` | venv | Run the original `PUHU.EXE` in a CPU emulator and capture its output |
| `make_reference.py` | venv | Generate the 68-case reference set and its manifest |
| `smpdump.py` | Python 3 | Dump a voice file completely |
| `mzdis.py` | venv | Disassemble an address range of the program |
| `fn_extract.py` | Python 3 | Reduce REA/Ghidra function envelopes to text files |
| `gen_tables.py` | Python 3 | Generate `port/src/tables.h` from the program's data |
| `pcspk_oracle.py` | venv | Run the original's PC-speaker table builder and player for given calibration values |
| `ui_explore.py` | venv | Drive the original's full-screen interface with scripted keys and show the screen |
| `smptool.py` | Python 3 | Unpack a voice file into editable files, and pack it again |
| `make_synth_voice.py` | Python 3 | Compute the synthetic voice Kone, as a directory for `smptool.py pack` (`make -C port kone`) |
| `gen_ui_tables.py` | Python 3 | Generate `port/src/ui_tables.h` (interface strings and tables) from the program's data |

## `puhu_emu.py`: the original in an emulator

```bash
$P tools/puhu_emu.py -- "hyvää päivää"                        # summary only
$P tools/puhu_emu.py --out /tmp/x -- "/KRI/NOP5 hyvää päivää"  # + /tmp/x.wav .u8 .events.json .screen.txt
$P tools/puhu_emu.py --calls -- "puhelin 123"                  # the stage trace
$P tools/puhu_emu.py --file teksti.cp437.txt -- ":INPUT.TXT"   # speak a file (CP437 text)
$P tools/puhu_emu.py --trace -- "moi"                          # log every DOS call
```

Everything after `--` is the DOS command tail, given in UTF-8 and converted to CP437.

How it works:

- The MZ image is loaded at segment 0x1000 with relocations applied; a PSP, an environment block and the command tail are set up as DOS would.
- Software interrupts are served in Python: the int 21h functions the program actually calls (version, vectors, memory, open/read/seek/close, console output, exit), and a few BIOS calls (video mode, cursor, equipment, memory size, keyboard status, time). Port 0x3DA toggles so that the console library's retrace wait ends. Other ports read 0 and ignore writes.
- No hardware interrupt is ever delivered, so the program's timer and keyboard handlers never run.
- Three patches in the device layer replace calibration, table building and the PC-speaker player. The player hook empties the ring buffer into `Dos.events`.
- File opens are mapped by base name to `original/files/` (read-only) or to the file given with `--file`. Writes to files are refused.

Use it as a library for anything else:

```python
import sys; sys.path.insert(0, "tools")
from puhu_emu import Dos, expand
dos = Dos("original/files/PUHU.EXE", "original/files", " /JAN moi".encode("cp437"))
dos.enable_trace()          # optional: fills dos.calls, dos.stack_image
dos.run()
dos.events                  # [["s", bytes], ["p", n], ...]
pcm = expand(dos.events)    # 8-bit unsigned; expand(events, PAUSE_SCALE_PC) for PC pause timing
dos.screen()                # the 80x25 text screen at exit
```

To hook another address, add a `uc.hook_add(UC_HOOK_CODE, fn, begin=addr, end=addr)` the way `enable_trace` does; `Dos.stack_word(n)` reads the n-th word on the stack at a far function's entry (2 = first argument).

Limits: the full-screen interface is not driven (no keys are queued; a wait for a key stops the run); one run speaks one command line; a run is stopped after 200 million instructions.

## `make_reference.py`: the reference set

```bash
$P tools/make_reference.py                 # writes evidence/reference/*.events.json and MANIFEST.tsv
$P tools/make_reference.py --wav /tmp/ref  # also WAV files for listening
```

`MANIFEST.tsv` (in git) lists each case with its command tail, sample count, pause count and the SHA-256 of the stream with pauses expanded to their count in samples. The event files are regenerable and ignored by git. The case list (`SENTENCES`, `VOICES`, `VARIATIONS`) is also what `port/tests/` iterates over.

## `smpdump.py`: voice files

```bash
tools/smpdump.py original/files/JANNE.SMP                  # container, settings, parameters, summaries, coverage
tools/smpdump.py original/files/JANNE.SMP --exceptions     # all 149 exception entries
tools/smpdump.py original/files/JANNE.SMP --units          # every unit and variant with its length
tools/smpdump.py original/files/JANNE.SMP --wav /tmp/units # each variant as a WAV file
```

The format is described in `docs/voice-format.md`. The last lines of the output say how many bytes the format explains and list the ranges it does not.

## `mzdis.py`: disassembly

```bash
$P tools/mzdis.py original/files/PUHU.EXE 0x13543 0x13ce9   # a range
$P tools/mzdis.py original/files/PUHU.EXE 0x1451e           # from an address to the first return
```

Intel syntax, linear addresses matching Ghidra's, far-call targets annotated with their linear address. It is a linear sweep: start at an instruction boundary and stop before data (jump tables sit in the code segments). `mzdis.load()` returns the relocated image for other scripts.

## `fn_extract.py`: Ghidra output to text

```bash
R=~/projects/rea/.claude/bin/rea-dev; T=$PWD/original/files/PUHU.EXE
$R function "$T" 0x1544a --provider ghidra --json > /tmp/fn/0x1544a.json      # one envelope per procedure
$R instructions "$T" 0x13543 --provider ghidra --json > /tmp/ins/0x13543.json  # where `function` fails
python3 tools/fn_extract.py /tmp/fn evidence/functions /tmp/ins
```

Writes `evidence/functions/<address>.txt` (body range, callers, callees, instruction list, pseudocode) and `INDEX.tsv`. The envelopes are about 300 KB each and are not kept. To regenerate all 262, loop over the addresses in `evidence/procedures.json`; with 18 calls in parallel it took about 10 minutes on ai.

## `gen_tables.py`: data tables for the port

```bash
python3 tools/gen_tables.py > port/src/tables.h
```

Reads the number words, the letter and special-character lists, the letter rewrites and a few strings from the program's data segment and writes them as C. The header in the repository is this script's output.

## `pcspk_oracle.py`: the original's PC-speaker code

```bash
$P tools/pcspk_oracle.py 400 2500        # loop time and overhead in ns, as the calibration would store them
```

Executes two routines of `PUHU.EXE` unpatched: the table builder (0x152b7) with the given values at DS:0x1dd4 and DS:0x1dd8, then the player (0x15616) over a buffer holding the sample values 1 to 255. Prints the 257 table words and, per sample value, the number of `LOOP` passes the player made with the speaker on and off (counted at 0x1565c and 0x15667 between writes to port 0x61). `port/tests/check_pcspk.py` compares this with the C port. Unlike `puhu_emu.py` it sets up no DOS: it calls the two routines directly on a scratch stack.

## `ui_explore.py`: the original's full-screen interface

```bash
$P tools/ui_explore.py                                   # the start screen, and what it says
$P tools/ui_explore.py F2 DOWN DOWN + ENTER text:moi ENTER
$P tools/ui_explore.py --attr F2                         # with the colour attributes per row
$P tools/ui_explore.py --raw F1                          # JSON: screen cells, cursor, speech
```

Keys: `F1 F2 ESC ENTER UP DOWN LEFT RIGHT HOME END BS DEL CTRL-LEFT CTRL-RIGHT CLEAR TAB + - SPACE PGUP PGDN INS`, and `text:...` for typed characters. The original is started without arguments and runs until it waits for a key that is not there. The screen is read from the emulated video memory.

Keys are handed over only when the program asks for one and none is pending (`Dos.typed_late`), because the interface empties the keyboard buffer after speaking. `port/tests/check_ui.py` uses the same function to compare the port's interface with the original's.

## `smptool.py`: editing voices

```bash
tools/smptool.py unpack original/files/JANNE.SMP /tmp/janne
tools/smptool.py pack /tmp/janne /tmp/JANNE2.SMP
tools/smptool.py roundtrip original/files/JANNE.SMP       # unpack + pack must give the identical file
```

The directory layout and what may be edited are in `docs/voice-format.md`, under "Editing a voice". `pack` checks what the program relies on: unit and variant numbers without gaps, table 3 units exactly one pitch period long, keys and replacements of at most 255 bytes, blocks below 64 KB.

## `gen_ui_tables.py`: the interface's strings

```bash
python3 tools/gen_ui_tables.py > port/src/ui_tables.h
```

The key bars, prompts, spoken texts, the table of settings rows, the frame characters and this copy's serial number, read from the program's data segment.
