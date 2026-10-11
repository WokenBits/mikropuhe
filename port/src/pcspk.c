/* The virtual PC speaker. See pcspk.h for what is ported and what is modelled. */
#include <math.h>
#include <string.h>
#include <strings.h>

#include "pcspk.h"

#ifndef M_PI /* not in strict C99 headers */
#define M_PI 3.14159265358979323846
#endif

/*
 * Machine presets. loop_ns and overhead_ns are what the original's calibration would measure;
 * they were worked out from the published instruction timings of each processor for the two
 * loops it times (0x1583e and 0x1585c). They have not been measured on hardware.
 */
const mp_pcspk_timing MP_PCSPK_MACHINES[] = {
    {"ideal", "no machine: exact pulse widths", 1, 0, 0},
    {"486", "486DX2 at 66 MHz", 100, 800, 100},
    {"386", "386DX at 33 MHz", 400, 2500, 300},
    {"at", "PC/AT, 286 at 8 MHz", 1100, 7400, 1400},
    {"xt", "PC/XT, 8088 at 4.77 MHz", 3600, 33000, 6000},
    {NULL, NULL, 0, 0, 0},
};

const mp_pcspk_timing *mp_pcspk_machine(const char *name)
{
    const mp_pcspk_timing *t;
    for (t = MP_PCSPK_MACHINES; t->name; t++)
        if (strcasecmp(t->name, name) == 0) return t;
    return NULL;
}

/* 0x15291 */
int mp_pcspk_too_slow(uint32_t loop_ns, uint32_t overhead_ns)
{
    return loop_ns + overhead_ns >= MP_PCSPK_PERIOD_NS;
}

/* 0x15301-0x1537d: delay-loop passes for an interval of `ns`. Too short to time: 1. Otherwise the
 * interval less the fixed overhead, in passes, rounded to nearest, plus the one pass that is
 * already part of the overhead. */
static uint16_t passes(uint32_t ns, uint32_t loop_ns, uint32_t overhead_ns)
{
    uint32_t q, r;
    if ((loop_ns >> 1) + overhead_ns > ns) return 1;
    q = (ns - overhead_ns) / loop_ns;
    r = (ns - overhead_ns) % loop_ns;
    return (uint16_t)(q + (r * 2 >= loop_ns ? 1 : 0) + 1);
}

/* 0x152b7. For s = 0..128 the speaker-on interval is 22727 -+ s * 22727 / (64 + s / 2) ns: not a
 * straight line, small signals get twice the slope. */
void mp_pcspk_table(uint32_t loop_ns, uint32_t overhead_ns, uint16_t table[257])
{
    uint32_t s;
    if (loop_ns == 0) loop_ns = 1;
    for (s = 0; s <= 0x80; s++) {
        uint32_t v = s * MP_PCSPK_HALF_NS / (0x40 + (s >> 1));
        table[0x81 - s] = passes(MP_PCSPK_HALF_NS - v, loop_ns, overhead_ns);
        if (s == 0x80) break;
        table[0x81 + s] = passes(MP_PCSPK_HALF_NS + v, loop_ns, overhead_ns);
    }
    table[0] = (uint16_t)(table[0x100] + 1);
}

/* ---- pulse train -> PCM ---------------------------------------------------------------------- */

#define FIR_TAPS 385
#define OVERSAMPLE 8

static void emit(mp_pcspk *ps, double x)
{
    int i;
    long v;
    for (i = 0; i < ps->nbq; i++) { /* transposed direct form II */
        double y = ps->bq[i].b0 * x + ps->bq[i].z1;
        ps->bq[i].z1 = ps->bq[i].b1 * x - ps->bq[i].a1 * y + ps->bq[i].z2;
        ps->bq[i].z2 = ps->bq[i].b2 * x - ps->bq[i].a2 * y;
        x = y;
    }
    v = lrint(x * 16384.0); /* a full swing of the speaker bit is half of full scale */
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    ps->obuf[ps->on++] = (int16_t)v;
    if (ps->on == sizeof ps->obuf / sizeof ps->obuf[0]) {
        ps->out(ps->ctx, ps->obuf, ps->on);
        ps->on = 0;
    }
}

/* one slot of the oversampled signal: the fraction of it the speaker bit was on, as -1..+1 */
static void slot(mp_pcspk *ps, double level)
{
    ps->hist[ps->hpos] = (float)level;
    ps->hpos = (ps->hpos + 1) % FIR_TAPS;
    if (++ps->phase == OVERSAMPLE) {
        double y = 0;
        int k, j = ps->hpos;
        ps->phase = 0;
        for (k = 0; k < FIR_TAPS; k++) {
            j = j == 0 ? FIR_TAPS - 1 : j - 1;
            y += ps->fir[k] * ps->hist[j];
        }
        emit(ps, y);
    }
}

/* the speaker bit holds `on` for `ns` nanoseconds */
static void segment(mp_pcspk *ps, int on, double ns)
{
    while (ns > 0) {
        double room = ps->slot_ns - ps->pos_ns, d = ns < room ? ns : room;
        if (on) ps->high_ns += d;
        ps->pos_ns += d;
        ns -= d;
        if (ps->pos_ns >= ps->slot_ns - 1e-9) {
            slot(ps, 2.0 * ps->high_ns / ps->slot_ns - 1.0);
            ps->pos_ns = 0;
            ps->high_ns = 0;
        }
    }
}

