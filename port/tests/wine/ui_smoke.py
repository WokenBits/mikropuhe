#!/usr/bin/env python3
"""The full-screen interface of puhu.exe in Wine's console, driven through a pseudo-terminal.

This is a smoke test of the Windows console code in ui.c (key events in, escape sequences out):
does the screen come up, do keys arrive, does Esc end the program. What the screens contain is
checked elsewhere, cell by cell (tests/check_ui.py, tests/portable). Wine's console is not the
Windows console, so this shows that the code works, not that it looks right on Windows.

usage: ui_smoke.py FOLDER_WITH_PUHU_EXE [IMAGE]
"""
import fcntl, os, pty, select, struct, sys, termios, time

folder = os.path.abspath(sys.argv[1])
image = sys.argv[2] if len(sys.argv) > 2 else "puhu-wine"
# Docker keeps Ctrl-P for itself (its detach sequence starts with it) unless told otherwise;
# the interface uses Ctrl-P for the settings screen.
cmd = ["docker", "run", "--rm", "-it", "--detach-keys", "ctrl-^", "--network", "none",
       "-e", "LC_ALL=C.UTF-8", "-e", "TERM=xterm", "-v", folder + ":/w:ro", image, "wine64", "Z:/w/puhu.exe"]

pid, fd = pty.fork()
if pid == 0:
    os.execvp(cmd[0], cmd)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))


def read_for(seconds):
    """What the program wrote during this time, and whether it is still running."""
    data, end = b"", time.time() + seconds
    while time.time() < end:
        if select.select([fd], [], [], 0.2)[0]:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                return data, False
            if not chunk:
                return data, False
            data += chunk
    return data, True


steps = [  # keys, seconds to wait, text that must then be on the screen, what it shows
    (b"", 12, "Anna puhuttava lause", "the main screen is drawn"),
    (b"moi", 3, "moi", "typed letters arrive"),
    (b"\x1bOQ", 4, "Nopeus", "F2 opens the settings screen"),
    (b"\r", 3, None, None),
    (b"\x10", 4, "Nopeus", "Ctrl-P opens the settings screen"),
    (b"\x1b", 8, None, "Esc ends the program"),
]
failed = 0
alive = True
for keys, wait, expect, what in steps:
    if keys:
        os.write(fd, keys)
    data, alive = read_for(wait)
    text = data.decode("utf-8", "replace")
    # Wine's console repaints in pieces, with cursor movements between them: compare without those
    plain = "".join(ch for ch in __import__("re").sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text) if ch not in "\r\n")
    if expect is not None:
        ok = expect.replace(" ", "") in plain.replace(" ", "")
        print("  interface: %s: %s" % (what, "yes" if ok else "NO"))
        failed += not ok
if alive:
    print("  interface: Esc ends the program: NO")
    failed += 1
    os.kill(pid, 15)
else:
    print("  interface: Esc ends the program: yes")
sys.exit(1 if failed else 0)
