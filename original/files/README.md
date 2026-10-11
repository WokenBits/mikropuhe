# The original files go here

This copy of the project does not contain the original program or its voices: they are
Timehouse Oy's. To build and run `puhu`, put your own copies of these four files in this folder:

    PUHU.EXE  JANNE.SMP  KRISTIAN.SMP  SOPHIA.SMP

and check them:

    cd original && sha256sum -c SHA256SUMS      # the archive's line fails; that file is not needed

The build reads `PUHU.EXE` once, to generate two small C headers with the program's number words
and interface texts (`make -C port` does it; it needs Python 3). `puhu` then loads the voices
from here.
