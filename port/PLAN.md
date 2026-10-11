# Port plan: MikroPuhe 1.0 on Linux

## Target

A command-line program, `puhu`, that behaves like the original for speech and adds modern output:

```
puhu [options] "puhuttava lause" | file.txt      speak through the default audio device
puhu -o out.wav  "..."                           write a WAV file
puhu -o out.mp3  "..."                           write MP3
puhu -o -        "..." | aplay                   WAV on stdout, for pipes
```

The original's options (speed, pitch, pitch variation, volume, voice, the emphasis switches) keep their names and meanings. The full-screen DOS interface is out of scope for the first version.

## Design

Three layers, so that the part taken from the original can be checked exactly:

1. **Core** (`libmikropuhe`, plain C, no I/O): text + settings + a loaded voice in, PCM samples out. A faithful re-implementation of the original's logic, function by function.
2. **Voice loader**: reads the original `.SMP` files unmodified.
3. **Outputs**: one small sink interface with three implementations.
   - Audio device: PipeWire or ALSA. Decide after the sample format is known; the fallback that always works is WAV on stdout into `pw-play` or `aplay`.
   - WAV: written directly, no library needed.
   - MP3: through `libmp3lame` if present at build time, otherwise by piping WAV into `lame` or `ffmpeg`.

The original's sample rate is kept in the core. Resampling for the sound card belongs to the output layer.

## Steps

Each step ends with something that can be checked. State on 2026-10-09 after the third session.

1. **Reference set.** Done. `tools/puhu_emu.py` runs the original and captures the ring-buffer stream; `tools/make_reference.py` produces 68 cases. Two runs give the same `evidence/reference/MANIFEST.tsv`.
2. **Stop-gap command.** Done: `port/puhu-emu`. Now the reference implementation for the checks.
3. **Map the program.** Done for the path from `main` to the ring buffer: `notes/FUNCTIONS.md`. Not read: the full-screen interface, the serial number, the driver calls.
4. **Decode the voice blocks.** Done: `docs/voice-format.md`, checked by `tools/smpdump.py` on all three files. Every byte the program reads is named; 1,976 bytes of block 0 are never read and their purpose is unknown.
5. **Stage oracles.** Done: `Dos.enable_trace` records every word-stage call with its inputs, outputs and stack bytes.
6. **Core in C.** Done: `port/src/voice.c`, `synth.c`, `text.c`. Checks: `make -C port check-phonemes` 68 of 68; `make -C port check-e2e` stage calls 89 of 89, streams 86 of 89 (the rest differ only through uninitialised stack bytes, see notes).
7. **Outputs and CLI.** Done: `port/src/puhu.c` with WAV, MP3, standard output and playback. `puhu` and `puhu-emu` give identical WAV files for the same input. Playback through a sound device has not been tried on ai.
8. **Text handling for today.** UTF-8 input is converted to CP437. Checked through the end-to-end cases, which pass UTF-8.

9. **Virtual PC speaker, ALSA, piping** (added 2026-10-09). Done: `puhu --device pcspeaker` with machine presets, native ALSA playback with `--alsa-device`, line-by-line speaking of piped text, `make install`. Also: WAV to a redirected standard output, `PUHU_PLAY_CMD`, `--why-silent`, and opening the recording in VS Code when a remote machine has no usable sound device. Check: `make -C port check-pcspk`. See `docs/pc-speaker.md` and `docs/playback.md`. Not verified: sound on a real card (no access to one from the SSH session on ai), and the machine presets against real hardware.

10. **The full-screen interface and voice editing** (added 2026-10-09). Done: `puhu` with no text opens the original's interface (`port/src/ui.c`), checked against the original cell by cell (`make -C port check-ui`); `tools/smptool.py` unpacks and packs voice files, `puhu -v PATH` loads them. See `docs/interface.md` and `docs/voice-format.md`. Not done: stopping speech with a key. Used by hand by the owner in the VS Code terminal, without sound and then with sound on the Mac through `port/puhu-listen`.

