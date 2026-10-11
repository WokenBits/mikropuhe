# How this was reverse-engineered

A short account of the route from a zip file to a checked native port, written so that the next target goes faster. The dated log with every command is [`notes/NOTES.md`](../notes/NOTES.md); this is the distilled version.

## The route

| Step | What was done | What it gave |
|---|---|---|
| 1. Triage | Checksums, `rea inspect`, procedure and string lists, a linear disassembly to grep for `in`/`out`/`int` | Format, compiler, where the hardware is touched |
| 2. Read the hardware end first | The handful of procedures with port I/O | The output is plain 8-bit samples in a ring buffer; the PC speaker is only the last few hundred bytes of code |
| 3. Build an oracle | The unmodified program in a CPU emulator, cut at the ring buffer (`tools/puhu_emu.py`) | The exact output for any input in 0.15 s, repeatable. A usable speech command the same day (`port/puhu-emu`). |
| 4. Listen | Three MP3s to a human | Confirmation that the captured stream is the speech, before building on it |
| 5. Trace the stages | Hooks at the internal boundaries: every word handed to the word stage, with the state it depends on | A second, finer oracle: the text stage and the word stage can be checked separately |
| 6. Port bottom-up | Sample generator and word stage first, replayed against the traced calls; then the text stage, compared call for call | Each layer proven before the next was written |
| 7. Explain every difference | Three of 89 streams differed; the stack images were compared byte by byte | The cause (uninitialised memory), not a shrug |
| 8. Close the format | `tools/smpdump.py` accounts for every byte of the voice files | The parts the program never reads are known as such |

Steps 3 and 5 carried the project. Reading 20 KB of 16-bit machine code would have produced a plausible port; only an oracle shows whether it is the same program.

## What worked

- **Cut the original at a data boundary, not at the hardware.** Emulating a PC speaker driven by calibrated busy-wait loops would have been slow and inexact. The ring buffer one level up is pure data.
- **Patch as little as possible, and only below the cut.** Three patches, all in the device layer. Everything being ported kept running as original code.
- **Make the oracle fine-grained.** With word-level traces a wrong sample count points at one word in one case, and the stage-call comparison tests the text handling without any audio at all.
- **Feed the original's inputs to one function at a time.** `tests/replay.c` runs the C word stage on the original's traced arguments. That is how the stale-stack bug was found: 10 of 68 cases passed, the failures all began at a word-initial `m`, `n`, `v` or `l`, and the frame bytes explained the rest.
- **Keep the original's layout where it leaks.** Three work arrays of the word stage sit in one block at their original frame offsets, and the frame sits in a stack image at a caller-dependent position. That is ugly C and the reason 86 streams match instead of 10.
- **Write C while reading assembly.** The 1,200-instruction function was transcribed chunk by chunk with the addresses in comments, not understood first and written later.
- **Unoptimised compiler output is regular.** Turbo C 2.0 code maps back to C almost line by line: `[bp+6]` is the first argument, `[bp-n]` a local, `si` and `di` the register variables.

## What to watch for

- **The decompiler silently drops code.** Ghidra treats the initialised data segment as constant, folds tests on globals that live there and deletes the "dead" branch. `lib_speak` decompiled to four lines. For such functions only the instruction list counts.
- **Switch tables hide code.** `JMP CS:[BX+table]` targets were not followed. Function bodies had holes of several hundred bytes, and one helper looked uncalled. A relocation-aware linear disassembly of the whole range (`tools/mzdis.py`) fills them.
- **Data references in the pseudocode are wrong.** A pushed `DS:0x14d` is shown as some other string plus an offset. Read the constant from the instruction and look it up at the data segment.
- **Call arguments are dropped.** Far calls with five arguments appeared with one. Count the pushes.
- **`rea function` fails as a whole when the decompiler throws**, although `rea instructions` works on the same function. Three functions, all with switch tables.
- **Uninitialised memory is part of the behaviour.** If the output must match exactly, the garbage has to match too, or the mismatch has to be traced to it.
- **Defaults lie.** The voices are saved at volume 8 and speed 0, an out-of-range digit silently becomes 4, and the sentence pauses are switched off by a buffer-size test. None of that is visible without running the thing.

## Tools used

| Tool | For |
|---|---|
| REA 6.2.0 with Ghidra 12.1.4 (`.claude/bin/rea-dev`) | inventory, per-procedure instruction lists, callers and callees, pseudocode |
| capstone (`tools/mzdis.py`) | linear disassembly with relocations applied, for the ranges Ghidra skipped |
| Unicorn (`tools/puhu_emu.py`) | running the original; hooks for the stage traces |
| gcc with `-Wall -Wextra -Wconversion`, ASan, UBSan | the port |
| sox, lame | spectrogram for a first sanity check, MP3s for listening |

Ghidra analysis is about 20 s per call here, so all 262 procedures were analysed once in parallel and reduced to one text file each (`tools/fn_extract.py` → `evidence/functions/`). After that, reading is `grep` and `sed`.

## Other systems

How the finished port is taken to macOS and Windows, and checked there without the emulator, is a method of its own: [porting.md](porting.md).

## Rules that held

From the project README, and worth keeping for the next target:

- `original/` is never modified and is checksummed.
- Tool output that supports a claim is saved under `evidence/`.
- Notes keep **observed**, **inferred** and **unknown** apart. Several early inferences were wrong in detail (the timer was thought to clock every sample; it only times pauses) and were easy to retire because they had been marked as inferences.
- A claim of "works" needs the check that was run and its result, including the failures.
