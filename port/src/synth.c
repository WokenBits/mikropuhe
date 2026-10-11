/* Phoneme string -> samples: the device boundary, the assembly sample generator and speak_phonemes. */
#include <string.h>

#include "mikropuhe.h"

#define S(i) (st->set[i])

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t blk0w(const mp_state *st, unsigned off) { return (int16_t)rd16(st->v->blk0 + off); }

/* ---- device boundary ------------------------------------------------------------------- */

static void apply_volume(const mp_state *st, uint8_t *p, uint16_t n);

/* 0x1548a: a speak call starts with an empty ring buffer */
void mp_dev_start(mp_state *st, uint16_t ring_size)
{
    st->ring_size = ring_size;
    st->ring_wr = 0;
}

/* 0x1589a: samples into the ring buffer; with_volume applies the volume to the copy; 0 bytes become 1 (0x15882) */
static void dev_write(mp_state *st, const uint8_t *p, uint16_t n, int with_volume)
{
    uint8_t out[0x400];
    while (n) {
        uint16_t k = n > sizeof out ? (uint16_t)sizeof out : n, i;
        memcpy(out, p, k);
        if (with_volume) apply_volume(st, out, k);
        for (i = 0; i < k; i++)
            if (out[i] == 0) out[i] = 1;
        st->sink.samples(st->sink.ctx, out, k);
        st->ring_wr = (uint16_t)(((uint32_t)st->ring_wr + k) % st->ring_size);
        p += k;
        n -= k;
    }
}

/* 0x1551d: a pause. Short ones are written as silence samples. A long one is a 3-byte record in the
 * ring buffer; when fewer than 3 bytes remain before the buffer end the original first fills them
 * with silence, which is why the write position is tracked here at all. */
void mp_dev_pause(mp_state *st, uint16_t n)
{
    if (n < 0x6e) {
        if (n) {
            memset(st->buf, 0x80, n);
            dev_write(st, st->buf, n, 0);
        }
        return;
    }
    if ((uint16_t)(st->ring_size - st->ring_wr) < 3) {
        uint8_t fill[2] = {0x80, 0x80};
        st->sink.samples(st->sink.ctx, fill, (size_t)(st->ring_size - st->ring_wr));
        st->ring_wr = 0;
    }
    st->sink.pause(st->sink.ctx, n);
    st->ring_wr += 3;
    if (st->ring_wr == st->ring_size) st->ring_wr = 0;
}

/* ---- assembly module 0x14d50 - 0x15167 -------------------------------------------------- */

/* 0x14d5c, 0x14d91: 1-based position in the list, 0 if absent. 'y' is in both lists. */
static int16_t is_vowel(uint8_t c)
{
    static const uint8_t list[8] = {'a', 'e', 'i', 'o', 'u', 'y', 0x84, 0x94};
    const uint8_t *p = memchr(list, c, sizeof list);
    return p ? (int16_t)(p - list + 1) : 0;
}

static int16_t is_consonant(uint8_t c)
{
    static const uint8_t list[22] = "bcdfghjklmnpqrstvwxyz";
    const uint8_t *p = memchr(list, c, 21);
    return p ? (int16_t)(p - list + 1) : 0;
}

/* 0x14dc9: how many samples to cut from (positive) or add to (negative) one pitch period */
static int16_t pitch_offset(const mp_state *st, uint8_t c)
{
    int16_t dx = (int8_t)c, ax, lim;
    dx = (int16_t)(dx + blk0w(st, 0x2e + (uint16_t)(st->level * 2)));
    dx = (int16_t)(dx + (int8_t)(uint8_t)(S(MP_PITCH) - 4) * (int8_t)(uint8_t)(st->period >> 4));
    ax = (int16_t)((S(MP_PITCH_VAR) & 0xff00) | (uint8_t)(S(MP_PITCH_VAR) + 4));
    ax = (int16_t)((int32_t)ax * dx);
    ax = (int16_t)(ax >> 3);
    lim = (int16_t)(st->period >> 1);
    if (ax > lim) return lim;
    if (ax < -lim) return (int16_t)-lim;
    return ax;
}

