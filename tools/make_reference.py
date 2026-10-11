#!/usr/bin/env python3
"""Generate the reference set: what the original PUHU.EXE emits for fixed inputs.

Runs tools/puhu_emu.py's harness over a matrix of sentences, voices and
settings and stores each result as evidence/reference/<id>.events.json, with
MANIFEST.tsv listing the command tail, sample counts and the SHA-256 of the
expanded 8-bit stream. The port is correct when it reproduces these streams.
Only MANIFEST.tsv is kept in git; the streams are regenerated in seconds.

    tools/make_reference.py [--out evidence/reference] [--wav DIR]

Run twice to confirm the set is deterministic (MANIFEST.tsv must not change).
"""
import argparse
import hashlib
import json
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from puhu_emu import SAMPLE_RATE, Dos, Exit, expand  # noqa: E402

SENTENCES = {
    "s01": "hyvää päivää",
    "s02": "tervetuloa, olen mikropuhe 1 piste nolla.",
    "s03": "Kaikki ihmiset syntyvät vapaina ja tasavertaisina arvoltaan ja oikeuksiltaan.",
    "s04": "puhelin 123 markkaa",
    "s05": "Onko tämä kysymys? Kyllä on! Ehkä... niin: ei; tai, joo.",
    "s06": "abcdefghijklmnopqrstuvwxyzåäö",
    "s07": "1990 12:30 50% 3,5 $100 (suluissa) ala_viiva",
    "s08": "ässä öljy yö Åbo zeppeliini quiz wc taxi cesium ruotsi",
    "s09": "tuli tuuli tulli, taka takka taakka, kisa kissa",
    "s10": "0 7 10 11 21 100 101 1000 1234567 1000000000",
}
VOICES = {"jan": "/JAN", "kri": "/KRI", "sop": "/SOP"}
# single-setting variations, applied to Janne on s03 and s05. Digits 0..8 are the valid range;
# NOP9/KOR9/VOI9 pin down what the program does with an out-of-range digit.
VARIATIONS = ["NOP0", "NOP4", "NOP8", "KOR0", "KOR4", "KOR8", "KRV0", "KRV4", "KRV8",
              "VOI0", "VOI4", "VOI8", "KNP0", "KNP1", "ETP0", "ETP1", "NOP9", "KOR9", "VOI9"]


def cases():
    for v, vopt in VOICES.items():
        for s, text in SENTENCES.items():
            yield "%s-%s-default" % (v, s), "%s %s" % (vopt, text)
    for var in VARIATIONS:
        for s in ("s03", "s05"):
            yield "jan-%s-%s" % (s, var.lower()), "/JAN/%s %s" % (var, SENTENCES[s])


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dir", default=os.path.join(here, "..", "original", "files"))
    ap.add_argument("--out", default=os.path.join(here, "..", "evidence", "reference"))
    ap.add_argument("--wav", help="also write WAV files into this directory")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
    rows = ["id\texit\tsamples\tpause_samples\tpauses\tsha256_u8\ttail"]
    bad = 0
    for cid, tail in cases():
        dos = Dos(os.path.join(a.dir, "PUHU.EXE"), a.dir, (" " + tail).encode("cp437"))
        err = None
        try:
            dos.run()
        except Exit as e:
            err = str(e)
            bad += 1
        pcm = expand(dos.events)
        n_s = sum(len(v) for k, v in dos.events if k == "s")
        pauses = [v for k, v in dos.events if k == "p"]
        with open(os.path.join(a.out, cid + ".events.json"), "w") as f:
            json.dump({"id": cid, "tail": " " + tail, "exit_code": dos.exit_code, "error": err,
                       "sample_rate_nominal": SAMPLE_RATE,
                       "events": [[k, v.hex() if k == "s" else v] for k, v in dos.events]}, f)
        if a.wav:
            with wave.open(os.path.join(a.wav, cid + ".wav"), "wb") as wv:
                wv.setnchannels(1)
                wv.setsampwidth(1)
                wv.setframerate(SAMPLE_RATE)
                wv.writeframes(pcm)
        rows.append("%s\t%s\t%d\t%d\t%d\t%s\t%s" % (
            cid, dos.exit_code if err is None else "ERR " + err, n_s, sum(pauses), len(pauses),
            hashlib.sha256(pcm).hexdigest(), tail))
    open(os.path.join(a.out, "MANIFEST.tsv"), "w").write("\n".join(rows) + "\n")
    print("%d cases, %d failed -> %s" % (len(rows) - 1, bad, a.out))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
