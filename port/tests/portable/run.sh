#!/bin/sh
# The same inputs through a build of puhu on any system: one line per case with a hash of what the
# program printed. Two builds that print the same lines behave the same for these cases
# (docs/porting.md). Needs only sh and sha256sum or shasum.
#
#   run.sh PUHU VOICESDIR                  PUHU: the program, or a wrapper that starts it
#   run.sh PUHU VOICESDIR | diff - expected.txt
#
# cases.tsv: kind, name, argument. T = a text, run with --events (the sample stream as text);
# U = key codes for the full-screen interface, run with --ui-script (the final screen as text).
PUHU=$1
V=$2
here=$(cd "$(dirname "$0")" && pwd)
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -c1-64; else shasum -a 256 | cut -c1-64; fi; }
while IFS="$(printf '\t')" read -r kind id arg; do
    if [ "$kind" = T ]; then
        h=$("$PUHU" --voices-dir "$V" --events -- "$arg" 2>&1 | sha)
    else
        h=$("$PUHU" --voices-dir "$V" --ui-script "$arg" 2>&1 | sha)
    fi
    echo "$kind $id $h"
done < "$here/cases.tsv"