/* 0x15643-0x1566a: speaker on, table[b + 1] passes, speaker off, the remaining passes */
void mp_pcspk_samples(mp_pcspk *ps, const uint8_t *p, size_t n)
{
    for (; n; n--, p++) {
        double on_ns = ps->on_fixed_ns + (ps->table[*p + 1] - 1) * ps->loop_ns;
        if (on_ns > ps->period_ns) on_ns = ps->period_ns;
        segment(ps, 1, on_ns);
        segment(ps, 0, ps->period_ns - on_ns);
    }
}

/* 0x156a7, 0x156df: the bit stays off while the timer chip counts n * 64 ticks */
void mp_pcspk_pause(mp_pcspk *ps, unsigned n)
{
    segment(ps, 0, n * 64.0 * 1e9 / MP_PIT_HZ);
}

void mp_pcspk_flush(mp_pcspk *ps)
{
    int i;
    for (i = 0; i < FIR_TAPS + OVERSAMPLE; i++) slot(ps, -1.0);
    if (ps->on) ps->out(ps->ctx, ps->obuf, ps->on);
    ps->on = 0;
}

static void biquad(mp_pcspk *ps, int type, double hz, double q, double gain_db)
{
    double w = 2 * M_PI * hz / ps->rate, c = cos(w), al = sin(w) / (2 * q), a0, A = pow(10, gain_db / 40);
    double b0, b1, b2, a1, a2;
    if (type == 0) { /* high-pass */
        b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    } else if (type == 1) { /* low-pass */
        b0 = (1 - c) / 2, b1 = 1 - c, b2 = (1 - c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    } else { /* peak */
        b0 = 1 + al * A, b1 = -2 * c, b2 = 1 - al * A, a0 = 1 + al / A, a1 = -2 * c, a2 = 1 - al / A;
    }
    ps->bq[ps->nbq].b0 = b0 / a0;
    ps->bq[ps->nbq].b1 = b1 / a0;
    ps->bq[ps->nbq].b2 = b2 / a0;
    ps->bq[ps->nbq].a1 = a1 / a0;
    ps->bq[ps->nbq].a2 = a2 / a0;
    ps->bq[ps->nbq].z1 = ps->bq[ps->nbq].z2 = 0;
    ps->nbq++;
}

void mp_pcspk_init(mp_pcspk *ps, const mp_pcspk_timing *t, unsigned rate, int speaker,
                   void (*out)(void *ctx, const int16_t *p, size_t n), void *ctx)
{
    double fc, sum = 0;
    int k;

    memset(ps, 0, sizeof *ps);
    mp_pcspk_table(t->loop_ns, t->overhead_ns, ps->table);
    ps->loop_ns = t->loop_ns;
    /* Between the two port writes with one pass each lie 6 of the 17 instructions of the loop,
     * about 30% of its time (0x15659-0x15664). The original's table assumes 100%, so on a slow
     * machine every pulse is shorter than intended by the other 70%. */
    ps->on_fixed_ns = 0.3 * t->overhead_ns;
    ps->period_ns = t->overhead_ns + t->extra_ns + (ps->table[0] - 2) * (double)t->loop_ns;
    ps->rate = rate;
    ps->os = OVERSAMPLE;
    ps->slot_ns = 1e9 / ((double)rate * OVERSAMPLE);
    ps->out = out;
    ps->ctx = ctx;
    for (k = 0; k < FIR_TAPS; k++) ps->hist[k] = -1.0f; /* the bit is off before the first sample */

    /* Low-pass before taking every 8th value: Blackman-windowed sinc. The cut-off is below the
     * pulse rate (20.5-22 kHz) so the carrier never reaches the output. */
    fc = 15000.0 < 0.42 * rate ? 15000.0 : 0.42 * rate;
    fc /= (double)rate * OVERSAMPLE;
    for (k = 0; k < FIR_TAPS; k++) {
        double m = k - (FIR_TAPS - 1) / 2.0;
        double h = m == 0 ? 2 * fc : sin(2 * M_PI * fc * m) / (M_PI * m);
        double w = 0.42 - 0.5 * cos(2 * M_PI * k / (FIR_TAPS - 1)) + 0.08 * cos(4 * M_PI * k / (FIR_TAPS - 1));
        ps->fir[k] = (float)(h * w);
        sum += h * w;
    }
    for (k = 0; k < FIR_TAPS; k++) ps->fir[k] = (float)(ps->fir[k] / sum);

    if (speaker == MP_SPEAKER_CONE) {
        /* A small paper-cone speaker in a metal case: no bass, a resonance around 2 kHz, little
         * above 6 kHz. Invented to sound plausible; not measured from any speaker. */
        biquad(ps, 0, 300.0, 0.707, 0);
        biquad(ps, 2, 2000.0, 1.4, 6.0);
        biquad(ps, 1, 6000.0 < 0.45 * rate ? 6000.0 : 0.45 * rate, 0.707, 0);
    } else {
        biquad(ps, 0, 20.0, 0.707, 0); /* only remove the standing level */
    }
}
