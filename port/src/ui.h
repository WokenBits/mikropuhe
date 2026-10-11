/* The full-screen interface of the original program (what PUHU started with no arguments). */
#ifndef MP_UI_H
#define MP_UI_H

#include <stdint.h>

/* What the interface needs from the program around it. */
typedef struct mp_ui_host {
    void *ctx;
    int (*speak)(void *ctx, const char *cp437);   /* 0x11fbf: say this, and return when it has been said.
                                                     Returns 0, or 1 if it could not be made audible. */
    int16_t *set;                                 /* the 14 setting words (DS:0x1a98) */
    void (*apply)(void *ctx);                     /* 0x14605: make them the active settings */
    int *voice;                                   /* current voice, 0..2 (DS:0x1a96) */
    void (*select_voice)(void *ctx, int old);     /* 0x12398: load voice *voice; back to `old` if that fails */
    const char *(*voice_name)(void *ctx);         /* name to show for the current voice */
    int (*save)(void *ctx);                       /* 0x11563: settings into the voice file; 0, or 5 = protected */
    void (*leaving)(void *ctx);                   /* the terminal is the user's again: say what there is to say */
} mp_ui_host;

/* Runs the interface on the terminal until Esc. Returns the exit status. */
int mp_ui_run(const mp_ui_host *host);

/* Tests: runs it without a terminal on the given key codes (a character, or 0x100 + scan code for
 * F1 0x13b, F2 0x13c, Up 0x148, Down 0x150, Left 0x14b, Right 0x14d, Home 0x147, End 0x14f, Del 0x153,
 * Ctrl-Left 0x173, Ctrl-Right 0x174). When the keys run out, or the program ends, the screen is
 * printed as JSON on standard output. */
int mp_ui_script(const mp_ui_host *host, const int *keys, int nkeys);

#endif
