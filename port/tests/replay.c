/* Replays a stage trace from tools/puhu_emu.py (Dos.enable_trace) through the C core.
 *
 *   replay VOICE.SMP < trace.txt
 *
 * Input lines:  S size w0..w13 | G level first paren volume | F hex (frame image) | W hex | P n | E
 * Output lines: s hex | p n | R n hex total
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mikropuhe.h"

static unsigned long total;

static void on_samples(void *ctx, const uint8_t *p, size_t n)
{
    size_t i;
    (void)ctx;
    fputs("s ", stdout);
    for (i = 0; i < n; i++) printf("%02x", p[i]);
    putchar('\n');
    total += n;
}

static void on_pause(void *ctx, unsigned n)
{
    (void)ctx;
    printf("p %u\n", n);
    total += n;
}

int main(int argc, char **argv)
{
    static char line[70000];
    mp_voice v;
    mp_state st;
    mp_sink sink = {on_samples, on_pause, NULL};
    char err[256];

    if (argc < 2 || mp_voice_load(&v, argv[1], err, sizeof err) != 0) {
        fprintf(stderr, "replay: %s\n", argc < 2 ? "usage: replay VOICE.SMP < trace" : err);
        return 2;
    }
    mp_init(&st, &v, sink);
    while (fgets(line, sizeof line, stdin)) {
        char *p = line + 2;
        if (line[0] == 'S') {
            int16_t set[MP_NSETTINGS];
            long size = strtol(p, &p, 10);
            int i;
            for (i = 0; i < MP_NSETTINGS; i++) set[i] = (int16_t)strtol(p, &p, 10);
            for (i = 0; i < MP_NSETTINGS; i++) st.set[i] = set[i]; /* already validated by the original */
            mp_dev_start(&st, (uint16_t)size);
        } else if (line[0] == 'G') {
            st.level = (int16_t)strtol(p, &p, 10);
            st.first = (uint8_t)strtol(p, &p, 10);
            st.paren = (uint8_t)strtol(p, &p, 10);
            st.set[MP_VOLUME] = (int16_t)strtol(p, &p, 10);
        } else if (line[0] == 'F') { /* stale stack bytes the original's frame started with */
            size_t n = 0;
            uint8_t *frame = mp_frame(&st, MP_PATH_WORD);
            while (n < 0x360 && sscanf(p + 2 * n, "%2hhx", &frame[n]) == 1) n++;
        } else if (line[0] == 'W') {
            char word[1024];
            size_t n = 0, i;
            int r;
            while (n < sizeof word - 1 && sscanf(p + 2 * n, "%2hhx", (unsigned char *)&word[n]) == 1) n++;
            word[n] = 0;
            r = mp_speak_phonemes(&st, word, MP_PATH_WORD);
            printf("R %d ", r);
            for (i = 0; word[i]; i++) printf("%02x", (unsigned char)word[i]);
            printf(" %lu\n", total);
        } else if (line[0] == 'P') {
            mp_dev_pause(&st, (uint16_t)strtol(p, &p, 10));
        }
    }
    mp_voice_free(&v);
    return 0;
}
