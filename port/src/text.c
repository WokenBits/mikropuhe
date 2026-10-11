/* Text -> words: character classes, spelling exceptions, numbers. After segment 1262 of PUHU.EXE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mikropuhe.h"
#include "tables.h"

#define S(i) (st->set[i])
#define RUN_MAX 100 /* a run of one character class is cut after this many characters (0x12867) */

typedef struct {
    char *out; /* DS:0x1b4c */
    uint16_t len; /* DS:0x1d7c */
} numbuf;

static void text_pause(mp_state *st, uint16_t n)
{
    if (st->trace) {
        char num[8];
        snprintf(num, sizeof num, "%u", n);
        st->trace(st, "P", num);
    }
    mp_dev_pause(st, n);
}

/* 0x14db1 */
static int16_t digit_value(uint8_t c) { return (c >= 0x30 && c < 0x3a) ? (int16_t)(c - 0x2f) : 0; }

/* 0x1262e */
static int is_letter(uint8_t c) { return memchr(ALPHABET, c, 0x20) != NULL; }

static int is_vowel_char(uint8_t c)
{
    static const uint8_t list[8] = {'a', 'e', 'i', 'o', 'u', 'y', 0x84, 0x94}; /* 0x14d54 */
    return memchr(list, c, sizeof list) != NULL;
}

/* 0x12651: lower-cases *pc and returns 1 letter, 2 digit, 3 special, 4 anything else */
static int char_class(uint8_t *pc)
{
    uint8_t c = *pc;
    int cls;

    if (c >= 0x41 && c <= 0x5a) {
        c = (uint8_t)(c + 0x20);
    } else {
        const uint8_t *p = memchr(UPPER_EXTRA, c, 4);
        if (p) c = LOWER_EXTRA[p - UPPER_EXTRA];
    }
    if (is_letter(c)) cls = 1;
    else if (c >= '0' && c <= '9') cls = 2; /* the original tests its ctype table at DS:0x167d */
    else cls = (c != 0 && memchr(CLASS3, c, sizeof CLASS3 - 1)) ? 3 : 4;
    *pc = c;
    return cls;
}

/* 0x129ee: the character at p is replaced by a string */
static void replace_char(char *p, const char *str)
{
    size_t n = strlen(p), m = strlen(str);
    if (n != 0 && m != 1) memmove(p + m, p + 1, n);
    memmove(p, str, m);
}

/* 0x12f79: voice block 1 is a list of (key, replacement) byte strings, each with a length byte,
 * after a 4-byte count. On a hit the string is rewritten in place. A final '.' on the string is
 * tolerated and kept. */
static int lookup_exception(mp_state *st, char *s)
{
    const uint8_t *b = st->v->blk1;
    uint16_t count, di = 4, len, klen = 0, i;
    int dot = 0;

    if (s[0] == 0) return 0;
    if (s[0] == '_' && s[1] == 0 && S(MP_UNDERSCORE) == 0) return 0;
    count = (uint16_t)(b[0] | b[1] << 8);
    len = (uint16_t)strlen(s);
    if (len > 1 && s[len - 1] == '.') {
        dot = 1;
        len--;
    }
    for (i = 0; i < count; i++) {
        if ((size_t)di + 2 > st->v->len1) return 0;
        klen = b[di];
        if ((klen == len || (dot && b[di + klen] == '.' && klen + 1 == len)) &&
            memcmp(b + di + 1, s, len) == 0) {
            if (klen != len) dot = 0;
            break;
        }
        di = (uint16_t)(di + klen + 1);
        di = (uint16_t)(di + b[di] + 1);
    }
    if (i >= count) return 0;
    di = (uint16_t)(di + klen + 1);
    len = b[di];
    memmove(s, b + di + 1, len);
    if (dot) s[len++] = '.';
    s[len] = 0;
    return 1;
}

/* ---- numbers ------------------------------------------------------------------------------- */

