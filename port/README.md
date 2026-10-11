# `puhu`: MikroPuhe 1.0 for Linux

A native re-implementation in C of the speech synthesiser in `PUHU.EXE` (Timehouse Oy, 1990), written from its disassembly. It reads the original voice files unchanged and produces the same sample stream as the original.

- How the synthesiser works: [`docs/how-it-works.md`](../docs/how-it-works.md)
- Text in, sound out, sound cards, SSH: [`docs/playback.md`](../docs/playback.md)
- The virtual PC speaker: [`docs/pc-speaker.md`](../docs/pc-speaker.md)
- How this port is checked: [`docs/verification.md`](../docs/verification.md)

## Build

```bash
make -C port            # port/build/puhu; needs a C99 compiler
make -C port install    # puts a link to it in ~/.local/bin (PREFIX=... to change)
```

The same sources build on Linux, on macOS ([`docs/macos.md`](../docs/macos.md)) and, cross-compiled with MinGW-w64, for Windows ([`docs/windows.md`](../docs/windows.md)):

```bash
make -C port windows    # port/build/mikropuhe-windows/: puhu.exe and the voices, a folder to copy to Windows
```
 If the ALSA development files are installed (pkg-config finds `alsa`), `puhu` plays through ALSA itself; `make ALSA=0` builds without, and on macOS that is what happens. Nothing else is linked. MP3 output calls `lame` or `ffmpeg` at run time, and playback without ALSA calls `pw-play`, `aplay`, `paplay`, `afplay` or `ffplay`.

## Use

```bash
puhu                                       # the original's full-screen interface: type, hear, F2 for settings
puhu "hyvää päivää"                        # play
echo "Hei Michael, tämä on KITT" | puhu -v sophia     # text piped in is spoken line by line as it arrives
puhu -o hei.wav "hyvää päivää"             # WAV
echo "hyvää päivää" | puhu > hei.wav       # the same: output redirected to a file or a pipe gets the WAV
puhu -o hei.mp3 -v sophia -s 4 -V 4 "..."  # MP3
puhu -f teksti.txt -o teksti.mp3           # a UTF-8 text file
echo "moi" | puhu -o - | aplay             # WAV to standard output
puhu -d pcspeaker --machine at "..."       # on the virtual PC speaker of a 286
puhu --list-devices                        # output devices, machines, ALSA devices
```

Text and voice:

| Option | Meaning |
|---|---|
| `-f FILE` | read the text from a file |
| `-v VOICE` | `janne` (default), `kristian`, `sophia`, or the path of a voice file (see `tools/smptool.py`) |
| `--ui`, `--lines` | with no text at a terminal `puhu` opens the full-screen interface ([`docs/interface.md`](../docs/interface.md)); `--lines` speaks typed lines instead, `--ui` asks for the interface explicitly |
| `-s N` | speed 0–8, 0 slowest |
| `-p N` | pitch 0–8, 4 neutral |
| `-r N` | pitch variation 0–8 |
| `-V N` | volume 0–8, 4 is unity gain |
| `--consonants 0\|1` | consonant emphasis |
| `--stress 0\|1` | first-syllable stress |
| `--cp437` | the text is already in code page 437; do not convert from UTF-8 |

The emulated output device, that is, what the speech is played *on*:

| Option | Meaning |
|---|---|
| `-d`, `--device dac` | (default) the samples as a D/A converter plays them: 8-bit, 22,000 Hz |
| `-d`, `--device pcspeaker` | the PC speaker the program was written for: its one-bit pulse train, rendered to 16-bit audio. See [`docs/pc-speaker.md`](../docs/pc-speaker.md). |
| `--machine NAME` | PC speaker: whose delay loops time the pulses: `ideal`, `486`, `386` (default), `at`, `xt` |
| `--timing L,O[,E]` | PC speaker: your own calibration values in nanoseconds |
| `--speaker cone\|none` | PC speaker: through a small loudspeaker (default), or the electrical signal |
| `--rate HZ` | PC speaker: output sample rate, 22050–96000, default 48000 |
| `--nominal-pauses` | DAC: a pause of *n* lasts *n* samples instead of the PC's *n* × 64 timer ticks (1.18 × *n*) |

Where the audio goes:

| Option | Meaning |
|---|---|
| `-o FILE` | write `FILE`: `.mp3` through lame or ffmpeg, `.m4a` (AAC) through macOS's afconvert or ffmpeg, anything else as WAV; `-` writes WAV to standard output |
| `-D`, `--alsa-device NAME` | play on this ALSA device, for example `plughw:1,0`. Without it: the ALSA device `default`, and if that cannot be opened, an external player. |
| `--play` | play even when standard output is a file or a pipe (which otherwise receives the WAV) |
| `--list-devices` | list the output devices, the machines and the ALSA devices |
| `--why-silent` | explain why nothing may be heard on this machine, and what to do about it |
| `--voices-dir DIR` | where `JANNE.SMP`, `KRISTIAN.SMP`, `SOPHIA.SMP` are |

Environment:

| Variable | Meaning |
|---|---|
| `PUHU_PLAY_CMD` | a player command of your own, tried before ALSA and the others; it gets the WAV on standard input |
| `MIKROPUHE_VOICES` | directory with the voice files, used when `--voices-dir` is not given |

All of the playing, piping and remote options are explained with examples in [`docs/playback.md`](../docs/playback.md).

Settings that are not given keep the values saved in the voice file. **Those are not gentle defaults**: all three voices are saved at volume 8, which clips heavily (it was meant for a PC speaker), and Janne and Sophia at speed 0, the slowest. `-s 4 -V 4` is a good place to start. On the PC speaker device, volume 8 is the authentic choice.

The voice files are looked for in `--voices-dir`, else in `$MIKROPUHE_VOICES`, else in `../../original/files` relative to the program itself (which is right for `port/build/puhu` in this repository, also when it is started through the installed link).

**Your own defaults.** Saving the settings with F2 in the full-screen interface copies the voice to `~/.local/share/mikropuhe/` (`$XDG_DATA_HOME/mikropuhe/`) and writes the settings into the copy, as the original wrote them into its voice file. A voice found there is used instead of the shipped one, so the saved settings are the defaults from then on, on the command line too. Delete the copy to go back. The voices directory itself is never written. With `--voices-dir` the directory is used exactly as it is, no copy is consulted or made, and saving is refused; the checks run that way.

Text from standard input is spoken line by line when it is played, so `tail -f file | puhu` works. With `-o`, all of it is read first.