/* 0x14e1a (0x14e5e returns the pointer, 0x14e70 the length): variant `variant` of unit `unit` in
 * table `table` of block 2. A table is a run of units, each: count, then count times (length, data
 * padded to even). A variant beyond the count gives the unit's first variant. */
static const uint8_t *find_unit(const mp_state *st, uint16_t table, uint16_t unit, uint16_t variant, uint16_t *len)
{
    const uint8_t *b = st->v->blk2;
    uint16_t bx = (uint16_t)((table - 1) * 4), si = rd16(b + bx), dx = unit, guard = 0;

    for (;;) {
        uint16_t count, cx = 0;
        if (dx == 0 || (size_t)si + 4 > st->v->len2 || ++guard > 4096) {
            si = (uint16_t)(bx + 2);
            break;
        }
        count = rd16(b + si);
        si += 2;
        bx = si;
        dx--;
        for (;;) {
            uint16_t l;
            cx++;
            if (count < cx || (size_t)si + 2 > st->v->len2) break;
            l = rd16(b + si);
            si += 2;
            if (dx == 0 && cx == variant) goto found;
            si = (uint16_t)(si + l + (l & 1));
        }
    }
found:
    if (len) *len = rd16(b + (uint16_t)(si - 2));
    return b + si;
}

static uint16_t unit_len(const mp_state *st, uint16_t table, uint16_t unit, uint16_t variant)
{
    uint16_t len;
    find_unit(st, table, unit, variant, &len);
    return len;
}

/* 0x14e81: times 1.25, clipped */
static void emphasise(uint8_t *p, uint16_t n)
{
    for (; n; n--, p++) {
        int16_t a = (int8_t)(uint8_t)(*p - 0x80);
        a = (int16_t)(a + (a >> 2));
        if (a > 0x7f) a = 0x7f;
        else if (a < -0x80) a = -0x80;
        *p = (uint8_t)(a + 0x80);
    }
}

/* 0x15114: volume as a shift by (volume - 4), clipped when shifting up */
static void apply_volume(const mp_state *st, uint8_t *p, uint16_t n)
{
    int16_t sh = (int16_t)(S(MP_VOLUME) - 4);
    if (n == 0 || sh == 0) return;
    if (sh > 0) {
        for (; n; n--, p++) {
            int16_t a = (int8_t)(uint8_t)(*p - 0x80);
            a = (int16_t)((uint16_t)a << (sh & 15));
            if (a > 0x7f) a = 0x7f;
            else if (a < -0x80) a = -0x80;
            *p = (uint8_t)(a + 0x80);
        }
    } else {
        int cl = -sh > 7 ? 7 : -sh;
        for (; n; n--, p++) {
            int8_t a = (int8_t)(uint8_t)(*p - 0x80);
            *p = (uint8_t)((a >> cl) + 0x80);
        }
    }
}

/* 0x14f46: one stretch of samples copied and made one volume step quieter */
static void copy_quieter(mp_state *st, const uint8_t *src, uint16_t n, uint8_t *dst)
{
    memmove(dst, src, n);
    S(MP_VOLUME)--;
    apply_volume(st, dst, n);
    S(MP_VOLUME)++;
}

/* 0x14f82: lens[0] periods that cross-fade from waveform a to waveform b. lens[1..] are pitch bytes,
 * one per period. mode: 0 volume and emphasis, 1 volume, 2 one step quieter. */
