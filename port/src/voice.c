/* Voice file loader, after 0x14813. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mikropuhe.h"

static const char MAGIC[] = "MikroPuhe - n\x84ytetiedosto\r\nTimehouse Oy 1990\r\n\x1aTIME"; /* DS:0x14e2, 51 bytes */
#define SLACK 1024 /* zeroed bytes after each block, so a short unit can be read a full period long */

static int fail(char *err, size_t errlen, const char *msg, const char *path)
{
    if (err) snprintf(err, errlen, "%s: %s", path, msg);
    return -1;
}

int mp_voice_load(mp_voice *v, const char *path, char *err, size_t errlen)
{
    unsigned char head[51], lenb, raw[46], lens[12];
    size_t n[3], i;
    uint8_t **blk[3] = {&v->blk0, &v->blk1, &v->blk2};
    FILE *f = fopen(path, "rb");

    memset(v, 0, sizeof *v);
    if (!f) return fail(err, errlen, "cannot open", path);
    if (fread(head, 1, 51, f) != 51 || memcmp(head, MAGIC, 51) != 0 || fread(&lenb, 1, 1, f) != 1)
        goto bad;
    for (i = 0; i < lenb; i++) {
        int c = fgetc(f);
        if (c == EOF) goto bad;
        if (i < 32) v->name[i] = (char)c;
    }
    if (fread(raw, 1, 46, f) != 46 || fread(lens, 1, 12, f) != 12) goto bad;
    for (i = 0; i < MP_NSETTINGS; i++)
        v->saved[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8);
    for (i = 0; i < 3; i++) {
        n[i] = lens[4 * i] | lens[4 * i + 1] << 8 | (size_t)lens[4 * i + 2] << 16 | (size_t)lens[4 * i + 3] << 24;
        if (n[i] > 0xffff) goto bad;
        n[i] += n[i] & 1; /* each block is stored padded to an even length */
        *blk[i] = calloc(1, n[i] + SLACK);
        if (!*blk[i] || fread(*blk[i], 1, n[i], f) != n[i]) goto bad;
    }
    v->len0 = n[0];
    v->len1 = n[1];
    v->len2 = n[2];
    fclose(f);
    if (v->len0 < 0x400 || v->len2 < 16) {
        mp_voice_free(v);
        return fail(err, errlen, "voice blocks too short", path);
    }
    return 0;
bad:
    fclose(f);
    mp_voice_free(v);
    return fail(err, errlen, "not a MikroPuhe voice file, or damaged", path);
}

void mp_voice_free(mp_voice *v)
{
    free(v->blk0);
    free(v->blk1);
    free(v->blk2);
    v->blk0 = v->blk1 = v->blk2 = NULL;
}
