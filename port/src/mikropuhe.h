/*
 * MikroPuhe 1.0 speech core, re-implemented from PUHU.EXE (Timehouse Oy, 1990).
 *
 * Every function names the address of the original routine it follows. Addresses are
 * linear with the DOS image at segment 0x1000, as in notes/FUNCTIONS.md. Arithmetic is
 * 16-bit where the original's was, because the output has to match sample for sample.
 */
#ifndef MIKROPUHE_H
#define MIKROPUHE_H

#include <stddef.h>
#include <stdint.h>

#define MP_SAMPLE_RATE 22000 /* nominal: one sample per 45,454 ns pulse */
#define MP_RING_SIZE 64000   /* ring buffer size the original uses with enough free memory */

/* Where the speech goes. A pause of n is n timer units, not n samples: see README. */
typedef struct mp_sink {
    void (*samples)(void *ctx, const uint8_t *p, size_t n); /* 8-bit unsigned, never 0 */
    void (*pause)(void *ctx, unsigned n);
    void *ctx;
} mp_sink;

/* The 14 setting words, in the order of the .SMP file and of the original's block at DS:0x1b19. */
enum {
    MP_SPEED,       /* NOP 0..8, 0 slowest */
    MP_PITCH,       /* KOR 0..8, 4 neutral */
    MP_PITCH_VAR,   /* KRV 0..8 */
    MP_VOLUME,      /* VOI 0..8, 4 unity */
    MP_CONSONANTS,  /* KNP flag: consonant emphasis */
    MP_STRESS,      /* ETP flag: first-syllable stress */
    MP_UNDERSCORE,  /* flag: a lone underscore is spoken */
    MP_DECIMAL_DOT, /* flag: decimal separator is '.', otherwise ',' */
    MP_PAREN_QUIET, /* flag: bracketed text quieter */
    MP_C_AS_K,      /* flag: letter c spoken as k, otherwise as s */
    MP_KEYBOARD,    /* flag: original hooks the keyboard while speaking; unused here */
    MP_W11,
    MP_W12,
    MP_SENTENCE_PAUSE, /* flag: . : ? ! at a word end give a pause */
    MP_NSETTINGS
};

typedef struct mp_voice {
    char name[33];
    int16_t saved[MP_NSETTINGS]; /* settings stored in the file */
    uint8_t *blk0, *blk1, *blk2; /* parameters, exceptions, waveform units */
    size_t len0, len1, len2;
} mp_voice;

typedef struct mp_state {
    const mp_voice *v;
    int16_t set[MP_NSETTINGS]; /* active settings, DS:0x1b19.. */
    int16_t level;             /* DS:0x1b13 sentence pitch level 1..3 */
    uint8_t first;             /* DS:0x1d9a nothing spoken yet in this call */
    uint8_t paren;             /* DS:0x1b47 0 off, 1 armed, 2 inside brackets */
    uint8_t digits;            /* DS:0x1b08 numbers are read digit by digit (after "puhelin") */
    uint8_t c_char;            /* DS:0x1b12 replacement for the letter c */
    uint8_t decimal;           /* DS:0x1b54 decimal separator */
    uint8_t has_tbl4;          /* DS:0x1da2 block 2 has a fourth table */
    uint16_t period;           /* DS:0x1d9f samples per pitch period */
    uint8_t buf[0x200];        /* DS:0x1b5b work buffer */
    char gbuf[0x200];          /* the 0x100-byte scratch string the original allocates per call (DS:0x1d7e) */
    uint16_t ring_size, ring_wr; /* DS:0x1fe0, DS:0x1dce: only their effect on pauses is kept */
    uint8_t stack[0x700];        /* image of the stack under speak_phonemes, kept between calls: see synth.c */
    void (*trace)(struct mp_state *st, const char *what, const char *s); /* tests: called at stage boundaries */
    int path;                    /* tests: path of the current speak_phonemes call */
    mp_sink sink;
} mp_state;

/* voice.c */
int mp_voice_load(mp_voice *v, const char *path, char *err, size_t errlen);
void mp_voice_free(mp_voice *v);

/* synth.c */
void mp_init(mp_state *st, const mp_voice *v, mp_sink sink);
void mp_set(mp_state *st, const int16_t set[MP_NSETTINGS]); /* 0x14605 */
void mp_dev_start(mp_state *st, uint16_t ring_size);        /* 0x1548a */
void mp_dev_pause(mp_state *st, uint16_t n);                /* 0x1551d */
int mp_speak_phonemes(mp_state *st, char *s, int path);     /* 0x13543; s is rewritten */

/* Which caller reaches speak_phonemes: the value is where its stack frame sits relative to the
 * frame of a call made for a word. It only matters for the stale bytes described in synth.c. */
#define MP_PATH_WORD 0
#define MP_PATH_NUMBER (-0x106)
#define MP_PATH_CHAR 0x12
uint8_t *mp_frame(mp_state *st, int path); /* the 0x360-byte frame image for a path */

/* text.c */
int mp_speak(mp_state *st, const char *text); /* 0x126eb: one piece of text, CP437 */

#endif