**Hearing a remote `puhu`.** When `puhu` runs on a machine you reach over SSH, `port/puhu-listen` on your own machine plays what it says: `python3 puhu-listen --tunnel HOST` there, and `export PUHU_PLAY_CMD='nc -N 127.0.0.1 4714'` on the remote machine. The listener closes each connection only when the sentence has been played, so `puhu` waits as it would for a sound card, and the full-screen interface works. Steps and troubleshooting: [`docs/playback.md`](../docs/playback.md#3-play-by-itself-from-the-remote-terminal-through-an-ssh-tunnel).

How playing is attempted, in order: the command in `$PUHU_PLAY_CMD` if set (it gets the WAV on standard input); ALSA; `pw-play`, `aplay`, `paplay`, `afplay`, `ffplay`; and in a VS Code remote terminal, as a last resort, the recording is opened in the editor. On a machine reached over SSH there is usually no sound device to use: [`docs/playback.md`](../docs/playback.md#over-ssh-hearing-a-machine-you-are-not-sitting-at) has three ways to hear it anyway.

The original's own options work inside the text, exactly as they did in DOS:

```bash
puhu "tämä on hidasta /NOP8 ja tämä nopeaa /SOP ja tässä puhuu Sophia"
```

`/NOP /KOR /KRV /VOI` take a digit 0–8, `/KNP /ETP` 0 or 1, `/JAN /KRI /SOP` change the voice. The prefix can also be `+` or `-`, and an option is recognised anywhere, even inside a word.

### What to expect from the text handling

It is the original's, with its habits:

- Numbers are read as Finnish words, with ordinals for `3.` and case forms next to `-`. After the word "puhelin", and for numbers with a leading zero, digits are read one by one.
- Abbreviations in the voice's exception list are expanded (`esim.`, `klo`, `mk`, `jne.`); a word without vowels is spelled out.
- A sentence ends at `. : ? !` followed by a space or a line end.
- Of the punctuation marks only `,` and `;` produce a pause; sentences follow each other directly.
- Known oddities are kept: `1001` is read "tuhat", and digits separated by spaces are treated as one number. See the list in [`docs/original-program.md`](../docs/original-program.md#quirks-and-bugs-collected).

Input is converted from UTF-8 to code page 437. Characters with no CP437 equivalent become spaces.

## Source

| File | Lines | Content | Original |
|---|---|---|---|
| `src/mikropuhe.h` | | types and the core's interface | |
| `src/voice.c` | 64 | voice file loader | 0x14813 |
| `src/synth.c` | 668 | device boundary, sample generator, word stage (`mp_speak_phonemes`) | 0x13543–0x15114, 0x1551d, 0x1589a |
| `src/text.c` | 414 | character classes, exceptions, numbers, `mp_speak` | 0x126eb–0x132fe |
| `src/tables.h` | | number words and character tables, generated by `tools/gen_tables.py` | data segment |
| `src/puhu.c` | | the command: options, sentence cutting, inline options, UTF-8 | 0x10669–0x11047 |
| `src/out.c`, `out.h` | | output: the two emulated devices, WAV, MP3, ALSA, external players | |
| `src/pcspk.c`, `pcspk.h` | | the virtual PC speaker: pulse-width table, pulse train, rendering | 0x152b7, 0x15616, 0x156df |
| `src/ui.c`, `ui.h` | | the full-screen interface: screens, settings, the text-field editor; terminal and test backends | 0x111cf–0x11ebf, 0x1594c–0x163d6 |
| `src/ui_tables.h` | | the interface's strings and tables, generated by `tools/gen_ui_tables.py` | data segment |
| `tests/replay.c` | | replays traced word-stage calls through the core | |
| `tests/check_phonemes.py`, `tests/check_e2e.py` | | the two speech checks | |
| `tests/check_pcspk.py`, `tests/pcspk_dump.c` | | the PC-speaker check | |
| `tests/check_ui.py` | | the interface check: same keys into the original and the port | |
| `puhu-listen` | | for the machine with the loudspeakers: plays what a remote `puhu` says, through an SSH tunnel ([`docs/playback.md`](../docs/playback.md#3-play-by-itself-from-the-remote-terminal-through-an-ssh-tunnel)) | |
| `puhu-emu` | | the original `PUHU.EXE` in the emulator behind a similar command line (speech options and `-o`; its `--pc-speaker` is only the duty-cycle curve per sample) | |

Every function names the address of the routine it follows. The core has no I/O of its own:

```c
mp_voice v;  mp_state st;  char err[256];
mp_sink sink = { on_samples, on_pause, ctx };   /* your callbacks */

mp_voice_load(&v, "JANNE.SMP", err, sizeof err);
mp_init(&st, &v, sink);          /* settings start as saved in the voice */
st.set[MP_SPEED] = 4;            /* or copy 14 words and call mp_set() to validate them */
mp_speak(&st, "hyv\x84\x84 p\x84iv\x84\x84");   /* one sentence, CP437 */
mp_voice_free(&v);
```

`on_samples` receives runs of 8-bit unsigned samples at 22,000 Hz; `on_pause` receives a pause count in units of 64 PC timer ticks (53.6 µs). Cutting text into sentences and finding inline options is done by the command (`speak_text`, `speak_piece` in `puhu.c`), as in the original, not by the core.

### Why some of the code looks strange

- **16-bit arithmetic everywhere.** Durations, pitch offsets and the cross-fade are computed with the original's widths, shifts and rounding, because the output has to match sample for sample.
- **`goto` and variables named `v344`** in `mp_speak_phonemes`. The function follows the original's control flow; a variable whose meaning is not settled keeps its stack-frame offset as its name.
- **A "stack image" in `mp_state`.** The original reads uninitialised stack memory in two places. The port keeps the relevant stack area between calls and places the word stage's frame where the original's was for each caller (`MP_PATH_WORD`, `MP_PATH_NUMBER`, `MP_PATH_CHAR`), so that the same stale bytes are found. Comments marked "sic" show the two bugs.
- **A ring-buffer write position** in the device boundary, although there is no ring buffer. The original inserts one or two silence samples when a pause record would straddle the end of its 64000-byte buffer.

## Other voices

`-v NAME` loads `NAME.SMP` from your own voices, the voices directory, or the repository's `voices/`; `-v PATH` loads any voice file. `voices/KONE.SMP` is a synthetic voice made by `tools/make_synth_voice.py`; see [`docs/voice-format.md`](../docs/voice-format.md#kone-a-voice-with-nothing-recorded-in-it).

## Checks

```bash
make -C port check     # needs the Python environment in ~/tools/mikropuhe-venv, see tools/README.md
```

State on 2026-10-09:

- PC speaker: pulse-width table and player passes identical to the original for 12 calibrations; rendering checks pass.
- Interface: 26 key sequences and 100 random ones give the same screen, colours, cursor and speech as the original.
- Word stage: 68 of 68 cases byte-identical.
- End to end: 89 inputs, stage calls identical in all, sample stream identical in 86. `make check` therefore ends with an error for the three known cases; they differ by 1 to 30 samples and the reason is in [`docs/verification.md`](../docs/verification.md#the-three-streams-that-differ).

Sanitizer build: `make -C port build/puhu-asan`.

Across systems ([`docs/porting.md`](../docs/porting.md)):

```bash
make -C port fingerprint     # 156 cases through this build; must print what the Linux build prints. Needs only sh.
make -C port check-windows   # builds puhu.exe and runs it under Wine in a Docker container against the Linux build
```

- macOS: 156 of 156 cases and all sound files identical to Linux (run on the Mac, 2026-10-09).
- Windows: the same under Wine 9.0 and on a Windows 11 machine (2026-10-11, `tests/portable/run.ps1` and `edges.cmd` there), plus the Windows-only parts (code pages, `%APPDATA%`, the console interface). Used at that machine by the owner: "it worked perfectly".

## Not included

- In the full-screen interface: stopping the speech with a key, and the monochrome colour set.
- Driving a physical PC speaker. The virtual one is a model; see [`docs/playback.md`](../docs/playback.md#the-physical-pc-speaker) for what Linux offers.
- The resident-driver interface.
- Resampling for the DAC device: it stays at the original's 22,000 Hz, and ALSA or the player converts.

## Rights

The program and the voice files are © 1990 Timehouse Oy. This port is a private study. `src/tables.h` contains text taken from the program (the number words), and the port is useless without the original voice files. Publishing any of it needs the right holder's permission.
