/*
 * The virtual PC speaker: the output device PUHU.EXE itself drove.
 *
 * Two parts with different standing:
 *  - mp_pcspk_table and the pulse timing follow the original's code (0x152b7, 0x15616) and are
 *    checked against it (tests/check_pcspk.py);
 *  - the machine presets and the loudspeaker filter are a model of hardware, written for this
 *    port. They are estimates, not measurements.
 */
#ifndef MP_PCSPK_H
#define MP_PCSPK_H

#include <stddef.h>
#include <stdint.h>

#define MP_PCSPK_HALF_NS 22727u   /* 0x58C7: half of the pulse period the program aims for */
#define MP_PCSPK_PERIOD_NS 45454u /* 0xB18E: one sample; also the "too slow" limit */
#define MP_PIT_HZ 1193182.0       /* a pause unit is 64 ticks of the PC's timer chip (0x156df) */

/* What the original measures at start-up (0x15167), plus what it does not measure. */
typedef struct mp_pcspk_timing {
    const char *name, *what;
    uint32_t loop_ns;     /* DS:0x1dd4: one pass of a LOOP instruction */
    uint32_t overhead_ns; /* DS:0x1dd8: one sample through the player's inner loop with both delays at 1 */
    uint32_t extra_ns;    /* per sample in the real player but not in the calibration loop (0x15648, 0x1564d) */
} mp_pcspk_timing;

extern const mp_pcspk_timing MP_PCSPK_MACHINES[];
const mp_pcspk_timing *mp_pcspk_machine(const char *name);

/* 0x15291: the program refuses to speak when this is true */
int mp_pcspk_too_slow(uint32_t loop_ns, uint32_t overhead_ns);

/* 0x152b7: table[0] = delay-loop passes per sample; table[b + 1] = passes with the speaker on for sample b */
void mp_pcspk_table(uint32_t loop_ns, uint32_t overhead_ns, uint16_t table[257]);

enum { MP_SPEAKER_NONE, MP_SPEAKER_CONE };

typedef struct mp_pcspk {
    uint16_t table[257];
    double loop_ns, on_fixed_ns, period_ns;
    /* pulse train -> PCM */
    unsigned rate, os;
    double slot_ns, pos_ns, high_ns;
    float fir[385], hist[385];
    int hpos, phase;
    struct { double b0, b1, b2, a1, a2, z1, z2; } bq[3];
    int nbq;
    void (*out)(void *ctx, const int16_t *p, size_t n);
    void *ctx;
    int16_t obuf[2048];
    size_t on;
} mp_pcspk;

void mp_pcspk_init(mp_pcspk *ps, const mp_pcspk_timing *t, unsigned rate, int speaker,
                   void (*out)(void *ctx, const int16_t *p, size_t n), void *ctx);
void mp_pcspk_samples(mp_pcspk *ps, const uint8_t *p, size_t n); /* what dev_write queued */
void mp_pcspk_pause(mp_pcspk *ps, unsigned n);                   /* a pause record */
void mp_pcspk_flush(mp_pcspk *ps);

#endif
