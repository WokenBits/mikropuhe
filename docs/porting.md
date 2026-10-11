# Taking `puhu` to another system: the working methods

`puhu` was written on Linux, then taken to macOS, then to Windows. This page describes how that is done and checked, so that the next system (a new DOS build is planned) can go the same way. The results for each system are on its own page: [macos.md](macos.md), [windows.md](windows.md).

## The chain

A build on a new system is never compared with the original program directly. It is compared with the Linux build, which is compared with the original:

```
PUHU.EXE in the emulator ──► Linux build ──► macOS build, Windows build, ...
        make check                 same output for the same inputs
        (verification.md)          (this page)
```

So a new build needs no emulator, no Python and no reference files on its machine. It needs a shell and a hash program.

## Method 1: the system stays at the edges

The speech core (`voice.c`, `synth.c`, `text.c`) calls nothing but the C library's memory and file functions. Everything that depends on the system sits in three files:

| File | What depends on the system |
|---|---|
| `out.c` | playing, starting other programs, temporary files |
| `puhu.c` | where the program and the user's files are, arguments, what standard output is connected to |
| `ui.c` | reading keys, the size of the window, setting the terminal up and restoring it |

A new system gets a branch (`#ifdef __APPLE__`, `#ifdef _WIN32`) in those files and nowhere else. macOS needed about 30 lines. Windows needed about 350, most of them for the console and for text encodings.

The first step on a new system is therefore to give each source file to its compiler and read the complaints. For Windows the core produced none; `pcspk.c` missed one constant; the three edge files listed exactly the functions that had to be replaced (`mkstemps`, `getuid`, `realpath`, `getline`, a two-argument `mkdir`, `termios`).

## Method 2: the program can print what it does

Two options exist only for checking. They turn behaviour into text:

| Option | Prints |
|---|---|
| `--events -- TEXT` | the speech as text: runs of samples and pauses, as the device would get them |
| `--ui-script "KEYS"` | the full-screen interface after these keys: all 2,000 characters, their colours, the cursor, and what was spoken |

Text can be hashed, and a hash can be compared across machines without copying sound files or looking at screens.

## Method 3: one list of cases, one line per case

`port/tests/portable/` holds the cases and the script:

| File | What it is |
|---|---|
| `cases.tsv` | 156 cases: 90 texts (the 68 reference sentences of `make check` and 22 others) and 66 key sequences for the interface |
| `run.sh PUHU VOICESDIR` | runs every case and prints `kind name hash`. Plain `sh`; uses `sha256sum` or `shasum`. |
| `expected.txt` | what the Linux build prints |

```bash
make -C port fingerprint                                         # on Linux, and on any system with make
sh tests/portable/run.sh build/puhu VOICESDIR | diff - tests/portable/expected.txt    # by hand
```

No output from `diff` means the build behaves like the Linux build in all 156 cases. A difference names the case.

`expected.txt` has to be made again when the port's behaviour is changed on purpose: `sh tests/portable/run.sh build/puhu ../original/files > tests/portable/expected.txt`, after `make check` has shown that the Linux build still agrees with the original.

## Method 4: the edges get cases of their own

The 156 cases test the shared code as compiled by the new compiler. They do not test the edge code, which is what was newly written. `tests/portable/edges.sh` does: it makes sound files in the ways a user would and prints a hash of each.

| Case | What it exercises |
|---|---|
| `-o FILE` | writing a file |
| `> FILE` | the redirect rule, and that the bytes arrive unchanged |
| text piped in | reading standard input |
| a file name with ä and ö | file names |
| the virtual PC speaker, two settings | the floating-point arithmetic of the filter (the only floating point in the program) |
| on Windows only: text in code pages 850 and 1252 | reading text that is not UTF-8 |

The hashes must be the same on every system. So far they are, on all three (`evidence/windows/edges-*.txt`).

## Method 5: running a build that does not run here

Development happens on the ai box (Linux). Two ways have been used to run another system's build from there.

**A real machine over SSH** (the Mac). The sources go into a scratch directory, are built with the machine's own compiler, checked, and removed:

```bash
cd port
ssh MAC 'rm -rf /tmp/puhu-mac && mkdir -p /tmp/puhu-mac'
tar cf - Makefile src tests/portable | ssh MAC 'tar xf - -C /tmp/puhu-mac'
ssh MAC 'cd /tmp/puhu-mac && make build/puhu &&
         sh tests/portable/run.sh build/puhu ~/Projects/rea/project/mikropuhe-1.0/original/files |
         diff - tests/portable/expected.txt && echo same; rm -rf /tmp/puhu-mac'
```

Nothing is installed there, and the voices are only read.

**Wine in a throwaway container** (Windows). The Windows program is built on ai with the MinGW-w64 cross-compiler and run by Wine inside a Docker container that has no network and sees the program and the voices read-only:

```bash
make -C port check-windows
```

