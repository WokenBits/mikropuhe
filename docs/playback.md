# Getting text in and sound out

Everything about how `puhu` takes text and where the speech can go: files, pipes, sound cards, and machines you are not sitting at. The options that shape the speech itself (voice, speed, the PC speaker model) are in [`port/README.md`](../port/README.md) and [pc-speaker.md](pc-speaker.md).

## Quick answers

| I want to | Command |
|---|---|
| speak a sentence | `puhu "hyvää päivää"` |
| speak what another program prints | `echo "Hei Michael, tämä on KITT" \| puhu -v sophia` |
| speak a log as it grows | `tail -f loki.txt \| puhu` |
| speak a text file | `puhu -f teksti.txt` |
| save a WAV | `puhu -o hei.wav "..."` or `puhu "..." > hei.wav` |
| save an MP3 | `puhu -o hei.mp3 "..."` (on a Mac without lame or ffmpeg: `-o hei.m4a`) |
| feed another program | `puhu "..." \| sox -t wav - -r 48000 out.flac` |
| play on a particular sound card | `puhu -D plughw:1,0 "..."` |
| see which sound cards there are | `puhu --list-devices` |
| find out why I hear nothing | `puhu --why-silent` |
| hear a remote machine | see [Over SSH](#over-ssh-hearing-a-machine-you-are-not-sitting-at); in short `python3 ~/puhu-listen --tunnel ai` on the Mac |

## Playback options at a glance

| Option or variable | What it does | Details |
|---|---|---|
| `-o FILE` | write a file instead of playing: `.mp3` (lame or ffmpeg), otherwise WAV | [files and pipes](#sound-out-files-and-pipes) |
| `-o -` | WAV to standard output | |
| `puhu … > FILE`, `puhu … \| PROGRAM` | the same as `-o -`: a redirected standard output receives the WAV | |
| `--play` | play even though standard output is a file or a pipe | for scripts whose output is logged |
| `-D NAME`, `--alsa-device NAME` | play on this ALSA device and no other; implies `--play` | [ALSA devices](#alsa-devices) |
| `--list-devices` | list the emulated output devices, the PC-speaker machines and the ALSA devices | |
| `--why-silent` | say why nothing may be heard on this machine and what to do | [hearing nothing](#hearing-nothing) |
| `$PUHU_PLAY_CMD` | a player of your own, tried before everything else; gets the WAV on standard input | [your own player](#your-own-player-puhu_play_cmd) |
| `-d dac` (default) | what is played: the 8-bit samples at 22,000 Hz | |
| `-d pcspeaker` | what is played: the virtual PC speaker, 16-bit at `--rate` | [pc-speaker.md](pc-speaker.md) |
| `--rate HZ` | PC speaker: output sample rate, 22050 to 96000, default 48000 | |
| `--nominal-pauses` | DAC: a pause of *n* lasts *n* samples instead of the PC's *n* × 64 timer ticks | [original-program.md](original-program.md#pauses) |
| `-f FILE` | text from a file instead of arguments or standard input | [text in](#text-in) |
| `--cp437` | the text is already code page 437 | |

Without any of them: text from the arguments or standard input, played through the first way that works (see [playing](#sound-out-playing)).

## Text in

| Source | How | Notes |
|---|---|---|
| arguments | `puhu sana toinen "kolme neljä"` | joined with single spaces, as the original did |
| a file | `puhu -f teksti.txt` | read whole |
| standard input | `... \| puhu` | when neither of the above is given and input is piped in |
| the keyboard | `puhu` | started at a terminal with nothing else: the full-screen interface ([interface.md](interface.md)); `puhu --lines` reads plain lines instead |

The text is UTF-8 and is converted to the program's own character set, code page 437; characters that do not exist there become spaces. `--cp437` skips the conversion for text that already is CP437, such as an old DOS file. A DOS line end (CR LF) counts as one line end and a 0x1A byte ends the text, as in DOS.

**Line by line.** When the text comes from standard input *and* the speech is played (not written to a file or pipe), each line is spoken as soon as it arrives. That is what makes `tail -f` and interactive typing work. A sentence that continues on the next line is therefore spoken in two parts. When the output is a file or a pipe, all input is read first and spoken as one text, with line ends treated as spaces.

## Sound out: files and pipes

| Form | Result |
|---|---|
| `-o NAME.mp3` | MP3, through `lame -V 4` if installed, else `ffmpeg` |
| `-o NAME.m4a` | AAC, through `afconvert` on macOS (built in), else `ffmpeg` |
| `-o NAME` (anything else) | WAV |
| `-o -` | WAV on standard output |
| no `-o`, standard output redirected to a file or a pipe | WAV on standard output, the same as `-o -` |
| no `-o`, standard output is the terminal | play |
| `--play`, or `-D DEVICE` | play, wherever standard output goes |

The WAV is mono: 8-bit unsigned at 22,000 Hz from the DAC device (the original's own format), 16-bit at `--rate` (default 48,000 Hz) from the PC speaker device. Nothing is resampled for the DAC device; players and encoders convert as needed (lame writes 22.05 kHz).

`puhu "..." > /dev/null` still plays: `/dev/null` is neither a file nor a pipe.

Mind this in scripts: if a script's output is captured or logged, a bare `puhu "valmis"` inside it would put a WAV into that log instead of playing. Write `puhu --play "valmis"` there.

## Sound out: playing

Without an output file, `puhu` tries these in order and stops at the first that works:

| Order | Way | When |
|---|---|---|
| 1 | the command in `$PUHU_PLAY_CMD` | if the variable is set. The command gets the WAV on its standard input. |
| 2 | ALSA, device `default` | if built with ALSA (`make` does that when the ALSA development files are installed) |
| 3 | `pw-play`, `aplay`, `paplay`, `afplay`, `ffplay` | whichever exist, in this order. `ffplay` is skipped when no sound device or sound server is reachable, because it reports success without one. |
| 4 | open the recording in VS Code | in a VS Code remote terminal: the file `/tmp/puhu-<uid>.wav` opens in the editor, and you press play |
| | give up | a message, exit status 1 |

With `-D NAME` (`--alsa-device`) only that ALSA device is used and a failure is an error; nothing else is tried.

### ALSA devices

```bash
puhu --list-devices          # the last part lists what ALSA offers
puhu -D default "hei"
puhu -D plughw:1,0 "hei"     # card 1, device 0, with conversion
puhu -D plughw:Generic_1 "hei"   # a card by name (see /proc/asound/cards)
puhu -D null "hei"           # nowhere; useful for testing
```

Use a `plug` device (`default`, `plughw:…`): it converts the rate and format to what the card accepts. A raw `hw:…` device converts nothing and will usually refuse 22,000 Hz 8-bit.

### Your own player: `PUHU_PLAY_CMD`

Any shell command that reads a WAV from standard input. For example:

```bash
export PUHU_PLAY_CMD='sox -q -t wav - -d'              # sox, to its default device
export PUHU_PLAY_CMD='nc -N 127.0.0.1 4714'            # to puhu-listen on another machine; see Over SSH
```

If the command exits with an error, `puhu` says so and goes on with the usual ways. The mechanism was tried with `sox` writing to its null output and with `nc`; `sox -d` itself needs a sound device and was not.

### The physical PC speaker

Linux can expose the PC's own beeper as an ALSA sound card through the kernel driver `snd-pcsp`, which does pulse-width modulation with the timer chip, as the original program did. With that driver loaded, `puhu -D plughw:pcsp "hei"` would play on the real thing; use the DAC device for it, because the driver does the modulation itself. The kernel on ai (7.1.5-arch1-2) does not include `snd-pcsp`, and the `pcspkr` driver it has can only make tones, so this is untried.

## Hearing nothing

`puhu --why-silent` looks at the machine it runs on and prints which of these applies:

- **No sound cards at all** (`/dev/snd` missing).
- **Cards exist but this user may not open them.** Normal on a machine reached over SSH: the device files belong to the group `audio`, or access is given to whoever is logged in at the screen. `sudo usermod -aG audio $USER` and a new login allow it.
- **Cards are accessible** but the default one is not the one with speakers: pick one with `-D`.

On the ai box (checked 2026-10-09): four cards, none accessible from an SSH session; the default card would be an HDMI output of a graphics card, and the analog output is the card named `Generic_1`. No PulseAudio-compatible server runs (`pipewire` runs, `pipewire-pulse`, `pipewire-alsa` and `wireplumber` are not installed).

## Over SSH: hearing a machine you are not sitting at

`puhu` runs on the remote machine; the sound should come out where you are. Three ways, from no setup to fully automatic.

| Way | Setup | What you get | State |
|---|---|---|---|
| 1. VS Code tab | none | each recording opens in the editor; you press play | works (heard 2026-10-09) |
| 2. Fetch and play | none, or a shell function on your machine | commands typed on *your* machine speak there | remote half tested |
| 3. Tunnel and listener | one file on your machine, one line on the remote one | the remote `puhu` simply speaks, the full-screen interface included | **works (heard 2026-10-09), the one to use** |

### 1. In a VS Code remote terminal: nothing to set up

When nothing on the remote machine can play, `puhu` writes the recording to `/tmp/puhu-<uid>.wav` and opens it in the editor, using the connection VS Code already has (`code FILE`). Press play in the tab that appears.

```bash
echo "Hei Michael, tämä on KITT" | puhu -v sophia
# puhu: no sound device can be used from this session, so the recording was opened in
#       VS Code instead (/tmp/puhu-1000.wav): press play there. See 'puhu --why-silent'.
```

Each call overwrites the same file. Tried on 2026-10-09 from ai to VS Code on the Mac: it was heard.

### 2. Make the audio there, play it here

From a terminal on your own machine, let the remote `puhu` write a WAV to standard output and play that locally:

```bash
ssh ai '~/.local/bin/puhu -v sophia -o - "Hei Michael"' > /tmp/hei.wav && afplay /tmp/hei.wav     # macOS
ssh ai '~/.local/bin/puhu -o - "hei"' | ffplay -nodisp -autoexit -loglevel error -              # with ffmpeg
echo "Hei Michael, tämä on KITT" | ssh ai '~/.local/bin/puhu -v sophia -o -' > /tmp/hei.wav       # text piped from here
```

The full path is used because a non-interactive SSH shell on ai does not have `~/.local/bin` on its PATH.

As a function for `~/.zshrc` on the Mac, so that `puhu` typed there means "speak on ai, play here":

```zsh
puhu() {
  local t=/tmp/puhu-$$.wav
  ssh ai "~/.local/bin/puhu -o - ${(j: :)${(q)@}}" > $t && afplay $t
  rm -f $t
}
```

Then `puhu -v sophia "Hei Michael"` and `echo "Hei Michael, tämä on KITT" | puhu -v sophia` both work in a Mac terminal. (`${(q)@}` quotes each argument for the remote shell.)

### 3. Play by itself, from the remote terminal, through an SSH tunnel

This is the one that makes the remote `puhu` behave as if it had loudspeakers, including the full-screen interface. Three pieces:

```
   ai (no sound)                               the Mac (loudspeakers)
   puhu ──► nc ──► port 4714 ═══ SSH tunnel ═══► port 4714 ──► puhu-listen ──► afplay
```

`puhu` hands each recording to `nc`, which sends it into a port on ai. SSH carries that port back to the Mac, where `port/puhu-listen` receives the recording, plays it, and only then closes the connection. So `puhu` waits until you have heard the sentence, exactly as it would with a sound card.

**Step 1, on the Mac, once: fetch the listener.** It is one Python file and needs nothing but Python 3.

```bash
scp ai:projects/rea/project/mikropuhe-1.0/port/puhu-listen ~/puhu-listen
python3 --version          # any Python 3 will do
```

**Step 2, on ai, once: tell `puhu` to send its speech to the port.** Add this line to `~/.zshrc` on ai:

```bash
export PUHU_PLAY_CMD='nc -N 127.0.0.1 4714'
```

Open a new terminal on ai (or run `source ~/.zshrc`) so that the variable is set. Check with `echo $PUHU_PLAY_CMD`.

**Step 3, on the Mac, every time you want sound: start the listener with its tunnel.** This has to be a terminal on the Mac itself, not one on ai. The prompt tells which is which: on ai it shows `user@ai`. A terminal inside a VS Code window that is connected to ai is a terminal on ai. To get a Mac one, open Terminal.app (Cmd-Space, type "Terminal"), or in VS Code press Cmd-Shift-P and choose "Terminal: Create New Integrated Terminal (Local)". Step 1 is run there too.

```bash
python3 ~/puhu-listen --tunnel ai
```

It prints two lines and stays there:

```
puhu-listen: tunnel: ssh -N -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 -R 4714:127.0.0.1:4714 ai
puhu-listen: listening on 127.0.0.1:4714, playing with afplay. Ctrl-C stops.
```

Leave it running. Ctrl-C stops it and closes the tunnel.

**Step 4, on ai: speak.**

```bash
echo "Hei Michael, tämä on KITT" | puhu -v sophia     # heard on the Mac
puhu                                               # the full-screen interface, talking
```

Each sentence shows up as a line in the listener's window: the time and its length in seconds.

**Day to day**, once steps 1 and 2 are done: start `python3 ~/puhu-listen --tunnel ai` on the Mac, then use `puhu` on ai as if it had loudspeakers. An alias on the Mac saves typing, for example in the Mac's `~/.zshrc`:

```bash
alias puhu-kuuntele='python3 ~/puhu-listen --tunnel ai'
```

**The mistake that is easy to make** (it happened twice while setting this up): starting the listener in a terminal that is on ai. It then listens on ai, opens a tunnel from ai to ai, and tries to play there. Three ways to notice:

- the listener's second line says `playing with paplay` (or another Linux player) instead of `playing with afplay`;
- newer copies of the listener print "this terminal is itself an SSH session";
- each sentence gives `Connection failure: Connection refused` and `the player exited with status 1` in the listener's window.

Stop it with Ctrl-C, since it holds the port on ai, and start it on the Mac. `uname` prints `Darwin` in a Mac terminal and `Linux` in one on ai.

**If you hear nothing**

| What you see | Cause | Do |
|---|---|---|
| on ai: `puhu: PUHU_PLAY_CMD failed; trying the usual ways`, or captions in the interface | nothing is listening at the other end of the port | is step 3 running? On ai, `ss -ltn \| grep 4714` must show a line with `127.0.0.1:4714` |
| on the Mac: `the SSH tunnel ended` right after starting | port 4714 on ai is already taken, usually by an older tunnel | stop the other `puhu-listen`, or find it on ai with `ss -ltnp \| grep 4714` |
| on ai: `echo $PUHU_PLAY_CMD` prints nothing | the terminal was opened before step 2 | open a new terminal |
| in the listener's window: `Connection refused` and `the player exited with status 1` | the listener is running on ai, not on the Mac | see "The mistake that is easy to make" above |
| lines appear in the listener's window, but no sound | the Mac's own volume or output device | `afplay /System/Library/Sounds/Ping.aiff` on the Mac |
| the interface went silent after one failure | it stops trying once playing has failed, so as not to stall on every sentence | leave with Esc, fix the above, start `puhu` again |

**Notes**

- The port number is arbitrary: `puhu-listen --port N`, and the same `N` in `PUHU_PLAY_CMD`.
- Both ends are bound to 127.0.0.1. Nothing is reachable from the network; the audio travels inside the SSH connection.
- If you would rather have the tunnel come up with every SSH connection, put `RemoteForward 4714 127.0.0.1:4714` under `Host ai` in `~/.ssh/config` on the Mac and run `python3 ~/puhu-listen` without `--tunnel`. Only the first connection to ai gets the port, and it disappears with that connection, which is why the separate tunnel of step 3 is the dependable way.
- With `PUHU_PLAY_CMD` set, `-o FILE` and redirecting the output still write a file; the variable only matters when `puhu` would play.
- `puhu-listen` also runs on Linux (it then plays with pw-play, aplay, paplay or ffplay), and `--player CMD` names any other player.

### What was tested

| Part | State |
|---|---|
| Way 1, ai to VS Code on the Mac | worked; the owner heard it |
| The remote half of ways 2 and 3: `puhu -o -` from a bare environment; `PUHU_PLAY_CMD` with `nc` | the receiving end got a WAV byte-identical to `puhu -o` |
| `puhu-listen` on ai itself, with a stand-in player that keeps the file and waits one second | `puhu` returned after 1.0 s for one sentence and 3.0 s for three piped lines; the player got exactly the WAV that `-o` writes; the full-screen interface spoke its greeting, settings and goodbye through it with no captions; something that is not a WAV is ignored; a tunnel that cannot be opened is reported |
| Way 3 from end to end: listener and tunnel on the Mac, `puhu` on ai | **works.** the owner heard `echo "Hei Michael, tämä on KITT" \| puhu -v sophia` and the full-screen interface on the Mac (2026-10-09). Seen from ai at that time: port 4714 held by the SSH server, no listener process on ai, `PUHU_PLAY_CMD` set in `~/.zshrc`. |
| The zsh function of way 2 | **not run** |
| ALSA playback | verified up to the bytes ALSA receives (its `null` and `file` devices), not on a sound card |

## Exit status

| Status | Meaning |
|---|---|
| 0 | spoken, written, or handed to the editor |
| 1 | could not play or write, nothing to say, a voice file could not be loaded, or the chosen PC-speaker machine is "too slow" |
| 2 | wrong usage |
