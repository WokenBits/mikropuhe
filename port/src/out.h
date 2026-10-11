/* Where puhu's speech ends up: an emulated output device, then a file, a pipe or a sound card. */
#ifndef MP_OUT_H
#define MP_OUT_H

#include <stddef.h>
#include <stdint.h>

#include "mikropuhe.h"
#include "pcspk.h"

enum { MP_DEV_DAC, MP_DEV_PCSPEAKER };

typedef struct mp_out {
    int device;
    int nominal_pauses; /* dac: a pause of n is n samples instead of n * 64 timer ticks */
    int events;         /* tests: print "s hex" and "p n" lines instead of making audio */
    int quiet;          /* full-screen interface: no messages on the terminal, and no falling back to
                           opening the recording in VS Code (it speaks every few seconds) */
    mp_pcspk speaker;   /* pcspeaker */
    /* the finished audio: mono, 8-bit unsigned or 16-bit signed */
    uint8_t *data;
    size_t len, cap;
    unsigned rate, bits;
    double carry;
} mp_out;

void mp_out_init_dac(mp_out *o);
void mp_out_init_pcspeaker(mp_out *o, const mp_pcspk_timing *machine, unsigned rate, int speaker);
mp_sink mp_out_sink(mp_out *o);
void mp_out_finish(mp_out *o);

/* path: NULL = play, "-" = WAV on standard output, *.mp3 = through lame or ffmpeg, else a WAV file.
 * alsa_device: NULL = the default device, with the external players as fallback. */
int mp_out_deliver(const mp_out *o, const char *path, const char *alsa_device);
void mp_out_list_alsa(void);
void mp_out_why_silent(void); /* explains, for this machine, why nothing may be heard */

#endif
