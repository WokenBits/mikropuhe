# The full-screen interface

Started with no arguments, `PUHU.EXE` did not print a usage line: it opened a full-screen program in which you type a sentence and hear it. `puhu` does the same when it is started at a terminal with nothing to say.

```bash
puhu                # the interface
puhu --lines        # no interface: each line you type is spoken
```

The interface talks. It greets you, reads the help aloud, announces every setting as you move over it and every value as you change it, and says goodbye. It can be used without looking at the screen, which was presumably the point.

## The main screen

```
         MikroPuhe 1.0 Sarjanumero ···    Copyright 1990 Timehouse Oy

                           ┌──────────────────────────────────────────────┐
     Anna puhuttava lause: │hyvää päivää                                  │
                           └──────────────────────────────────────────────┘




    F1 Apu   F2 Puhearvot   ↑ ↓ Lauseiden selaus   ◄──┘ Puhu   Esc Lopetus
```

On start it greets you, says what it is, points to F1 for instructions, and asks for a sentence to speak.

| Key | What it does |
|---|---|
| typing | inserts at the cursor (there is no overwrite mode); up to 255 characters, the box scrolls |
| Enter | speaks the line, stores it in the history, and empties the box. An empty line does nothing. |
| ↑ ↓ | step through the last ten spoken lines. ↓ past the newest empties the box. |
| ← → Home End | move the cursor |
| Ctrl-← Ctrl-→ | to the start of the previous or next word |
| Backspace, Del | delete the character before or under the cursor |
| Ctrl-Backspace | empty the box |
| F1, or Ctrl-A, or Alt-1 | reads the help aloud |
| F2, or Ctrl-P, or Alt-2 | the settings screen |
| Esc | says "Näkemiin." and leaves |

The extra keys for F1 and F2 are the port's: many terminal programs keep the function keys for themselves. In the VS Code terminal F1 opens VS Code's command palette and never reaches `puhu`. Ctrl-A stands for *Apu*, Ctrl-P for *Puhearvot*; they work in the settings screen too. A grey reminder of them sits at the right of row 24.

A word, for the word jumps, ends at any of ``space - . , : ; ! " # % / ( ) = + ? ' [ ]``.

The line is handed to the speech core as it is. Unlike text given on the command line it is not cut into sentences, and options such as `/NOP5` inside it are not recognised: they are read out.

## The settings screen (F2)

The screen lists, one to a row: speed, pitch, pitch variation and volume, each a number from 0 to 8;
the speaking voice; and five switches: consonant emphasis, first-syllable stress, quieter speech for
text in brackets, whether an underscore is spoken, and how the letter c is read. The key bar at the
bottom names F1 (help), F2 (save), the arrows, + and - (change the value) and Enter (done).

Arriving on a row reads it: "nopeus: nolla", "puhuva ääni: Janne", "konsonanttien painotus: on".

| Key | What it does |
|---|---|
| ↑ ↓ | previous or next setting. At the ends it says "alku" or "loppu". |
| `+` `-` | change the value and say the new one. Numbers run 0 to 8; switches go on and off ("on", "ei"); the voice steps Janne, Kristian, Sophia; the letter c is read as S ("äs") or K ("koo"). |
| Space | reads the row again |
| F1 | reads the help for this screen |
| F2 | saves the settings into the voice file and says "talletettu" |
| Enter | says "ok" and returns to the main screen |
| Esc | leaves the program |

Changing the voice loads the voice file, and with it the settings saved in that file: the other rows change to the new voice's values.

The screen remembers which row you were on when you come back.

**Saving** (F2) makes the current settings the voice's defaults, for the interface and for the command line alike. The original did this by writing the 14 setting words into the voice file itself, right after the voice's name, and so does the port, with one difference: the shipped voice files are never changed. The first time you save, the voice is copied to `~/.local/share/mikropuhe/` (or `$XDG_DATA_HOME/mikropuhe/`) and the settings go into that copy. From then on `puhu` uses your copy of that voice.

```bash
ls ~/.local/share/mikropuhe/          # the voices you have saved settings for
rm ~/.local/share/mikropuhe/JANNE.SMP # back to the shipped Janne and its settings
```

