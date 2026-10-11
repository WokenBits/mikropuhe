#!/usr/bin/env python3
"""Make a voice for MikroPuhe in which nothing is recorded: every sound is computed here.

    tools/make_synth_voice.py DIR            writes the voice as a directory for smptool.py
    tools/smptool.py pack DIR voices/KONE.SMP

The directory has the layout `smptool.py unpack` produces (docs/voice-format.md). Everything in
it is this project's own: the sound units come from the formulas below, the parameters, the pitch
contours and the exception list were written for this voice. Only the file format, and the 51
bytes of signature every voice file must start with, are the original's.

How the sounds are made (22,000 samples a second, 8 bits, 128 = silence):
  vowels, j l m n v ng   one pitch period: what a chain of resonators (the formants) answers to
                         one pulse per period, once it has settled
  h s f                  noise through resonators
  r                      a short stretch of a neutral vowel with a dip in loudness: one tap
  b d g k p t            a burst of noise coloured by the vowel that follows; the voiced ones
                         with a low hum in front

Pure Python, no modules beyond the standard library. The noise uses its own generator, so the
output is the same on every machine.
"""
import math
import os
import struct
import sys
import wave

RATE = 22000
PERIOD = 200  # samples: 110 Hz

# Formant frequencies in Hz (F1, F2, F3) of the eight Finnish vowels, textbook values for a male
# speaker, rounded. F4 is fixed.
VOWELS = {
    "a": (700, 1100, 2500), "e": (480, 2000, 2600), "i": (280, 2300, 3000), "o": (500, 900, 2500),
    "u": (320, 700, 2400), "y": (300, 1800, 2300), "ae": (700, 1700, 2500), "oe": (480, 1550, 2300),
}
VOWEL_ORDER = ["a", "e", "i", "o", "u", "y", "ae", "oe"]  # unit numbers 1..8 of table 3
VOWEL_RMS = {"a": 32, "e": 30, "i": 26, "o": 31, "u": 27, "y": 25, "ae": 31, "oe": 29}
BANDWIDTHS = (80, 100, 160, 220)
F4 = 3500
NEUTRAL = (500, 1500, 2500)

# j l m n v ng: formants, loudness, and how dull (0..1: share of an extra low-pass)
VOICED = [("j", (270, 2200, 3000), 14, 0.2), ("l", (360, 1250, 2600), 11, 0.4), ("m", (250, 1000, 2200), 8, 0.8),
          ("n", (250, 1500, 2500), 9, 0.75), ("v", (300, 1000, 2300), 9, 0.6), ("ng", (250, 2000, 2800), 9, 0.85)]


class Noise:
    """A fixed pseudo-random sequence, -1..1."""

    def __init__(self, seed):
        self.x = seed

    def __call__(self):
        self.x = (self.x * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.x >> 8) / float(1 << 22) - 1.0


def resonator(x, f, bw):
    """A two-pole resonator at f Hz with bandwidth bw, gain 1 at 0 Hz."""
    c = -math.exp(-2 * math.pi * bw / RATE)
    b = 2 * math.exp(-math.pi * bw / RATE) * math.cos(2 * math.pi * f / RATE)
    a = 1 - b - c
    y1 = y2 = 0.0
    out = []
    for v in x:
        y = a * v + b * y1 + c * y2
        out.append(y)
        y2, y1 = y1, y
    return out


def lowpass(x, k):
    y = 0.0
    out = []
    for v in x:
        y += k * (v - y)
        out.append(y)
    return out


def formants(x, f, widths=BANDWIDTHS):
    for freq, bw in zip(tuple(f) + (F4,), widths):
        x = resonator(x, freq, bw)
    return x


def scaled(x, rms, peak=118):
    """Centre on zero and bring to the wanted loudness, without clipping."""
    m = sum(x) / len(x)
    x = [v - m for v in x]
    r = math.sqrt(sum(v * v for v in x) / len(x)) or 1.0
    g = rms / r
    top = max(abs(v) for v in x) * g
    if top > peak:
        g *= peak / top
    return [v * g for v in x]