/* 0x130c5 */
static void num_append(numbuf *nb, const char *s)
{
    for (; nb->len < 0xff; nb->len++) {
        char c = *s++;
        nb->out[nb->len] = c;
        if (c == 0) break;
    }
    nb->out[nb->len] = 0;
}

/* 0x130f9: entry `idx` (1-based) of inflected form `form` (1..7) of the number word list */
static void num_word(numbuf *nb, int16_t form, int16_t idx)
{
    uint16_t target = (uint16_t)(NUM_FORM_BASE[(form - 1) % 7] + idx), i;
    const char *p = NUM_WORDS;

    for (i = 1; i != target; i++) {
        p = strchr(p, ',');
        if (!p || target == 0) return; /* past the list: the original would read on */
        p++;
    }
    for (; *p && *p != ',' && nb->len < 0xff; p++) nb->out[nb->len++] = *p;
    nb->out[nb->len] = 0;
}

/* 0x132fe: a number of two or more digits, three at a time from the left */
static void num_groups(numbuf *nb, const char *num, int16_t form)
{
    uint16_t n = (uint16_t)strlen(num);

    while (n != 0) {
        char part[20];
        uint16_t g = 0, k, v;
        int kind, teen;
        int16_t base;

        while (g < 4 && n <= NUM_GROUP[g]) g++;
        if (g < 4) {
            for (k = 0; k < (uint16_t)(n - NUM_GROUP[g]) && k < sizeof part - 1; k++) part[k] = num[k];
            part[k] = 0;
        } else {
            snprintf(part, sizeof part, "%s", num);
            k = (uint16_t)strlen(part);
        }
        num += k;
        n = (uint16_t)(n - k);
        v = (uint16_t)atoi(part);
        if (v == 0) {
            kind = 0;
        } else if (v == 1) {
            kind = 1; /* the group word alone: "tuhat", not "yksituhat" */
        } else {
            kind = 2;
            base = (form == 1) ? 0xc : 6;
            if (v > 99) {
                uint16_t h = v / 100;
                v = (uint16_t)(v - h * 100);
                if (h > 1) num_word(nb, form, (int16_t)(base + h + 1));
                if (form == 1) num_word(nb, 1, h < 2 ? 9 : 10); /* sata, sataa */
                else num_word(nb, form, 5);
            }
            teen = 0;
            if (v > 9) {
                uint16_t t = v / 10;
                v = (uint16_t)(v - t * 10);
                if (t < 2) {
                    if (v == 0) {
                        if (form == 1) num_word(nb, 1, 0xb); /* kymmenen */
                        else num_word(nb, form, 6);
                    } else {
                        teen = 1;
                    }
                } else {
                    num_word(nb, form, (int16_t)(base + t + 1));
                    num_word(nb, form, base); /* kymmentä */
                }
            }
            if (v != 0) {
                if (teen || *num != 0) num_word(nb, form, (int16_t)(base + v + 1));
                else num_word(nb, form, (int16_t)(base + v + 0xb));
            }
            if (teen) num_append(nb, STR_TOISTA);
        }
        if (g < 4 && kind != 0) {
            if (form == 1) num_word(nb, 1, (int16_t)(kind == 1 ? (g + 1) * 2 - 1 : (g + 1) * 2));
            else num_word(nb, form, (int16_t)(g + 1));
        }
    }
}

/* 0x13175: digit string (with an optional decimal part) -> words in `out`. form 1 is the basic
 * cardinal; 2..7 are the inflected lists chosen by speak_number from the surrounding characters. */