If the file cannot be written, a box says that the file was read- or write-protected (also aloud) and waits for Enter; Space repeats the message. That is the original's behaviour for a write-protected disk. It now appears only with `--voices-dir`, which rules out copies and therefore saving, or with a `-v PATH` file that is not writable. The voices directory itself is never written, whatever its file permissions are.

## How it follows the original

The interface is the original's code path, ported routine by routine: the main loop (0x11c57), the settings screen (0x11a26, 0x11713, 0x112c6), the key bars (0x1121b), the error box (0x1203a) and the text-field editor (segment 1594, with its key handler at 0x1606a). The strings, the table of settings rows and the frame characters are read from the program's data by `tools/gen_ui_tables.py`.

The original drew with Turbo C's console functions on the 80×25 text screen. The port makes the same calls into a buffer of 25×80 character cells with the original's colour attributes, and a small backend shows that buffer on a terminal with ANSI colours and UTF-8.

### Checked against the original

`make -C port check-ui` sends the same keys to both programs. The original runs in the emulator until it waits for a key that is not there; the port runs with `--ui-script`. At that point everything is compared: all 2,000 character cells, their colour attributes, the cursor position, whether the program ended, and every word that was handed to the speech core.

**Result (2026-10-09): 26 hand-written key sequences and more than 600 random ones, 0 differences.** The checker itself was tested by giving the port one key less than the original, which it reported.

Look at any sequence in the original yourself:

```bash
tools/ui_explore.py F2 DOWN DOWN + ENTER text:moi ENTER
```

Things the original does that are kept, because the comparison shows them:

- Keys typed while the program is speaking are thrown away when it returns to the input box (after the greeting and after F1).
- In the settings screen, of several keys typed ahead only the last one counts.
- The typed text is drawn white on black inside the blue box, so the box turns black where there is text.
- `+` on the last voice, or `-` on the first, says the voice's name again.

### Where it differs

| | Original | Port |
|---|---|---|
| Screen | 80×25 text mode; refuses other modes | a terminal of at least 80×25; refuses smaller ones. In a larger window the 80×25 screen sits in the top left corner and the rest stays empty. |
| Colours | colour set for CGA/EGA/VGA, and a monochrome set | the colour set only |
| Interrupting speech | a key press stops the speech, going by its keyboard interrupt hook (0x14ed5; not exercised in the emulator) | speech plays to its end; keys typed meanwhile are handled as described above |
| Backspace | key codes of the PC keyboard | today's terminals: Backspace sends 0x7F and is taken as Backspace; Ctrl-Backspace or Ctrl-H (0x08) empties the box |
| Sound | the PC speaker, always there | whatever `puhu` can play on ([playback.md](playback.md)). If nothing can play, everything it would have said is shown as text on rows 21 to 23 instead, which the original leaves empty, and a note is printed when you leave. |
| F1 and F2 | the function keys | also Ctrl-A and Ctrl-P, or Alt-1 and Alt-2, with a reminder on row 24 |
| Serial number | shown from the copy's data | the number stored in your copy of `PUHU.EXE` |
| A voice file given with `-v PATH` | not possible | shown under its own name; `+` and `-` on the voice row do nothing |
| Saving settings (F2) | into the voice file on the program's disk | into your own copy of the voice under `~/.local/share/mikropuhe/`; the shipped files stay as they are |

On Windows the same screen buffer is shown in the console window, and keys are read as console key events instead of escape sequences ([windows.md](windows.md)). That backend has run under Wine only.

The captions and the key reminder are drawn by the terminal backend over the finished screen. They are not in the screen buffer that is compared with the original, so the comparison is unaffected (re-run after adding them: 26 sequences, 0 differences).

The terminal backend (escape sequences out, key decoding in) has no original to compare with. It was exercised through a pseudo-terminal: start, typing, F2, a change, Enter, Esc, and the terminal restored afterwards. The owner then used it by hand in the VS Code terminal on 2026-10-09 over SSH: first without sound (the screen looked right, and the help worked through the extra key once that had been added), then with sound, played on his Mac through `port/puhu-listen` ([playback.md](playback.md#3-play-by-itself-from-the-remote-terminal-through-an-ssh-tunnel)).

Not ported: the monochrome colour set, and the start-up check that refused other screen modes.