def to_bytes(x):
    return bytes(max(0, min(255, int(round(128 + v)))) for v in x)


def period_of(f, rms, dull=0.0, widths=BANDWIDTHS):
    """One settled pitch period. The answer of the filters to a single pulse is computed over many
    periods and folded into one, which is what an endless pulse train would give."""
    n = PERIOD * 24
    pulse = [0.0] * n
    pulse[0] = 1.0
    src = lowpass(pulse, 0.25)  # the voice source falls off with frequency
    y = formants(src, f, widths)
    if dull:
        d = lowpass(lowpass(y, 0.12), 0.12)
        y = [(1 - dull) * a + dull * b for a, b in zip(y, d)]
    one = [sum(y[i + k * PERIOD] for k in range(24)) for i in range(PERIOD)]
    return scaled(one, rms)


def fade(x, n_in, n_out):
    x = list(x)
    for i in range(min(n_in, len(x))):
        x[i] *= i / float(n_in)
    for i in range(min(n_out, len(x))):
        x[-1 - i] *= i / float(n_out)
    return x


def noise_through(seed, n, bands, rms, hp=False):
    """n samples of noise through resonators given as (frequency, bandwidth, weight)."""
    rnd = Noise(seed)
    src = [rnd() for _ in range(n + 400)]
    if hp:  # take the lows out first
        low = lowpass(src, 0.3)
        src = [a - b for a, b in zip(src, low)]
    mix = [0.0] * len(src)
    for f, bw, w in bands:
        r = resonator(src, f, bw)
        mix = [a + w * b for a, b in zip(mix, r)]
    return scaled(mix[400:], rms)


def burst(seed, vowel, kind, rms):
    """The release of a stop before `vowel` (None: no vowel follows)."""
    f1, f2, f3 = VOWELS[vowel] if vowel else NEUTRAL
    if kind == "p":    # lips: low and short
        n, bands, decay = 200, [(f1 * 0.7, 300, 1.0), (f2 * 0.75, 400, 0.5)], 45.0
    elif kind == "t":  # tongue tip: high and sharp
        n, bands, decay = 260, [(1800 + 0.25 * (f2 - 1800), 500, 0.5), (4200, 900, 1.0), (f3, 500, 0.4)], 55.0
    else:              # k: where the following vowel has its second formant, and longer
        n, bands, decay = 440, [(f2, 300, 1.0), (f3 * 1.05, 500, 0.4), (f1, 300, 0.25)], 110.0
    x = noise_through(seed, n, bands, 1.0, hp=(kind == "t"))
    x = [v * math.exp(-i / decay) for i, v in enumerate(x)]
    x[0] *= 0.3
    return fade(scaled(x, rms, peak=90), 2, 30)


