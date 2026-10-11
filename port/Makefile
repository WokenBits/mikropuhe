CC ?= cc
CFLAGS ?= -O2 -g -std=c99 -Wall -Wextra -Wconversion -Wno-sign-conversion
CORE = src/voice.c src/synth.c src/text.c
HDRS = src/mikropuhe.h src/tables.h
OUT = src/out.c src/pcspk.c src/ui.c
OUTHDRS = src/out.h src/pcspk.h src/ui.h src/ui_tables.h

# Native ALSA playback if the development files are there (ALSA=0 to build without)
ALSA ?= $(shell pkg-config --exists alsa 2>/dev/null && echo 1 || echo 0)
ifeq ($(ALSA),1)
OUTFLAGS = -DHAVE_ALSA $(shell pkg-config --cflags alsa)
OUTLIBS = $(shell pkg-config --libs alsa)
endif

build/puhu: src/puhu.c $(CORE) $(OUT) $(HDRS) $(OUTHDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE $(OUTFLAGS) -o $@ src/puhu.c $(CORE) $(OUT) $(OUTLIBS) -lm

# The two headers with data read from PUHU.EXE (number words, interface texts). They are made
# again only if they are missing or the program file is newer; plain Python 3, no modules.
ORIGINAL ?= ../original/files
src/tables.h: $(wildcard ../tools/gen_tables.py $(ORIGINAL)/PUHU.EXE)
	python3 ../tools/gen_tables.py $(ORIGINAL)/PUHU.EXE > $@.new && mv $@.new $@
src/ui_tables.h: $(wildcard ../tools/gen_ui_tables.py $(ORIGINAL)/PUHU.EXE)
	python3 ../tools/gen_ui_tables.py $(ORIGINAL)/PUHU.EXE > $@.new && mv $@.new $@

build/pcspk_dump: tests/pcspk_dump.c src/pcspk.c src/pcspk.h
	@mkdir -p build
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ tests/pcspk_dump.c src/pcspk.c -lm

build/replay: tests/replay.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/replay.c $(CORE)

# The checks run the original PUHU.EXE in the emulator harness (tools/puhu_emu.py) and compare.
check: check-pcspk check-phonemes check-ui check-e2e

check-ui: build/puhu
	$(PY) tests/check_ui.py --random 100

check-pcspk: build/pcspk_dump
	$(PY) tests/check_pcspk.py

check-phonemes: build/replay
	$(PY) tests/check_phonemes.py

check-e2e: build/puhu
	$(PY) tests/check_e2e.py --extra

build/puhu-asan: src/puhu.c $(CORE) $(OUT) $(HDRS) $(OUTHDRS)
	@mkdir -p build
	$(CC) -O1 -g -std=c99 -fsanitize=address,undefined -fno-sanitize-recover=undefined -D_DEFAULT_SOURCE $(OUTFLAGS) -o $@ src/puhu.c $(CORE) $(OUT) $(OUTLIBS) -lm

# Windows 10 and 11: cross-compiled here with MinGW-w64 (Arch: mingw-w64-gcc). One file with no
# DLLs of its own; it plays through the system's sound output. "make windows" also puts the program
# and the three voices into one folder to copy to the Windows machine (docs/windows.md).
WINCC ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
build/puhu.exe: src/puhu.c $(CORE) $(OUT) $(HDRS) $(OUTHDRS) win/puhu.rc win/puhu.manifest
	@mkdir -p build
	$(WINDRES) --include-dir win win/puhu.rc -O coff -o build/puhu-res.o
	$(WINCC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ src/puhu.c $(CORE) $(OUT) build/puhu-res.o -static -s -lwinmm -lshell32 -lm

VOICES ?= ../original/files
windows: build/puhu.exe
	@mkdir -p build/mikropuhe-windows
	cp build/puhu.exe $(VOICES)/JANNE.SMP $(VOICES)/KRISTIAN.SMP $(VOICES)/SOPHIA.SMP $(wildcard ../voices/*.SMP) build/mikropuhe-windows/
	@chmod u+w build/mikropuhe-windows/*.SMP
	@echo "build/mikropuhe-windows: puhu.exe and the voices. They are Timehouse Oy's: for your own machines."

# The same inputs must give the same output on every system the port runs on (docs/porting.md).
# tests/portable/expected.txt is the Linux build's output, which "make check" ties to the original.
fingerprint: build/puhu
	@sh tests/portable/run.sh build/puhu $(VOICES) | diff - tests/portable/expected.txt && echo "$$(wc -l < tests/portable/expected.txt | tr -d ' ') cases: the output this build should give"

# The Windows build, run under Wine in a throwaway Docker container, against the Linux build.
check-windows: windows build/puhu
	@sh tests/wine/check.sh

# The synthetic voice Kone: every sound computed by tools/make_synth_voice.py (docs/voice-format.md)
kone:
	rm -rf build/kone && python3 ../tools/make_synth_voice.py build/kone
	python3 ../tools/smptool.py pack build/kone ../voices/KONE.SMP

# Puts a link to build/puhu on the PATH. A link, so the program still finds the voices in this
# repository; elsewhere set MIKROPUHE_VOICES.
PREFIX ?= $(HOME)/.local
install: build/puhu
	mkdir -p $(PREFIX)/bin
	ln -sf $(abspath build/puhu) $(PREFIX)/bin/puhu

uninstall:
	rm -f $(PREFIX)/bin/puhu

PY ?= $(HOME)/tools/mikropuhe-venv/bin/python
clean:
	rm -rf build
.PHONY: check check-pcspk check-phonemes check-ui check-e2e install uninstall clean windows fingerprint check-windows kone