static void synth_voiced(mp_state *st, const uint8_t *lens, const uint8_t *a, const uint8_t *b, int16_t mode)
{
    uint8_t n = lens[0];
    uint16_t sum = 0, total, err, i;
    int8_t wa = 0x1f, wb = 1;

    if (n == 0) return; /* the original would loop 65,536 times here; it never passes 0 */
    for (i = 1; i <= n; i++)
        sum = (uint16_t)(sum - (uint16_t)pitch_offset(st, lens[i]));
    total = (uint16_t)((uint16_t)(n * (uint8_t)st->period) + sum);
    err = (uint16_t)((total >> 1) + (total & 1));
    for (i = 1; i <= n; i++) {
        uint8_t off = (uint8_t)pitch_offset(st, lens[i]);
        uint16_t cnt = st->period, k;
        uint8_t *o = st->buf;

        if (!(off & 0x80)) cnt = (uint16_t)(cnt - off);
        if (cnt > sizeof st->buf - 0x80) cnt = sizeof st->buf - 0x80;
        for (k = 0; k < cnt; k++) {
            int16_t x = (int16_t)((int8_t)(uint8_t)(a[k] - 0x80) * wa);
            int16_t y = (int16_t)((int8_t)(uint8_t)(b[k] - 0x80) * wb);
            int16_t ax = (int16_t)(x + y);
            *o++ = (uint8_t)((ax >> 5) + ((ax >> 4) & 1) + 0x80);
            err = (uint16_t)(err + 0x20);
            if (err >= total) {
                err = (uint16_t)(err - total);
                wa--;
                wb++;
            }
        }
        if (mode == 0) {
            apply_volume(st, st->buf, cnt);
            emphasise(st->buf, cnt);
        } else if (mode == 1) {
            apply_volume(st, st->buf, cnt);
        } else if (mode == 2) {
            S(MP_VOLUME)--;
            apply_volume(st, st->buf, cnt);
            S(MP_VOLUME)++;
        }
        if (off & 0x80) { /* a longer period: silence is appended, the cross-fade keeps advancing */
            uint16_t pad = (uint16_t)(-(int16_t)(int8_t)off);
            for (k = 0; k < pad; k++) {
                err = (uint16_t)(err + 0x20);
                if (err >= total) {
                    err = (uint16_t)(err - total);
                    wa--;
                    wb++;
                }
            }
            memset(o, 0x80, pad);
            o += pad;
        }
        dev_write(st, st->buf, (uint16_t)(o - st->buf), 0);
    }
}

/* ---- C module, helpers of speak_phonemes ------------------------------------------------- */

/* 0x1451e and 0x14560: a duration in periods from a voice parameter, speed, pitch and pitch variation */
static int16_t dur_common(const mp_state *st, int16_t x)
{
    uint16_t k = (uint16_t)((uint16_t)(S(MP_PITCH) - 4) * (uint16_t)(S(MP_PITCH_VAR) + 4));
    k = (uint16_t)(k * (uint16_t)x);
    return (int16_t)((int16_t)k >> 7);
}

static int16_t dur8(const mp_state *st, int16_t x)
{
    int16_t si = dur_common(st, x);
    int16_t ax = (int16_t)(uint16_t)((uint16_t)(8 - S(MP_SPEED) + 4) * (uint16_t)x);
    si = (int16_t)(si + (ax >> 3));
    return si < 1 ? 1 : si;
}

static int16_t dur6(const mp_state *st, int16_t x)
{
    int16_t si = dur_common(st, x);
    int16_t ax = (int16_t)(uint16_t)((uint16_t)(8 - S(MP_SPEED) + 2) * (uint16_t)x);
    si = (int16_t)(si + ax / 6);
    return si < 1 ? 1 : si;
}

/* 0x14503: closure silence before a stop, scaled by speed */
static uint16_t pause_scale(const mp_state *st, uint16_t x)
{
    return (uint16_t)((x / 7) * (uint16_t)(8 - S(MP_SPEED) + 3));
}