11. **Windows** (added 2026-10-09). Done: `make -C port windows` cross-compiles a native `puhu.exe` (MinGW-w64) and puts it with the voices into one folder; playing through the system's sound output, the console interface, `%APPDATA%\mikropuhe` for saved settings, text in Windows code pages. Checked under Wine in a container (`make -C port check-windows`): 156 of 156 cases and the sound files identical to Linux. On 2026-10-11 run on the owner's Windows 11 virtual machine over SSH with the same results, including the interface in the SSH session's console and a sentence played without error. The owner then used it at the machine: "it worked perfectly". See `docs/windows.md`; the method is in `docs/porting.md`.

12. **A voice of our own** (added 2026-10-11). Done: `tools/make_synth_voice.py` computes a complete voice, `voices/KONE.SMP`; `puhu -v kone`; the original accepts it and speaks the same samples. Not done: listening and tuning.

## What is left

- **Kone: listen and tune.** The first version was made without ears. After that, optionally a voice from the owner's own recordings.
- **A copy that speaks without any original file.** With Kone the voices are no longer needed, but the build still reads the number words and the interface texts from `PUHU.EXE`. Own tables for those would make the shared copy complete in itself.


- Hear it on a real sound card, and with `snd-pcsp` on a machine that has a PC speaker.
- The tunnel with `port/puhu-listen` works (heard on the Mac, 2026-10-09). The shell function of way 2 in `docs/playback.md` is still untried.
- macOS: installed on the Mac and used there by hand (`docs/macos.md`). Left: optionally Core Audio; an Apple Silicon Mac.
- If a recording of MikroPuhe on a real PC can be found or made: tune `--timing` presets and the loudspeaker filter to it.

- Decide how to treat the stale-stack reads: keep (now), or define them (a fixed pitch byte), which gives a cleaner program but no byte-for-byte check for the affected words.
- Where the voice files live outside this repository (`make install` only links to the build here).
- Optional, new features behind options: resampling to 44.1/48 kHz, a de-clipped default volume, saving settings.
- More inputs in the end-to-end check, in particular text files with the screen echo on.
- Report upstream that `rea function` returns nothing when the decompiler fails (see `docs/method.md`).
- In the interface: interrupting speech by a key press.
- Record a new voice (the tool for packing it exists).

## Ideas for later

**English, next to Finnish** (the owner, 2026-10-09): a switch between a Finnish and an English pronouncing mode, and English voice packs. For the further future. The switch on the command line is to be `--language fi|en`, short `-l` (`fi` the default; `-l` is free today). Not started. What the engine as it is implies:

- The engine speaks spelling. That works because Finnish spelling is nearly phonemic; English needs a step in front that turns words into sounds (a pronouncing dictionary such as CMUdict plus letter-to-sound rules for unknown words).
- **Stage 1, no new recordings: English with a Finnish accent.** Map each English sound to the nearest thing the three voices have and feed the result to the existing word stage as Finnish-style spelling ("hello" → `helou`, "thing" → `ting`, "she" → `sii`). The stress marks the word stage already understands (`#`, `_`, `-`) can carry English word stress. This is a text front end only; the exception lists show the original authors doing the same by hand (`macintosh` → `mä#kintoss`, `apple` → `äpll`).
- **Stage 2, English voice packs.** The voices have 8 vowels, `j l m n v ng`, the stops, `f h r s`. English also needs at least the sounds of *th* (two), *sh*, *zh*, *ch*, *j*, *w*, *z*, the English *r*, and several vowels (as in *cat*, *cup*, *bird*, *about*). That means more units per table than the format's fixed letter lists address (0x14d54, 0x14d7c), so the word stage and the voice format need an extension: more unit numbers, and symbols for the new sounds. `tools/smptool.py` already packs whatever units it is given; the engine side is the work.
- English rhythm differs too (reduced vowels, stress not on the first syllable). The duration parameters are per voice, so an English voice can carry its own, but vowel reduction would be new logic.
- The original Finnish mode has to stay byte-identical to the original; anything new goes behind `--language en`, as the checks would otherwise fail. In the full-screen interface the language would be one more row of the settings screen, which the original's table of rows allows for.
- A language also belongs to a voice pack: an English pack should be able to say so in its file, so that `-v english.smp` does not need `-l en` as well. The settings block has two unused words (11 and 12).

