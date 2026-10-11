#!/bin/sh
# The parts of puhu that touch the system, on any system: files, redirection, pipes, file names,
# and the floating-point rendering of the virtual PC speaker. One line per case with a hash of the
# sound file; the lines must be the same on every system. With a third argument "windows" it also
# checks what only Windows has: text in the old code pages (prints "same" or "DIFFERENT").
#
#   edges.sh PUHU VOICESDIR [windows]
PUHU=$1
V=$2
t=$(mktemp -d) && cd "$t" || exit 1
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -c1-64; else shasum -a 256 | cut -c1-64; fi; }
p() { "$PUHU" --voices-dir "$V" "$@"; }
p -v sophia -o a.wav "Hei Michael, tämä on KITT";                       echo "to-file $(sha < a.wav)"
p -v sophia "Hei Michael, tämä on KITT" > b.wav;                        echo "redirected $(sha < b.wav)"
echo "Hei Michael, tämä on KITT" | p -v sophia -o c.wav;                echo "piped-in $(sha < c.wav)"
p -o "ääkköset.wav" "hyvää päivää";                                  echo "file-name $(sha < ääkköset.wav)"
p -d pcspeaker -o d.wav "hyvää päivää";                              echo "pcspeaker-386-cone $(sha < d.wav)"
p -d pcspeaker --machine xt --speaker none -o e.wav "hyvää päivää";  echo "pcspeaker-xt-none $(sha < e.wav)"
if [ "$3" = windows ]; then
    same() { if [ "$(sha < "$1")" = "$(sha < "$2")" ]; then echo same; else echo DIFFERENT; fi; }
    # cmd's "echo" writes the console's code page: ä is byte 0x84 in 850 and 437, and lines end CR LF
    printf 'Hei Michael, t\204m\204 on KITT\r\n' | p -v sophia -o f.wav;  echo "windows-pipe-cp850 $(same f.wav c.wav)"
    # a text file in the "ANSI" code page 1252: ä is byte 0xE4
    printf 'hyv\344\344 p\344iv\344\344\r\n' > ansi.txt
    p -f ansi.txt -o g.wav;                                            echo "windows-file-cp1252 $(same g.wav ääkköset.wav)"
fi
cd / && rm -rf "$t"
