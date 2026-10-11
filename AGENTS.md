# Guidelines for agents working on this project

For a coding agent (or a person) about to change something here. The README says what the project is; this page says how to work in it without breaking what it promises.

## What the project promises

1. **`puhu` says what the original said.** For the same text, voice and settings, the port produces the sample stream `PUHU.EXE` produces. This is checked, not assumed.
2. **The full-screen interface is the original's**, cell for cell, colour for colour.
3. **Every system behaves the same.** Linux, macOS and Windows builds give identical output for identical input.
4. **Nothing of Timehouse Oy's is in the repository.**

A change that weakens one of these needs a reason and the owner's agreement.

## Never

- **Never add `PUHU.EXE`, the original voices (`JANNE.SMP`, `KRISTIAN.SMP`, `SOPHIA.SMP`), or listings and dumps made from them.** `original/files/` is for your local copies; `.gitignore` keeps them out. The same holds for `port/src/tables.h` and `port/src/ui_tables.h`, which the build generates from `PUHU.EXE`.
- **Never modify the files in `original/files/`.** Check them with `cd original && sha256sum -c SHA256SUMS`.
- **Never "fix" the original's behaviour in the Finnish mode.** Two off-by-one bugs and several reads of uninitialised memory are reproduced on purpose (`mp_state.stack`, `mp_frame`): they are why 86 of 89 streams match byte for byte. Read "Why some of the code looks strange" in [`port/README.md`](port/README.md) first.
- **Never edit a generated file by hand.** `tables.h` and `ui_tables.h` come from `tools/gen_tables.py` and `tools/gen_ui_tables.py`; `voices/KONE.SMP` comes from `tools/make_synth_voice.py`; `tests/portable/expected.txt` comes from the Linux build.
- **Never claim that something works without the check that shows it**, and say which checks you could not run.

## Setting up

```bash
# 1. your own copies of the four original files
cp /where/you/have/them/{PUHU.EXE,JANNE.SMP,KRISTIAN.SMP,SOPHIA.SMP} original/files/
(cd original && sha256sum -c SHA256SUMS)

# 2. build (a C99 compiler, make, and Python 3 once for the generated headers)
make -C port

# 3. for the checks against the original: a Python environment with Unicorn and capstone
python3 -m venv ~/tools/mikropuhe-venv
~/tools/mikropuhe-venv/bin/pip install unicorn capstone numpy
```

## After every change

| Run | Needs | Shows |
|---|---|---|
| `make -C port` | compiler | builds without a single warning (`-Wall -Wextra -Wconversion`) |
| `make -C port fingerprint` | only `sh` | 156 inputs still give the recorded output. Fast; run it always. |
| `make -C port check` | the Python environment, the original files | the port against the original in the emulator: PC-speaker tables, word stage, interface, end to end |
| `make -C port check-windows` | MinGW-w64, Docker | the Windows build under Wine against the Linux build |
| `make -C port build/puhu-asan` | compiler | a build that stops on memory errors; run your new case through it |

`make check` ends with an error today, by design: three end-to-end cases (`jan-s07-default`, `kri-s07-default`, `extra-07`) differ from the original by 1 to 30 samples, and the reason is written down in [`docs/verification.md`](docs/verification.md#the-three-streams-that-differ). Three known failures and one skip is the passing state. A fourth is yours.

If you changed behaviour **on purpose**: get `make check` back to that state first, then record the new output with `sh tests/portable/run.sh build/puhu ../original/files > tests/portable/expected.txt` (from `port/`), and say in the commit what changed and why.

## Where things go

| Kind of code | Where | Rule |
|---|---|---|
| speech: text to samples | `port/src/voice.c`, `synth.c`, `text.c` | no system calls, no floating point, fixed-width integers. Follows the original routine by routine; the comments carry the original's addresses (`0x13543`). Keep them. |
| what depends on the operating system | `port/src/out.c`, `puhu.c`, `ui.c` | behind `#ifdef _WIN32` / `__APPLE__` / `HAVE_ALSA`. A new system gets a branch here and nowhere else ([`docs/porting.md`](docs/porting.md)). |
| the virtual PC speaker | `port/src/pcspk.c` | the pulse-width table is the original's and is checked; the machines and the loudspeaker are a model and are labelled as estimates. Keep that line visible. |
| anything new that the original did not do | behind an option | the default behaviour stays the original's |
| tools | `tools/` | plain Python 3; say in `tools/README.md` what each needs |

## Things that will trip you

- **`puhu` writes a WAV to standard output when that is a file or a pipe.** In a script, a test or over SSH, give `-o FILE` or `--play`, or the sound data lands in your output.
- **The interface discards keys typed while it speaks.** A scripted test must wait for the speech to end, or use `--ui-script`, which has no sound.
- **`-v k…` means Kristian** unless a voice file of that name exists; `-v kone` is the synthetic voice.
- **The voices are saved at volume 8 and speed 0.** Volume 8 clips. Tests that look at loudness should set `-V 4`.
- **zsh does not split a variable into words.** Put command lines into functions, not variables; the test scripts are `sh` for this reason.
- **The decompiler's pseudocode is a hint, not the truth.** Where the port and the pseudocode disagree, read the instructions ([`docs/method.md`](docs/method.md)).

## Writing things down

- Notes keep **observed**, **inferred** and **unknown** apart, and an observation names the command or file that shows it.
- Tool output that supports a claim is saved under `evidence/`, as a file, not retyped.
- Documentation is written for a reader who was not there: full sentences, the number with its unit, and what was *not* checked next to what was.
- A commit message says what changed and why, and which checks were run.

## Ideas that are welcome, and where they are described

In [`port/PLAN.md`](port/PLAN.md): tuning Kone by ear, a voice from someone's own recordings, tables of our own so that nothing needs `PUHU.EXE`, an English mode behind `--language`, a new DOS build, stopping speech with a key.