static void number_to_words(mp_state *st, char *num, char *out, int16_t form)
{
    numbuf nb = {out, 0};
    int16_t base = (form == 1) ? 0xc : 6;
    char *frac = NULL, *p;
    int colon = 0;

    out[0] = 0;
    if (num[0] == ':') {
        frac = num + 1;
        num[0] = 0;
        colon = 1;
    } else if ((p = strchr(num, st->decimal)) != NULL) {
        frac = p + 1;
        *p = 0;
    }
    if (num[0] != 0) {
        if (strlen(num) < 0x10) {
            while (*num == '0') num++;
            if (*num == 0) {
                num_word(&nb, form, (int16_t)(base + 0xb));
            } else if (num[1] == 0) {
                if (*num > 0x2f && *num < 0x3a) num_word(&nb, form, (int16_t)(*num + base - 0x25));
            } else {
                num_groups(&nb, num, form);
            }
        } else {
            num_word(&nb, 1, (int16_t)(form + 0xbc)); /* sixteen digits or more: "enemmän kuin paljon" */
        }
    }
    if (frac) {
        if (!colon) num_append(&nb, nb.len == 0 ? STR_NOLLA_PILKKU : STR_PILKKU);
        for (; *frac; frac++)
            if (*frac >= '0' && *frac <= '9') num_word(&nb, form, (int16_t)((uint8_t)*frac + base - 0x2f));
    }
}

/* 0x12c74: a run that began with a digit */
static void speak_number(mp_state *st, char *str)
{
    char b[0x208];
    uint16_t n, si, di = 0xffff;

    if ((str[0] == '0' && digit_value((uint8_t)str[1]) != 0) || st->digits) {
        /* a leading zero, or after the word "puhelin": digit by digit */
        for (si = 0; str[si] != 0; si++) {
            if (digit_value((uint8_t)str[si]) != 0) {
                b[0] = str[si];
                b[1] = 0;
                number_to_words(st, b, st->gbuf, 1);
                mp_speak_phonemes(st, st->gbuf, MP_PATH_NUMBER);
            } else {
                text_pause(st, 4000);
            }
        }
        return;
    }
    n = (uint16_t)strlen(str);
    for (si = 0; si <= n; si++) {
        uint8_t c = (uint8_t)str[si];
        const char *p = NULL;

        if (c != 0 && (strchr(STR_DIGITS, c) != NULL || c == st->decimal)) {
            if (di == 0xffff) di = si;
            continue;
        }
        if (di != 0xffff) { /* a digit group ends here: its neighbours choose the inflection */
            int16_t form;
            if (c == '.') {
                if (str[si + 1] == '-') form = 6;
                else if (di != 0 && str[di - 1] == '.') form = 5;
                else if (di != 0 && str[di - 1] == '-') form = 7;
                else form = 2;
            } else {
                if (di != 0 && str[di - 1] == '-') form = 4;
                else if (c == '-') form = 3;
                else form = 1;
            }
            if (c == 0x15) form = 2; /* the section sign */
            memmove(b, str + di, (size_t)(si - di));
            b[si - di] = 0;
            number_to_words(st, b, st->gbuf, form);
            mp_speak_phonemes(st, st->gbuf, MP_PATH_NUMBER);
            di = 0xffff;
        }
        if (c == ',' && str[si + 1] == '-') { /* "12,-" */
            strcpy(st->gbuf, STR_MARKKAA);
            mp_speak_phonemes(st, st->gbuf, MP_PATH_NUMBER);
            continue;
        }
        if (c == '.' || c == '-' || c == st->decimal) continue;
        switch (c) { /* table at 0x12ec8 */
        case '$': p = STR_DOLLARIA; break;
        case '%': p = STR_PROSENTTIA; break;
        case 0x9c: p = STR_PUNTAA; break;
        case 0xf8: p = STR_ASTETTA; break;
        default:
            if (c == ')' && st->paren == 2) {
                S(MP_VOLUME)++;
                st->paren = 1;
            } else {
                b[0] = (char)c;
                b[1] = 0;
                if (lookup_exception(st, b)) p = b;
            }
            break;
        }
        if (p) {
            if (p != b) strcpy(b, p);
            mp_speak_phonemes(st, b, MP_PATH_NUMBER);
        }
    }
}

/* ---- words --------------------------------------------------------------------------------- */