This builds `puhu.exe`, builds the image `puhu-wine` on first use (`tests/wine/Dockerfile`: Ubuntu 24.04 with its `wine64` package), and runs `tests/wine/check.sh`: the 156 cases, the edge cases, saving settings, and the interface. The interface is driven through a pseudo-terminal by `tests/wine/ui_smoke.py`, which types keys and looks for words in what comes back.

Wine is installed nowhere on ai itself; the container is the installation. To get the space back: `docker rmi puhu-wine` (1.6 GB).

**A real Windows over SSH.** Windows 11 has an SSH server but no `sh`, so the two scripts exist a second time in Windows' own languages: `tests/portable/run.ps1` (PowerShell; prints the same lines as `run.sh`) and `tests/portable/edges.cmd` (a `cmd` batch file, stored in code page 850 so that `cmd` reads its ä and ö the way it reads typed ones). Files travel as a `tar` stream, which Windows 11 unpacks with its own `tar`. From ai the Windows machine is reached through the Mac, which has the key for it:

```bash
win() { ssh MAC "ssh WINDOWS $(printf '%q' "$*")"; }
tar cf - mikropuhe-windows | win 'tar xf - -C %USERPROFILE%'
win 'powershell -NoProfile -ExecutionPolicy Bypass -File run.ps1 PUHU.EXE VOICESDIR cases.tsv' | tr -d '\r' | diff - tests/portable/expected.txt
```

The interface was driven there the same way as under Wine: a pseudo-terminal on ai, `ssh -tt` through both machines, keys in, screen text out. An SSH session on Windows gets a real Windows console, so this exercises the same system calls a window would.

**Planned for DOS:** FreeDOS under `qemu-system-i386`, with QEMU writing the emulated sound device to a file.

## What each kind of run shows

| | Shows | Does not show |
|---|---|---|
| The 156 cases | the shared code behaves identically when built by this compiler for this system | anything about sound devices, real keyboards, real windows |
| The edge cases | files, pipes, redirection, text encodings, floating point | playing |
| Wine | that Windows code runs and does what was intended, as far as Wine implements Windows the same way | how the real Windows console draws and which keys it delivers; sound (the container has no sound device) |
| A real machine over SSH | everything above for that machine, that the system accepts the sound, and that its console delivers keys and takes the drawing | what it sounds like; what the interface looks like in a window |
| A person at the machine | sound and looks | |

The last row cannot be replaced. For macOS, the owner's ears and two screenshots closed it. For Windows, the owner used it at the machine and reported that it worked perfectly.

## Traps that cost time once

| Trap | What happened | What is done now |
|---|---|---|
| zsh does not split a variable into words | `$V` holding `--voices-dir DIR` reached the program as one argument, and the Linux side of a comparison silently produced usage text instead of sound files | shell functions instead of variables holding commands; the scripts are `sh` |
| Over SSH, standard output is a pipe | `puhu "text"` then follows its redirect rule and writes the WAV into the pipe, that is, into the output being read | `--play` or `-o FILE` in every scripted call |
| Docker keeps Ctrl-P | The interface's Ctrl-P seemed to arrive one key late. Docker's client was holding it back: it begins Docker's detach sequence. | `--detach-keys` set to something else in `ui_smoke.py` |
| Docker creates a mounted folder that is missing, owned by root | a wrong path in the check script left empty root-owned folders in the source tree | `check.sh` refuses to start unless the folders exist |
| The Mac's clone has writable voice files | a test run with `--voices-dir` rewrote `JANNE.SMP` there (same bytes, new date) | the program never writes into the voices directory, on any system |
| On Windows, standard output changes line ends | would have corrupted WAVs and changed every hash | standard output is switched to binary at start |
| Windows has no single text encoding | `echo ä \| puhu` delivers code page 850 from `cmd`; arguments arrive in the "ANSI" code page | arguments are taken as UTF-16 from the system; piped text that is not valid UTF-8 is read in the console's code page |
| `long` is 32 bits on 64-bit Windows | nothing: the core uses fixed-width types throughout | the 156 cases would show it |
| The interface throws away keys typed while it speaks | a scripted test typed during the greeting and the letters never appeared. That is the original's behaviour, and on a machine that really plays sound the greeting takes its 10 seconds. | scripted keys wait until the speech is over |
| The Mac has Make 3.81 and no `pkg-config` | nothing broke, but the Makefile must not use newer features | ALSA is detected with a fallback to "no" |

## Checklist for the next system

1. Give every source file to the new compiler (`-fsyntax-only`). The core should pass untouched; the complaints in the edge files are the work list.
2. Write the edge branch. Do not touch the core.
3. Build the Linux program again and run `make check` and `make fingerprint`: the existing systems must be unchanged.
4. Find a way to run the new build: the real machine, or an emulator in a container.
5. `run.sh` must print `expected.txt`. Then `edges.sh`.
6. Exercise what only that system has (its console, its way of playing, its folders).
7. Write down what was run and what was not, on the system's page under `docs/`, with the outputs in `evidence/`.
8. Ask a person to listen and look.