/* 0x145a1: repeat count for a noise unit: x * (13 - speed) / 9 rounded, at least min */
static int16_t scale(const mp_state *st, int16_t x, int16_t min)
{
    int16_t si = (int16_t)(uint16_t)((uint16_t)(8 - S(MP_SPEED) + 5) * (uint16_t)x);
    int16_t q = (int16_t)(si / 9), r = (int16_t)(si % 9);
    si = (int16_t)(q + (r > 4 ? 1 : 0));
    return si < min ? min : si;
}

/* 0x142de: unit number in table 1 for the stops and f; 0x1433a: unit in table 3 for j l m n v */
static int16_t cons_unit(int16_t c)
{
    static const int8_t map[16] = {1, 0, 2, 3, 4, 0, 0, 5, 0, 0, 0, 6, 0, 0, 0, 7}; /* b d f g k p t */
    return (c >= 1 && c <= 16) ? map[c - 1] : 0;
}

static int16_t voiced_unit(int16_t c)
{
    static const int8_t map[11] = {9, 0, 10, 11, 12, 0, 0, 0, 0, 0, 13}; /* j l m n v */
    return (c >= 7 && c <= 17) ? map[c - 7] : 0;
}

/* 0x14384: optional closure pause, then a stored unit written `repeat` times with volume */
static void plain_unit(mp_state *st, uint16_t pre, const uint8_t *unit, uint16_t len, int16_t repeat)
{
    uint16_t n = pause_scale(st, pre);
    int16_t i;
    if (n) mp_dev_pause(st, n);
    if (S(MP_CONSONANTS)) S(MP_VOLUME) += 2;
    for (i = 0; i < repeat; i++)
        dev_write(st, unit, len, 1);
    if (S(MP_CONSONANTS)) S(MP_VOLUME) -= 2;
}

/* 0x143f2: `count` copies of one stored period, each shortened or lengthened by its pitch byte.
 * mode as in synth_voiced, but forced to 1 when first-syllable stress is off. Returns the new
 * position in the contour. */
static uint16_t synth_plain(mp_state *st, const uint8_t *contour, uint16_t pos, const uint8_t *unit,
                            int16_t count, int16_t mode)
{
    int16_t i;
    int emph = 0, restore = 0;

    if (S(MP_STRESS) == 0) mode = 1;
    if (mode == 0) emph = 1;
    else if (mode == 2) {
        S(MP_VOLUME)--;
        restore = 1;
    }
    for (i = 1; i <= count; i++) {
        int16_t off = pitch_offset(st, contour[pos]);
        pos++;
        memmove(st->buf, unit, st->period);
        apply_volume(st, st->buf, st->period);
        if (emph) emphasise(st->buf, st->period);
        if (off < 1) {
            dev_write(st, st->buf, st->period, 0);
            if (off != 0) mp_dev_pause(st, (uint16_t)-off);
        } else {
            dev_write(st, st->buf, (uint16_t)(st->period - off), 0);
        }
    }
    if (restore) S(MP_VOLUME)++;
    return pos;
}

/* ---- 0x13543 ------------------------------------------------------------------------------ */

/*
 * One word, written as its sounds (Finnish spelling is close to phonemic) with any punctuation
 * still attached, becomes samples. Vowel runs are built from stored pitch periods with a pitch
 * contour laid over them; j l m n v likewise; stops, f, h, r, s are stored noise units.
 *
 * The control flow and the stack layout follow the original: the work arrays sit in one block at
 * the original frame offsets, because the code indexes past their ends in places (noted "sic")
 * and the neighbours then matter.
 *
 * The block lives in an image of the stack that mp_state keeps between calls. The original has a
 * bug at a word-initial j l m n v: the pitch bytes for the consonant are stored at the wrong index
 * (see "sic" in case 7.. below) and the ones actually used are whatever the stack held: usually the
 * pitch contour of the previous word's last vowel run, or, when the previous call came through a
 * different caller and so sat at a different stack depth, some other array of that frame. `path`
 * places the frame where the original's was for that caller, so the same stale bytes are found.
 */
