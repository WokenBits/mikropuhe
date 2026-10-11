# Voices made for this project

| File | What |
|---|---|
| `KONE.SMP` | "Kone" (machine): a synthetic voice. Every sound is computed by `tools/make_synth_voice.py`; nothing is recorded, and nothing is taken from the original voices. |

```bash
puhu -v kone "hyvää päivää"
make -C port kone            # generate it again
```

How it is made, what was checked and what was not: [`docs/voice-format.md`](../docs/voice-format.md#kone-a-voice-with-nothing-recorded-in-it).

The three original voices (Janne, Kristian, Sophia) are Timehouse Oy's and live in `original/files/`.
