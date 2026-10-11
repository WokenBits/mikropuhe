# Wine, to run the cross-compiled puhu.exe on a Linux machine (tests/wine/check.sh).
# Nothing of the project is in the image; the program and the voices are mounted read-only at run time.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends wine64 && rm -rf /var/lib/apt/lists/*
ENV PATH=/usr/lib/wine:$PATH WINEDEBUG=-all WINEPREFIX=/wp
RUN wine64 wineboot --init >/dev/null 2>&1; wineserver64 -w