uint8_t *mp_frame(mp_state *st, int path) { return st->stack + 0x1a0 + path; }

int mp_speak_phonemes(mp_state *st, char *str, int path)
{
    uint8_t *const frame = mp_frame(st, path);
    uint8_t *const tail = frame + (0x360 - 0x32a);    /* trailing punctuation, up to 32 */
    uint8_t *const seg = frame + (0x360 - 0x308);     /* seg[0] = end index, then (unit, glide periods, steady periods)... */
    uint8_t *const contour = frame + (0x360 - 0x204); /* contour[0] = count, then one pitch byte per period */
    uint8_t *const tmp = frame + (0x360 - 0x104);     /* pitch bytes for one synth_voiced call */
    uint8_t *const orig = (uint8_t *)str;
    uint8_t *s = orig;
    const uint8_t *tbl;
    uint16_t len, di, si;
    st->path = path;
    if (st->trace) st->trace(st, "W", str);
    int16_t emph, punct, acc;
    int16_t v344, v33c, v34a, v334, v354, v33e, v358, v35c, v35e, v350, v348 = 0, v356, v342 = 0;
    int16_t v35a, v352, v33a, v330, v338, v34e, v340, v346, v34c;

    for (di = 0; s[di] != 0; di++)
        if (is_vowel(s[di]) + is_consonant(s[di]) != 0) break;
    s += di;
    len = (uint16_t)strlen((char *)s);
    emph = 0;
    if (di != 0 && memchr(orig, '_', di) != NULL) emph = 1;
    else if (len < 5 && S(MP_STRESS) == 0) emph = 1;
    if (di != 0 && st->paren == 1 && memchr(orig, '(', di) != NULL) {
        if (!st->first) mp_dev_pause(st, 7000);
        S(MP_VOLUME)--;
        st->paren = 2;
    }
    if (is_vowel(s[0]) && !st->first) mp_dev_pause(st, 1500);
    for (di = (uint16_t)(len - 1); di != 0xffff; di--)
        if (is_vowel(s[di]) + is_consonant(s[di]) != 0) break;
    si = (uint16_t)(len - 1 - di);
    if (si != 0) {
        if (si > 0x20) si = 0x20;
        memmove(tail, s + di + 1, si);
        len = (uint16_t)(di + 1);
        s[len] = 0;
    }
    tail[si] = 0;
    if (strchr((char *)tail, '.') != NULL && st->level < 3) st->level++;
    if (strchr((char *)tail, '!') != NULL) punct = 1;
    else if (strchr((char *)tail, '?') != NULL) punct = 2;
    else punct = 0;
    if (punct == 2) emph = 1;
    st->first = 0;
    v344 = 1000; /* 1000 = "the previous sound was not a vowel" */
    di = 0;
    v33c = 1;
    v34a = 0;
    v334 = 0;
    goto L141f0;

L1373f:
    if (is_vowel(s[di])) {
        for (si = (uint16_t)(di + 1); si != len; si++)
            if (!is_vowel(s[si])) break;
        if (si == len && emph < 2) { /* the word ends in this vowel run */
            if (punct != 0) emph = 1;
            else emph++;
        }
        si = 0;
        v354 = 0;
        v33e = 0;
        for (;;) {
            v358 = is_vowel(s[di]);
            if (!(v358 != 0 && v33e < 200 && si < 200 && di < len)) break;
            seg[si + 1] = (uint8_t)v358;
            if (v354 == 0 || v354 == v358) {
                seg[si + 2] = 0;
                seg[si + 3] = (uint8_t)dur6(st, blk0w(st, 0x10 + (uint16_t)(emph * 2)));
                if (v354 == v358) /* a long vowel */
                    seg[si + 3] = (uint8_t)(seg[si + 3] + dur6(st, blk0w(st, 0x3a + (uint16_t)(emph * 2))));
            } else { /* a diphthong: glide, then steady part */
                seg[si + 2] = (uint8_t)dur8(st, blk0w(st, 0x16 + (uint16_t)(emph * 2)));
                seg[si + 3] = (uint8_t)dur6(st, blk0w(st, 0x1c + (uint16_t)(emph * 2)));
                v33e = (int16_t)(v33e + seg[si + 2]);
            }
            v33e = (int16_t)(v33e + seg[si + 3]);
            si += 3;
            v354 = v358;
            di++;
        }
        seg[si + 1] = 0;
        seg[0] = (uint8_t)(si + 1);
        /* resample the 256-step contour template for this stress level to the run's length */
        tbl = st->v->blk0 + (uint16_t)((emph + 1) << 8);
        acc = 0;
        v35c = 0;
        v358 = 0;
        contour[0] = 0;
        for (si = 0; si < 0x100; si++) {
            acc = (int16_t)(acc + (int8_t)tbl[si]);
            v358 = (int16_t)(v358 + v33e);
            v35c++;
            if (v358 > 0xff) {
                v35c = (int16_t)(acc / v35c);
                contour[0]++;
                contour[contour[0]] = (uint8_t)v35c;
                v35c = 0;
                acc = 0;
                v358 = (int16_t)(v358 - 0x100);
            }
        }
        v35e = 0;
        if (v34a != 0) { /* glide in from the voiced consonant before */
            v350 = dur8(st, blk0w(st, 0x22));
            v358 = (int16_t)(contour[1] - v348);
            tmp[0] = (uint8_t)v350;
            for (si = 1; si <= (uint16_t)v350; si++)
                tmp[si] = (uint8_t)((int16_t)(uint16_t)(si * (uint16_t)v358) / v350 + (uint8_t)v348);
            synth_voiced(st, tmp, find_unit(st, 3, (uint16_t)v34a, 1, NULL),
                         find_unit(st, 3, seg[1], (uint16_t)v33c, NULL), emph);
            si = tmp[0];
            if (contour[0] != 0) memmove(&contour[si + 1], &contour[1], contour[0]);
            if (si != 0) memmove(&contour[1], &tmp[1], si);
            contour[0] = (uint8_t)(contour[0] + si);
            v35e = (int16_t)(v35e + v350);
            v34a = 0;
        }
        for (v358 = 1; (v356 = seg[v358]) != 0; v358 = (int16_t)(v358 + 3), v354 = v356) {
            if (seg[v358 + 1] == 0) {
                v35e = (int16_t)synth_plain(st, contour, (uint16_t)v35e,
                                            find_unit(st, 3, (uint16_t)v356, (uint16_t)v33c, NULL), seg[v358 + 2], emph);
            } else {
                v350 = seg[v358 + 1];
                for (si = 1; si <= (uint16_t)v350; si++)
                    tmp[si] = contour[(uint16_t)(v35e + si)];
                tmp[0] = (uint8_t)si; /* sic: one more than the number copied */
                synth_voiced(st, tmp, find_unit(st, 3, (uint16_t)v354, (uint16_t)v33c, NULL),
                             find_unit(st, 3, (uint16_t)v356, 1, NULL), emph);
                v35e = (int16_t)(v35e + v350);
                v35e = (int16_t)synth_plain(st, contour, (uint16_t)v35e, find_unit(st, 3, (uint16_t)v356, 1, NULL),
                                            seg[v358 + 2], emph);
                v33c = 1;
            }
        }
        v33c = 1;
        v344 = (int8_t)contour[(uint16_t)v35e];
        v342 = emph;
        if (emph == 0) emph = 1;
        if (punct == 1) emph = 0;
        goto L141f0;
    }
    if (!is_consonant(s[di])) {
        if (s[di] == '#') emph = 0;
        else if (s[di] == '&') mp_dev_pause(st, 2000);
        else if (s[di] == '-') emph = 1;
        goto L141e9;
    }

    v35c = is_consonant(s[di]);
    v35a = (di <= len) ? is_consonant(s[di + 1]) : 0;
    v352 = (int16_t)((v35c == v35a) ? di + 2 : di + 1); /* index after the (possibly doubled) consonant */
    if ((uint16_t)v352 >= len) {
        v352 = (int16_t)(len - 1);
        v334 = 1;
    }
    switch (v35c) { /* jump table at 0x13ce9 */
    case 1: case 4: case 5: case 8: case 12: case 16: /* b f g k p t */
        if (v35c == v35a && v35c == 4 && st->has_tbl4) { /* ff */
            uint16_t l = unit_len(st, 4, 1, 1);
            plain_unit(st, 0, find_unit(st, 4, 1, 1, NULL), l, 1);
            break;
        }
        acc = (v35c == v35a) ? blk0w(st, 2) : 0; /* longer closure for a doubled stop */
        v33a = is_vowel(s[(uint16_t)v352]);
        v358 = cons_unit(v35c);
        v330 = (int16_t)unit_len(st, 1, (uint16_t)v358, (uint16_t)(v33a + 1));
        si = (uint16_t)blk0w(st, 0);
        if (si > (uint16_t)v330) acc = (int16_t)(acc + si - (uint16_t)v330);
        plain_unit(st, (uint16_t)acc, find_unit(st, 1, (uint16_t)v358, (uint16_t)(v33a + 1), NULL), (uint16_t)v330, 1);
        break;
    case 3: /* d */
        v33a = is_vowel(s[(uint16_t)v352]);
        v358 = cons_unit(3);
        if (v33a != 0) {
            uint16_t l = unit_len(st, 1, (uint16_t)v358, (uint16_t)(v33a + 1));
            plain_unit(st, 0, find_unit(st, 1, (uint16_t)v358, (uint16_t)(v33a + 1), NULL), l, 1);
            break;
        }
        v330 = (int16_t)unit_len(st, 1, (uint16_t)v358, 1);
        acc = (v35c == v35a) ? blk0w(st, 2) : 0;
        acc = (int16_t)(acc + blk0w(st, 0) - v330);
        plain_unit(st, (uint16_t)acc, find_unit(st, 1, (uint16_t)v358, 1, NULL), (uint16_t)v330, 1);
        break;
    case 6: case 14: case 15: /* h r s */
        v358 = (v35c == 0xe) ? 1 : (v35c == 0xf) ? 2 : 0;
        if (v35c == v35a) v338 = scale(st, blk0w(st, 0xa + (uint16_t)(v358 * 2)), 2);
        else v338 = scale(st, blk0w(st, 4 + (uint16_t)(v358 * 2)), 1);
        {
            uint16_t l = unit_len(st, 2, (uint16_t)(v358 + 1), 1);
            plain_unit(st, 0, find_unit(st, 2, (uint16_t)(v358 + 1), 1, NULL), l, v338);
        }
        break;
    case 7: case 9: case 10: case 11: case 17: /* j l m n v */
        v350 = dur8(st, blk0w(st, 0x22));
        v34e = blk0w(st, 0x2e);
        contour[0] = 0;
        v35e = 0;
        if (v35c == 0xb && v35a == 5) { /* ng */
            v35a = v35c;
            v358 = 0xe;
            v352++;
            if ((uint16_t)v352 >= len) {
                v352 = (int16_t)(len - 1);
                v334 = 1;
            }
        } else {
            v358 = voiced_unit(v35c);
        }
        if (v344 != 1000) { /* glide out of the vowel before */
            v340 = is_vowel(s[(uint16_t)(di - 1)]);
            contour[0] = (uint8_t)v350;
            for (si = 1; si <= (uint16_t)v350; si++) {
                contour[si] = (uint8_t)v344;
                v344 = (int16_t)(v344 + v34e);
            }
            v35e = v350;
            copy_quieter(st, find_unit(st, 3, (uint16_t)v340, 1, NULL), st->period, seg);
            synth_voiced(st, contour, seg, find_unit(st, 3, (uint16_t)v358, 1, NULL), emph);
            v346 = 1;
        } else {
            v346 = 3;
            v344 = blk0w(st, 0x2c);
        }
        if (!is_vowel(s[(uint16_t)v352])) v346++;
        if (v346 == 1) v34c = blk0w(st, 0x24);
        else if (v334 == 1) v34c = blk0w(st, 0x28);
        else if (cons_unit(is_consonant(s[(uint16_t)v352])) != 0) v34c = blk0w(st, 0x26);
        else v34c = blk0w(st, 0x38);
        if (v35c == v35a) v34c = (int16_t)(v34c + blk0w(st, 0x2a));
        v34c = dur6(st, v34c);
        for (si = 1; si <= (uint16_t)v34c; si++) {
            contour[contour[0]] = (uint8_t)v344; /* sic: stores at index contour[0], then increments it */
            contour[0]++;
            v344 = (int16_t)(v344 + v34e);
        }
        synth_plain(st, contour, (uint16_t)v35e, find_unit(st, 3, (uint16_t)v358, 1, NULL), v34c, emph);
        if (v346 == 1 || v346 == 3) { /* a vowel follows: it glides in from this unit */
            v34a = v358;
            v348 = v344;
            v342 = emph;
            if (v35c == 0xa || v35c == 0xb) v33c = 2;
        }
        break;
    default: /* c q, and w x y z, are not sounds at this stage */
        break;
    }
    if (v35c == v35a) {
        di += 2;
        goto L141ea;
    }
L141e9:
    di++;
L141ea:
    v344 = 1000;
L141f0:
    if (di < len) goto L1373f;
    (void)v342;

    len = 0; /* from here: number of tail characters handed back to the caller */
    if (st->level == 1) st->level = 2;
    for (di = 0; tail[di] != 0; di++) {
        si = 0;
        switch (tail[di]) { /* table at 0x1422d */
        case '!': si = 13000; break;
        case ',': si = 8000; break;
        case '.': si = 16000; st->level = 1; break;
        case ':': si = 12000; break;
        case ';': si = 10000; break;
        case '?': si = 14000; break;
        case ')':
            if (st->paren == 2) {
                S(MP_VOLUME)++;
                st->paren = 1;
                break;
            }
            /* fall through */
        default:
            orig[len++] = tail[di];
            break;
        }
        if (si != 0 && (S(MP_SENTENCE_PAUSE) != 0 || strchr(".:?!", tail[di]) == NULL))
            mp_dev_pause(st, si);
    }
    orig[len] = 0;
    return (int)len;
}

/* ---- state ---------------------------------------------------------------------------------- */

/* 0x145e8 */
static int16_t clamp_setting(int16_t x) { return (x < 0 || x > 8) ? 4 : x; }

/* 0x14605 */
void mp_set(mp_state *st, const int16_t set[MP_NSETTINGS])
{
    int i;
    for (i = 0; i < 4; i++) st->set[i] = clamp_setting(set[i]);
    for (; i < MP_NSETTINGS; i++) st->set[i] = set[i] != 0;
    st->c_char = st->set[MP_C_AS_K] ? 'k' : 's';
}

void mp_init(mp_state *st, const mp_voice *v, mp_sink sink)
{
    memset(st, 0, sizeof *st);
    st->v = v;
    st->sink = sink;
    st->period = rd16(v->blk0 + 0x36);
    st->has_tbl4 = (rd16(v->blk2 + 0xc) | rd16(v->blk2 + 0xe)) != 0;
    st->level = 1;
    st->first = 1;
    st->ring_size = MP_RING_SIZE;
    mp_set(st, v->saved);
}
