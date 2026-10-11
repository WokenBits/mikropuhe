#!/bin/sh
# Runs the Windows build (build/mikropuhe-windows, from "make windows") under Wine in a throwaway
# Docker container and compares it with the Linux build (build/puhu). See docs/porting.md.
# The container has no network; the program and the voices are mounted read-only.
#
#   tests/wine/check.sh            all checks
#   PUHU_WINE_IMAGE=name           use another image name (default puhu-wine)
here=$(cd "$(dirname "$0")" && pwd)
port=$(cd "$here/../.." && pwd)
voices=$(cd "$port/../original/files" && pwd)
image=${PUHU_WINE_IMAGE:-puhu-wine}
fail=0
# Docker creates a folder that is mounted but missing, owned by root: make sure they are there
[ -f "$port/build/mikropuhe-windows/puhu.exe" ] && [ -x "$port/build/puhu" ] && [ -n "$voices" ] || {
    echo "check.sh: run 'make windows build/puhu' in $port first" >&2; exit 1; }

docker image inspect "$image" >/dev/null 2>&1 || docker build -t "$image" "$here" || exit 1
wine() {
    docker run --rm --network none -e LC_ALL=C.UTF-8 \
        -v "$port/build/mikropuhe-windows:/w:ro" -v "$port/tests:/t:ro" "$image" "$@"
}

echo "puhu.exe under $(wine wine64 --version):"
wine sh /t/portable/run.sh /t/wine/puhu Z:/w > "$port/build/windows-cases.txt"
if diff "$port/build/windows-cases.txt" "$port/tests/portable/expected.txt" > "$port/build/windows-cases.diff"; then
    echo "  $(wc -l < "$port/build/windows-cases.txt" | tr -d ' ') speech and interface cases: same output as the Linux build"
else
    echo "  FAIL: cases differ from the Linux build, see build/windows-cases.diff"; fail=1
fi

sh "$port/tests/portable/edges.sh" "$port/build/puhu" "$voices" > "$port/build/edges-linux.txt"
wine sh /t/portable/edges.sh /t/wine/puhu Z:/w windows > "$port/build/edges-windows.txt"
if grep -v '^windows-' "$port/build/edges-windows.txt" | diff - "$port/build/edges-linux.txt" >/dev/null &&
   ! grep -q DIFFERENT "$port/build/edges-windows.txt" && grep -q '^windows-file-cp1252 same' "$port/build/edges-windows.txt"; then
    echo "  $(wc -l < "$port/build/edges-windows.txt" | tr -d ' ') sound files (file, redirect, pipe, code pages, PC speaker): same bytes as the Linux build's"
else
    echo "  FAIL: sound files differ, compare build/edges-windows.txt with build/edges-linux.txt"; fail=1
fi

# F2 twice: open the settings, save. The copy must appear in the user's folder; /w is read-only.
if wine sh -c 'wine64 Z:/w/puhu.exe --ui-script "0x13c 0x13c" >/dev/null 2>&1; test -s /wp/drive_c/users/root/AppData/Roaming/mikropuhe/JANNE.SMP'; then
    echo "  saving settings: a copy of the voice appears under %APPDATA%\\mikropuhe"
else
    echo "  FAIL: saving settings made no copy under %APPDATA%\\mikropuhe"; fail=1
fi

if command -v python3 >/dev/null 2>&1; then
    python3 "$here/ui_smoke.py" "$port/build/mikropuhe-windows" "$image" || fail=1
fi
exit $fail