def hum(periods, rms):
    """The low hum of the vocal cords behind closed lips: b d g start with it."""
    one = period_of((180, 900, 2200), 1.0, dull=0.95)
    x = []
    for _ in range(periods):
        x += one
    return fade(scaled(x, rms), PERIOD, PERIOD // 2)


def write_wav(path, x):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(RATE)
        w.writeframes(to_bytes(x))


SETTINGS = [("speed", 2), ("pitch", 4), ("pitch_variation", 0), ("volume", 4), ("consonant_emphasis", 1),
            ("first_syllable_stress", 1), ("speak_underscore", 0), ("decimal_dot", 0), ("brackets_quieter", 1),
            ("c_as_k", 0), ("keyboard_hook", 1), ("word11", 1), ("word12", 0), ("sentence_pauses", 1)]

# Durations in pitch periods of 200 samples (9.1 ms) unless the name says samples.
PARAMETERS = [
    ("stop_slot_samples", 2000), ("doubled_stop_extra_samples", 2800),
    ("h_repeats", 1), ("r_repeats", 2), ("s_repeats", 1), ("hh_repeats", 3), ("rr_repeats", 4), ("ss_repeats", 3),
    ("vowel_periods_stress0", 10), ("vowel_periods_stress1", 9), ("vowel_periods_stress2", 8),
    ("diphthong_glide_periods_stress0", 5), ("diphthong_glide_periods_stress1", 4),
    ("diphthong_glide_periods_stress2", 4),
    ("diphthong_second_periods_stress0", 8), ("diphthong_second_periods_stress1", 7),
    ("diphthong_second_periods_stress2", 6),
    ("consonant_vowel_glide_periods", 3), ("voiced_periods_between_vowels", 2), ("voiced_periods_before_stop", 7),
    ("voiced_periods_word_end", 10), ("doubled_voiced_extra_periods", 11),
    ("voiced_start_pitch", -4), ("voiced_pitch_step", -1),
    ("pitch_offset_level1", 4), ("pitch_offset_level2", 0), ("pitch_offset_level3", -12),
    ("period_samples", PERIOD), ("voiced_periods_other", 5),
    ("long_vowel_extra_periods_stress0", 4), ("long_vowel_extra_periods_stress1", 3),
    ("long_vowel_extra_periods_stress2", 3),
]


def contours():
    """Three pitch courses of 256 steps: samples added to each period (negative = higher). A machine
    speaks flatter than a person: each is a gentle fall, the stressed ones higher."""
    rows = []
    for i in range(256):
        t = i / 255.0
        ease = t * t * (3 - 2 * t)
        rows.append((int(round(4 + 10 * ease)), int(round(-14 + 10 * ease)), int(round(-28 + 10 * ease))))
    return rows


# What a word is replaced by before it is spoken. In a replacement "-" joins words without stress
# and "&" is a short pause. Written for this voice: the Finnish names of letters and signs, some
# abbreviations, and a few words whose spelling is not Finnish.
EXCEPTIONS = """
a	aa
b	bee
c	see
d	dee
e	ee
f	äff
g	gee
h	hoo
i	ii
j	jii
k	koo
l	äll
m	ämm
n	änn
o	oo
p	pee
q	kuu
r	ärr
s	äss
t	tee
u	uu
v	vee
w	kaksois-vee
x	äks
y	yy
z	tset
å	ruotsalainen-oo
ä	ää
ö	öö
!	huutomerkki
?	kysymysmerkki
.	piste
,	pilkku
:	kaksoispiste
;	puolipiste
-	viiva
+	plus
=	on-yhtä-kuin
%	prosenttia
&	ja
@	ät
#	risuaita
$	dollaria
£	puntaa
/	kautta
\\	kenoviiva
(	sulku-auki
)	sulku-kiinni
[	hakasulku-auki
]	hakasulku-kiinni
{	aaltosulku-auki
}	aaltosulku-kiinni
*	tähti
'	heittomerkki
"	lainausmerkki
_	alaviiva
<	pienempi-kuin
>	suurempi-kuin
^	hattu
~	aaltoviiva
|	pystyviiva
°	astetta
esim	esimerkiksi
jne	ja-niin-edelleen
ym	ynnä-muuta
yms	ynnä-muuta-sellaista
ks	katso
klo	kello
nro	numero
puh	puhelin
kpl	kappaletta
kg	kiloa
km	kilometriä
cm	senttimetriä
eur	euroa
mk	markkaa
ma	maanantai
ti	tiistai
ke	keskiviikko
to	torstai
pe	perjantai
la	lauantai
su	sunnuntai
pc	pee-see
dos	doss
kitt	kit
michael	maikkel
linux	linuks
windows	vindous
mac	mäk
github	git-hab
wifi	vai-fai
""".strip("\n")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    units = os.path.join(out, "units")
    raw = os.path.join(out, "raw")
    os.makedirs(units, exist_ok=True)
    os.makedirs(raw, exist_ok=True)

    with open(os.path.join(out, "voice.ini"), "w", encoding="utf-8") as f:
        f.write("; A synthetic voice, generated by tools/make_synth_voice.py. Meanings: docs/voice-format.md\n")
        f.write("[voice]\nname = Kone\n\n[settings]\n")
        f.write("".join("%s = %d\n" % kv for kv in SETTINGS))
        f.write("\n[parameters]\n")
        f.write("".join("%s = %d\n" % kv for kv in PARAMETERS))
    with open(os.path.join(out, "contours.tsv"), "w") as f:
        f.write("stress0\tstress1\tstress2\n")
        f.write("".join("%d\t%d\t%d\n" % r for r in contours()))
    with open(os.path.join(out, "exceptions.tsv"), "w", encoding="utf-8") as f:
        f.write(EXCEPTIONS + "\n")
    for name, data in (("block0-0x040-0x100.bin", bytes(0xC0)), ("block0-0x400-end.bin", bytes(1976)),
                       ("exceptions-word2.bin", bytes(2)), ("settings-pad.bin", b"\xff" * 18), ("trailer.bin", b"")):
        with open(os.path.join(raw, name), "wb") as f:
            f.write(data)

    def unit(table, number, name, variant, samples):
        write_wav(os.path.join(units, "t%d-u%02d-%s-v%d.wav" % (table, number, name, variant)), samples)

    # table 3: one pitch period each. A vowel's second variant follows m or n: a little nasal.
    murmur = period_of((250, 1200, 2300), 1.0, dull=0.8)
    for n, v in enumerate(VOWEL_ORDER, 1):
        p = period_of(VOWELS[v], VOWEL_RMS[v])
        unit(3, n, v, 1, p)
        nasal = [0.8 * a + 0.2 * VOWEL_RMS[v] * b for a, b in zip(p, murmur)]
        unit(3, n, v, 2, scaled(nasal, VOWEL_RMS[v] * 0.9))
    for n, (name, f, rms, dull) in enumerate(VOICED, 9):
        unit(3, n, name, 1, period_of(f, rms, dull=dull))

    # table 2: h, r, s
    unit(2, 1, "h", 1, fade(noise_through(11, 1600, [(500, 300, 1.0), (1500, 400, 0.7), (2500, 500, 0.5)], 3.0), 300, 400))
    schwa = period_of(NEUTRAL, 1.0)
    tap = []
    for k in range(4):
        tap += schwa
    tap = [v * (0.12 + 0.88 * abs(math.cos(math.pi * i / len(tap)))) for i, v in enumerate(tap)]
    unit(2, 2, "r", 1, scaled(tap, 9.0))
    unit(2, 3, "s", 1, fade(noise_through(13, 1700, [(5200, 1500, 1.0), (7800, 2000, 0.8)], 5.0, hp=True), 250, 300))

    # table 1: b d f g k p t. d k p t have nine variants: alone, then before a e i o u y ä ö.
    before = [None] + VOWEL_ORDER
    unit(1, 1, "b", 1, hum(8, 5.0) + burst(21, None, "p", 5.0))
    for var, v in enumerate(before, 1):
        unit(1, 2, "d", var, hum(3, 5.0) + burst(30 + var, v, "t", 5.5))
    unit(1, 3, "f", 1, fade(noise_through(17, 2400, [(1500, 2500, 0.6), (5000, 3000, 1.0)], 3.2, hp=True), 300, 400))
    unit(1, 4, "g", 1, hum(7, 5.0) + burst(41, None, "k", 5.0))
    for kind, number, loud in (("k", 5, 11.0), ("p", 6, 8.5), ("t", 7, 8.5)):
        for var, v in enumerate(before, 1):
            unit(1, number, kind, var, burst(50 + 10 * number + var, v, kind, loud if v else loud * 0.6))
    print("%s: a synthetic voice, %d unit files" % (out, len(os.listdir(units))))


if __name__ == "__main__":
    main()