static void speak_char(mp_state *st, char c, int path)
{
    st->gbuf[0] = c;
    st->gbuf[1] = 0;
    if (lookup_exception(st, st->gbuf)) mp_speak_phonemes(st, st->gbuf, path);
}

/* 0x12a4f: a run that began with a letter (it may carry specials in front and behind) */
static void speak_word(mp_state *st, char *w)
{
    uint16_t i;
    char *p;

    for (i = 0; w[i] != 0; i++) {
        char c = w[i];
        if (is_letter((uint8_t)c) || (st->paren == 1 && c == '(')) break;
        if (c != '_' && c != '#' && c != '-') speak_char(st, c, MP_PATH_WORD);
    }
    w += i;
    if (*w == 0) return;
    lookup_exception(st, w);
    st->digits = memcmp(w, STR_PUHELIN, 7) == 0;
    for (i = 0; w[i] != 0 && !is_vowel_char((uint8_t)w[i]); i++) {}
    if (w[i] == 0) { /* no vowel: an abbreviation, spelled out */
        for (i = 0; w[i] != 0; i++) speak_char(st, w[i], MP_PATH_WORD);
        return;
    }
    for (i = 0; REWRITE_FROM[i] != 0; i++)
        while ((p = strchr(w, REWRITE_FROM[i])) != NULL) replace_char(p, REWRITE_TO[i]);
    while ((p = strchr(w, 'c')) != NULL) *p = (char)st->c_char;
    /* DS:0x1da1 would also rewrite j as "&i_"; nothing in the program sets it */
    mp_speak_phonemes(st, w, MP_PATH_WORD);
    for (i = 0; w[i] != 0; i++) speak_char(st, w[i], MP_PATH_WORD); /* what the word stage handed back */
}

/* 0x126eb: one piece of text. Characters are gathered into runs of one class and each run goes to
 * its handler. The class table is in notes/FUNCTIONS.md. */
int mp_speak(mp_state *st, const char *text)
{
    char run[0x400];
    const uint8_t *t = (const uint8_t *)text;
    int16_t saved_volume = S(MP_VOLUME);
    uint16_t n = 0, k;
    int prev, cls, nl = 0;
    size_t i;
    uint8_t c;

    st->decimal = S(MP_DECIMAL_DOT) ? '.' : ',';
    st->paren = S(MP_PAREN_QUIET) ? 1 : 0;
    st->level = 1;
    st->digits = 0;
    c = t[0];
    if (c == 0) return 0;
    prev = char_class(&c);
    if (prev != 4) run[n++] = (char)c;
    mp_dev_start(st, st->ring_size);
    st->first = 1;
    if (st->trace) st->trace(st, "S", NULL);
    for (i = 1; t[i - 1] != 0; i++) {
        c = t[i];
        cls = (c == 0) ? 4 : char_class(&c);
        if (n > RUN_MAX) cls = 4;
        if (cls != prev) {
            if (prev == 1) {
                if (cls == 3) {
                    cls = 1;
                } else {
                    run[n] = 0;
                    speak_word(st, run);
                    n = 0;
                }
            } else if (prev == 2) {
                if (c == ' ' || cls == 3) {
                    cls = 2;
                } else {
                    run[n] = 0;
                    speak_number(st, run);
                    n = 0;
                    st->digits = 0;
                }
            } else if (prev == 3 && cls != 1 && cls != 2) {
                for (k = 0; k < n; k++) speak_char(st, run[k], MP_PATH_CHAR);
                n = 0;
            }
        }
        if (c == '\n') {
            if (!nl) {
                text_pause(st, 20000);
                nl = 1;
            }
        } else {
            nl = 0;
        }
        if (cls != 4) run[n++] = (char)c;
        if (cls == 2 && c == ' ') n--;
        prev = cls;
    }
    S(MP_VOLUME) = saved_volume;
    if (st->trace) st->trace(st, "E", NULL);
    return 0;
}
