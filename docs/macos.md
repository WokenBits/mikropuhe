# puhu on macOS

The same C sources build and run on macOS. This page says what was tested on a Mac, what is different there, how to set it up, and what is still only a plan.

## State (2026-10-09)

Built and run on the owner's Mac (macOS 12.7, x86_64, Apple clang 14.0.0, GNU Make 3.81; no Homebrew, no pkg-config), first from a scratch directory driven over SSH from the ai box, then installed from the project clone as version 1.1. The machine reports itself as a Mac; it is a Hackintosh with a 9500F processor.

The owner then ran the installed `puhu` there himself and reported that it "works just like in 1995", and later: "macOS version is also perfect". He also used the full-screen interface by hand in Terminal.app (a 226×46 window) and sent a screenshot: title bar, input box with its frame, key bar and the grey key reminder are all drawn correctly, in the right colours, while `afplay` speaks the greeting.

| Checked on the Mac | Result |
|---|---|
| `make -C port` | builds without warnings; no ALSA, nothing else to link |
| 90 speech inputs (the 68 reference cases and 22 others) with `--events`, and 66 key scripts for the full-screen interface, each compared with the Linux build's output | **156 of 156 identical** |
| WAV of one sentence from the DAC device, and from the virtual PC speaker (`--machine at`) | both files byte-identical to the Linux build's |
| voices found from the program's own location, started through a link from another directory | works (`_NSGetExecutablePath`) |
| playing one sentence (`--play`) | `afplay` returned 0 after 4 s for a 3.3 s sentence. The owner later confirmed that the native program works there. |
| text piped in, output redirected to a file | a WAV |
| `-o FILE.m4a` | AAC through the system's `afconvert`: 22,050 Hz, 3.2 s |
| `-o FILE.mp3` | refused, with a pointer to `.m4a`: the Mac has neither `lame` nor `ffmpeg` |
| ä ö å typed in decomposed form (a letter followed by a combining mark) | same speech as the composed letters |
| saving settings from the interface (F2) | a copy of the voice appears in the user's directory |
| the full-screen interface in a pseudo-terminal | alternate screen, title, box, typing, Ctrl-P to the settings, Esc restores the terminal |

Not tried: an Apple Silicon Mac; `make check` on the Mac (it needs the Python environment with Unicorn there).

Since the Linux build is checked against the original program, and the Mac build gives identical output for the same inputs, the Mac build inherits those results for the speech and the interface. The comparison is `port/build/puhu --events` and `--ui-script` on both machines.

## What is different on a Mac

| | Linux | macOS |
|---|---|---|
| Playing | ALSA, then external players | `afplay`, which is part of the system. (`$PUHU_PLAY_CMD` first, as everywhere.) |
| `-D`, `--alsa-device` | an ALSA device | not available |
| MP3 | `lame` or `ffmpeg` | only if one of them is installed; otherwise use `.m4a` |
| M4A (AAC) | `ffmpeg` | `afconvert`, built in |
| Finding the shipped voices | relative to `/proc/self/exe` | relative to the path from `_NSGetExecutablePath` |
| F1 and F2 in the interface | usually arrive | on Mac keyboards they need `fn`; Ctrl-A and Ctrl-P always work |
| `puhu --why-silent` | describes `/dev/snd` and groups | points at the output device and volume |

Everything else is the same code: the speech core, the virtual PC speaker, the interface, piping, your own copies of voices under `~/.local/share/mikropuhe/`.

Two changes were made to the sources for this, both small: the program's location is found per system (`program_dir` in `puhu.c`), and temporary files handed to players end in `.wav`. A third came out of testing on the Mac and matters everywhere: **saving never writes into the voices directory** any more, whatever the file permissions say. On ai the originals are read-only files; in the Mac's clone they are ordinary writable files, and a test run with `--voices-dir` rewrote `JANNE.SMP` there (with the same bytes: its checksum still matched, only its date changed). Now a shipped voice is always copied to the user's directory before settings are written, and with `--voices-dir` saving is refused.

## Setting it up on the Mac

The Mac's clone of the project pulls from ai (`git pull ai main` in `~/Projects/rea/project`), so the work has to be committed on ai first.

```bash
# on the Mac
cd ~/Projects/rea/project && git pull ai main
make -C mikropuhe-1.0/port            # builds mikropuhe-1.0/port/build/puhu
make -C mikropuhe-1.0/port install    # a link in ~/.local/bin, which is on the PATH there
puhu -s 4 -V 4 "hyvää päivää"
puhu                                  # the full-screen interface
```

With `puhu` running on the Mac itself there is no need for the SSH tunnel and `puhu-listen`; those are for hearing the copy that runs on ai.

## Plan

| | What | State |
|---|---|---|
| 1 | Build and run from the same sources | done, see above |
| 2 | Same output as on Linux | done: 156 of 156 |
| 3 | Play through the system | done with `afplay`; needs your ears once |
| 4 | Compressed output without extra software | done: `.m4a` |
| 5 | Install from the Mac's clone | done: pulled to the 1.1 merge, `make install`, same output as the ai build for all 156 cases |
| 6 | Use the interface by hand in Terminal.app and iTerm | done in Terminal.app (two screenshots, 2026-10-09): main screen and settings screen, frame characters and colours right, values changed with `+` and `-` and saved with F2 into `~/.local/share/mikropuhe/JANNE.SMP` (the shipped file untouched). Not reported: whether `fn`-F1/F2 reach the program, Option as Meta for Alt-1/Alt-2. iTerm untried. |
| 7 | `make check` on the Mac | optional. Needs `python3 -m venv` with `unicorn` and `capstone`; the checks themselves are plain Python |
| 8 | Play through Core Audio directly instead of `afplay` | optional. It would remove the temporary file and the short start-up delay per sentence, and allow stopping speech with a key. About a hundred lines with AudioQueue, linked with `-framework AudioToolbox`, behind `#ifdef __APPLE__` as ALSA is behind `HAVE_ALSA`. |
| 9 | Apple Silicon, and a genuine Intel Mac | untested; the one Mac it has run on is a Hackintosh. Nothing in the code depends on the processor: the arithmetic is fixed-width, the files are read byte by byte. The floating-point rendering of the virtual PC speaker is the one place where a different processor could round differently. |
| 10 | A double-clickable app or a Homebrew formula | not planned. Publishing anything needs the right holder's permission first (see the README). |

## What was done on the Mac

So that it is on record: a scratch directory `/tmp/puhu-mac` was created for the tests (sources, the build, test outputs, a link to the clone's `original/`) and removed afterwards; one sentence was played aloud; the date of `original/files/JANNE.SMP` in the clone changed, as described above. Then, with the owner's agreement, the project clone was pulled forward to the 1.1 merge and `make install` put a link in `~/.local/bin`.
