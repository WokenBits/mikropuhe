/* Test helper for the virtual PC speaker.
 *
 *   pcspk_dump table LOOP_NS OVERHEAD_NS      the pulse-width table and the passes per sample value,
 *                                             in the format of tools/pcspk_oracle.py
 *   pcspk_dump level MACHINE RATE VALUE       renders 11000 samples of one value with no filter and
 *                                             prints: expected duty, measured mean level as duty,
 *                                             residual ripple (RMS), samples out, pulse period in ns
 *   pcspk_dump pause MACHINE RATE N           renders a pause of N and prints samples out
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/pcspk.h"

static double sum, sumsq;
static size_t count, skip, window, used;

static void collect(void *ctx, const int16_t *p, size_t n)
{
    size_t i;
    (void)ctx;
    for (i = 0; i < n; i++, count++) {
        if (count < skip || used >= window) continue;
        sum += p[i];
        sumsq += (double)p[i] * p[i];
        used++;
    }
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "table") == 0) {
        uint16_t t[257];
        int i;
        mp_pcspk_table((uint32_t)atol(argv[2]), (uint32_t)atol(argv[3]), t);
        printf("T");
        for (i = 0; i < 257; i++) printf(" %u", t[i]);
        printf("\n");
        for (i = 1; i < 256; i++) printf("b %d %u %u\n", i, t[i + 1], t[0] - t[i + 1]);
        return 0;
    }
    if (argc == 5 && (strcmp(argv[1], "level") == 0 || strcmp(argv[1], "pause") == 0)) {
        static mp_pcspk ps;
        const mp_pcspk_timing *m = mp_pcspk_machine(argv[2]);
        unsigned rate = (unsigned)atoi(argv[3]);
        int v = atoi(argv[4]), i;
        if (!m) return 2;
        mp_pcspk_init(&ps, m, rate, MP_SPEAKER_NONE, collect, NULL);
        ps.nbq = 0; /* no filter at all: the standing level is what is measured */
        if (argv[1][0] == 'p') {
            mp_pcspk_pause(&ps, (unsigned)v);
            mp_pcspk_flush(&ps);
            printf("%zu\n", count);
            return 0;
        }
        skip = rate / 10; /* let the low-pass settle */
        window = rate / 4;
        for (i = 0; i < 11000; i++) {
            uint8_t b = (uint8_t)v;
            mp_pcspk_samples(&ps, &b, 1);
        }
        {
            double on_ns = ps.on_fixed_ns + (ps.table[v + 1] - 1) * ps.loop_ns;
            double mean, rms;
            mp_pcspk_flush(&ps);
            mean = sum / (double)used;
            rms = sqrt(sumsq / (double)used - mean * mean);
            /* level -1..+1 is scaled by 16384; duty = (level + 1) / 2 */
            printf("%.6f %.6f %.3f %zu %.3f\n", on_ns / ps.period_ns, (mean / 16384.0 + 1) / 2, rms, count,
                   ps.period_ns);
        }
        return 0;
    }
    fprintf(stderr, "usage: see the comment at the top of tests/pcspk_dump.c\n");
    return 2;
}
