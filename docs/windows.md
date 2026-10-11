# puhu on Windows

The same C sources build a native `puhu.exe` for 64-bit Windows 10 and 11. This page says what was checked, what was not, how to get it onto a Windows machine, and what is different there.

## State (2026-10-11)

**Runs on Windows 11, passes every check there, and the owner has used it at the machine: "Had so much fun, it worked perfectly!" (2026-10-11).** He added that he had also run the full-screen interface in the plain console window, and that it "looked great".

On 2026-10-11 the folder was copied to the owner's Windows 11 Pro virtual machine (build 26300, on the Unraid machine) and the program was run there over SSH; outputs in `evidence/windows/win11-*.txt`:

| Checked on Windows 11 | Result |
|---|---|
| the 156 speech and interface cases, run by `tests/portable/run.ps1` (PowerShell 5.1; the machine has no `sh`) | **156 of 156 identical** to the Linux build |
| sound files made in a `cmd` session by `tests/portable/edges.cmd`: `-o FILE`, `> FILE`, `echo ... \| puhu` with ä in code page 850, a file name with ä and ö, the virtual PC speaker with two settings | all six byte-identical to the Linux build's |
| playing a 3.4 s sentence (`--play`) | the system call took 3.5 s and reported success. Whether it was audible is not known: nobody was logged in at the machine's screen. |
| saving settings (F2 twice by key script) | `%APPDATA%\mikropuhe\JANNE.SMP` created, the shipped file unchanged |
| the full-screen interface in the console of an SSH session | main screen with its frame characters and key bar; typing, with ä; Backspace, Left, Del; F2 and Ctrl-P open the settings; `+` changes Korkeus from 4 to 5; Esc ends the program and the session |

The console of an SSH session is Windows' own console code, but it is not a window: what the screen looks like in Windows Terminal or a `cmd` window, with their fonts and colours, has not been seen.

Before that, the same build had been checked on ai under Wine:

`puhu.exe` is cross-compiled on the ai box with MinGW-w64 (`x86_64-w64-mingw32-gcc` 16.2.0). It is one file of 88 KB that needs nothing installed: it uses only libraries that are part of Windows 10 and 11 (`KERNEL32`, `SHELL32`, `WINMM` and the C runtime; list in `evidence/windows/puhu-exe-imports.txt`).

Checked with `make -C port check-windows`, which runs the program under Wine 9.0 in a container ([porting.md](porting.md)); output in `evidence/windows/check-windows.txt`:

| Checked under Wine | Result |
|---|---|
| 90 speech inputs with `--events` and 66 key scripts for the full-screen interface, compared with the Linux build's output | **156 of 156 identical** |
| sound files: `-o FILE`, `> FILE`, text piped in, a file name with ä and ö, the virtual PC speaker with two settings | all six byte-identical to the Linux build's |
| text piped in as code page 850 with CR LF line ends, as `cmd`'s `echo` writes it | same sound file as the UTF-8 text |
| a text file in code page 1252 given with `-f` | same sound file as the UTF-8 text |
| saving settings (F2 in the settings screen) | a copy of the voice appears under `%APPDATA%\mikropuhe`; the voices folder was mounted read-only |
| the full-screen interface in Wine's console | the main screen is drawn, typed letters arrive, F2 and Ctrl-P open the settings, a value changes with `+`, Esc ends the program |
| playing (`--play`) | **fails as expected**: the container has no sound device. The message and the exit status are right. |

Since the Linux build is checked against the original program, and this build gives identical output for the same inputs, it inherits those results for the speech and the interface logic.

**Seen by the owner:** the interface in the plain console window ("the UI looked great").

**Not reported in detail** (his "it worked perfectly" may cover these; he did not name them):

- How the speech sounds next to the Linux and Mac ones.
- The interface in Windows Terminal, and single keys of a real keyboard: F1, F2, AltGr characters.
- What Windows makes of the unsigned program when it is started by double-click or from Explorer (SmartScreen may ask). Started over SSH it ran without a question.

Not tried at all: Windows 10; Windows on ARM (it runs 64-bit Intel programs through its own translation).

## Where it is on the owner's Windows machine

`C:\Users\NAME\mikropuhe-windows` (copied there on 2026-10-11; checksums of all four files compared). In a terminal there:

```
cd %USERPROFILE%\mikropuhe-windows
puhu "hyvää päivää"
puhu
```

## Getting it onto a Windows machine

On ai:

```bash
make -C port windows      # builds port/build/mikropuhe-windows/: puhu.exe and the three voices
```

Copy that folder to the Windows machine. From PowerShell or `cmd` there, with the address and path of the ai box filled in:

```
scp -r USER@AI:projects/rea/project/mikropuhe-1.0/port/build/mikropuhe-windows .
cd mikropuhe-windows
```

The program finds the voices next to itself, so the folder can live anywhere. The voices are Timehouse Oy's: the folder is for your own machines.

## Using it

```
.\puhu "hyvää päivää"
.\puhu -v sophia "Hei Michael, tämä on KITT"
.\puhu -s 4 -V 4 -d pcspeaker "hyvää päivää"
.\puhu -o hei.wav "hei"
.\puhu                         the full-screen interface
.\puhu --version               should say 1.2
```

In `cmd`:

```
echo Hei Michael, tämä on KITT | puhu -v sophia
puhu -v sophia "Hei Michael" > sophia.wav
```

In PowerShell, two things are different, and they are PowerShell's doing (known behaviour, not tried here):

- `>` treats a program's output as text. Windows PowerShell 5.1 and PowerShell before 7.4 rewrite it, which ruins a WAV. Use `-o sophia.wav` instead.
- `"text" | .\puhu` sends the text in PowerShell's own output encoding. In Windows PowerShell 5.1 that is ASCII, so ä and ö are lost before `puhu` sees them. Give the text as an argument instead.

## What is different on Windows

| | Linux | Windows |
|---|---|---|
| Playing | ALSA, then external players | the system's sound output, called directly. `$PUHU_PLAY_CMD` first, as everywhere. |
| `-D`, `--alsa-device` | an ALSA device | not available |
| MP3, M4A | `lame` or `ffmpeg` | the same, if one of them is on the PATH |
| Finding the voices | relative to the program | next to the program (the folder above), else relative to it |
| Your own copies of voices (saved settings) | `~/.local/share/mikropuhe/` | `%APPDATA%\mikropuhe\` |
| Text encoding | UTF-8 | arguments: taken from Windows as UTF-16. Piped text and `-f` files: UTF-8, else the console's code page (pipes) or the system's "ANSI" code page (files). |
| The interface's keys | decoded from the terminal's escape sequences | read as key events from the console. Backspace and Ctrl-Backspace arrive with the PC keyboard's own codes, as under DOS. |
| The interface's screen | escape sequences and UTF-8 | the same, after switching the console to understand them (Windows 10 and later) |
| A terminal without a console (Git Bash's default window) | | the interface refuses with a message; `puhu "text"` works |

Finding the voices next to the program is new for all systems, not only Windows: if `JANNE.SMP` is in the program's folder, that folder is used.

`puhu --lines`, piping, the virtual PC speaker and all speech options are the same code as elsewhere.

## Source

All of it is behind `#ifdef _WIN32` in the three edge files ([porting.md](porting.md)); the speech core is untouched.

| File | Windows part |
|---|---|
| `out.c` | `play_windows`: builds the WAV in memory and plays it; commands looked up with `where`; pipes opened in binary mode |
| `puhu.c` | `windows_start`: arguments as UTF-8, standard output binary. `windows_text`: text that is not UTF-8. `program_dir`, `user_dir`. `read_line` replaces `getline` on every system. |
| `ui.c` | `console_key`, `byte_ready`, `terminal_key`: console key events to the original's key codes. Console set-up and restore in `mp_ui_run` and `ui_end`. |
| `win/puhu.manifest`, `win/puhu.rc` | asks Windows (10 version 1903 and later) to hand file names and environment values to the program as UTF-8 |

## What is left

| | What | State |
|---|---|---|
| 1 | Build | done |
| 2 | Same output as on Linux | done under Wine and on Windows 11: 156 of 156, and the sound files |
| 3 | Run it on Windows 11 | done over SSH (table above) |
| 4 | Use it at the machine | done by the owner, 2026-10-11: "it worked perfectly". The interface in the plain console window "looked great". Single keys were not named. |
| 5 | Stopping speech with a key | not done on any system |
| 6 | An installer, a signed program | not planned. Publishing anything needs the right holder's permission first (see the README). |