**Windows 11, and a new DOS version** (the owner, 2026-10-09). Windows is built and checked under Wine (step 11 above; what follows under *Windows 11* is the plan as it was written before, kept for the record). DOS is not started. Facts first:

- Of the port's C code, the speech core has nothing system-specific in it: `synth.c`, `text.c` and `voice.c` (1,146 lines) contain no POSIX call at all. What is tied to Linux and macOS is the edge: `out.c` (playing, pipes, temporary files), `puhu.c` (finding files, terminal checks), `ui.c` (reading keys, terminal size).
- On ai, when this was written, there was no cross-compiler for either target: no MinGW-w64, no Wine, no DJGPP or Open Watcom, no DOSBox. `qemu-system-i386` is there. The owner installed MinGW-w64 the same day; Wine runs in a Docker container instead of being installed. For DOS, still nothing.

*Windows 11*

1. **Today, without porting:** under WSL the Linux build should work as it is, with sound through WSLg. Untested.
2. **A native `puhu.exe`**, cross-compiled with MinGW-w64. To replace: playing (`PlaySound` from memory, or `waveOut`; no ALSA, no pipes to players), the program's location (`GetModuleFileName`), the user's directory (`%APPDATA%`), temporary files and `popen`, and in the interface the key reading and the window size (the console of Windows 10 and later understands the same escape sequences for drawing, once switched on). An estimate: 200 to 300 lines behind `#ifdef _WIN32`, as macOS needed about 30.
3. **Check:** the same as for macOS: the 156 comparison cases (`--events`, `--ui-script`) must give the Linux build's output. That needs a Windows machine, or Wine on ai.
4. The original `PUHU.EXE` itself cannot run on Windows 11: 64-bit Windows has no DOS subsystem. There it needs DOSBox.

*DOS (DR-DOS, FreeDOS, MS-DOS)*

1. **The original is the DOS version**, and on a DOS that boots on a machine of its time, or in an emulator such as DOSBox-X or 86Box, it runs unchanged. That is what `tools/puhu_emu.py` relies on.
2. **On a fast modern processor the original is likely to misbehave** (inferred from 0x15167, not tried): it measures one pass of a delay loop in whole nanoseconds. A pass that takes less than a nanosecond is measured as 0 or 1, which makes the speech too fast and too high, or divides by zero when the pulse-width table is built. And today's machines often have no PC speaker to drive.
3. **A new DOS build of the port** is therefore a real project, not a recompile. The core compiles with a DOS C compiler (DJGPP, 32-bit with a DPMI host, is the easy one; it runs on DR-DOS and FreeDOS). New would be the device layer: a PC-speaker player timed by the timer chip instead of by delay loops, so that it works at any processor speed, and optionally Sound Blaster output; and the interface on the real text screen, which is what the original's code did in the first place.
4. **Check:** FreeDOS under `qemu-system-i386` on ai, with QEMU writing the emulated PC speaker or Sound Blaster to a WAV file, compared against the sample stream. Needs a FreeDOS image and the DOS compiler.

Suggested order: Windows first (small, and checkable the way macOS was), then the DOS build as the retro project it is.

Output choices: the raw 8-bit stream is the default; `--pc-speaker` applies the pulse-width curve; pauses are rendered with the PC's timer timing (× 1.18) unless `--nominal-pauses`.

## Open decisions for the owner

- Where the port's source lives in the long run: inside this directory (`port/`, now) or its own repository.
- Whether this will ever be published (see Rights in the target README). `port/src/tables.h` contains text taken from the program (the number words), and the voices are needed to run it.
- Keep the original's bugs for exactness (now), or fix them.

## Risks

- The checks compare against one emulated DOS environment. Behaviour that depended on the real machine (timing of the speaker loop, memory layout) is outside what they can show.
- Tools on ai, checked 2026-10-09: `gcc`, `lame`, `ffmpeg`, `sox` and `paplay` are installed; `aplay`, `pw-play` and DOSBox are not. Hearing live playback needs a machine with speakers.
