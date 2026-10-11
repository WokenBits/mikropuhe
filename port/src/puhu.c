/*
 * puhu: MikroPuhe 1.0 for Linux, macOS and Windows. Speaks Finnish text with the original voice files.
 *
 * The program layer follows PUHU.EXE's main (0x11047): options, then text cut into sentences
 * (0x10ce0), each passed to the speech core; options may also appear inside the text (0x1099c).
 * New here: UTF-8 input and the outputs (out.c): an emulated D/A converter or PC speaker, written
 * as WAV or MP3 or played through ALSA.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mikropuhe.h"
#include "out.h"
#include "ui.h"

#define PUHU_VERSION "1.2"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#define MKDIR(d) _mkdir(d)
#else
#define MKDIR(d) mkdir(d, 0755)
#endif

/* The directory this program file is in, with links resolved; empty if it cannot be found. The
 * voices of this repository are looked for relative to it. */
static void program_dir(char *buf, size_t n, const char *argv0)
{
    char path[PATH_MAX], *slash;
    path[0] = 0;
#if defined(_WIN32)
    {
        DWORD k = GetModuleFileNameA(NULL, path, sizeof path);
        if (k >= sizeof path) k = 0;
        path[k] = 0;
        for (slash = path; *slash; slash++) /* the C library takes either kind of slash */
            if (*slash == '\\') *slash = '/';
    }
    (void)argv0;
#elif defined(__APPLE__)
    {
        char raw[PATH_MAX];
        uint32_t size = sizeof raw;
        if (_NSGetExecutablePath(raw, &size) != 0 || !realpath(raw, path)) path[0] = 0;
    }
#elif defined(__linux__)
    {
        ssize_t k = readlink("/proc/self/exe", path, sizeof path - 1);
        path[k < 0 ? 0 : k] = 0;
    }
#endif
#ifndef _WIN32
    if (!path[0] && (!argv0 || !strchr(argv0, '/') || !realpath(argv0, path))) path[0] = 0;
#endif
    slash = strrchr(path, '/');
    if (slash) *slash = 0;
    snprintf(buf, n, "%.*s", (int)n - 1, path);
}

typedef struct {
    mp_state st;
    mp_voice voice;
    int voice_index;
    int16_t set[MP_NSETTINGS]; /* the program's copy of the settings, DS:0x1a98 */
    const char *voices_dir;
    int voices_dir_given;   /* --voices-dir: use exactly that directory, no copies of one's own */
    const char *voice_file; /* -v PATH: a voice file of one's own, loaded as voice number 3 */
    FILE *trace;
    /* the full-screen interface */
    mp_out *out;
    const char *alsa_device;
    int ui_silent; /* 1: scripted test, nothing is played; 2: playing failed once, do not try again */
} program;

static const char *const VOICE_FILE[3] = {"JANNE.SMP", "KRISTIAN.SMP", "SOPHIA.SMP"};

/* Where one's own copies of the voices live. The original saved the speech settings into the voice
 * file itself (F2 in its settings screen). The shipped voices are kept unchanged here, so saving
 * makes a copy in this directory, and a copy found here is used instead of the shipped voice. */
static void user_dir(char *buf, size_t n)
{
#ifdef _WIN32
    const char *app = getenv("APPDATA"); /* C:\Users\NAME\AppData\Roaming */
    char *p;
    snprintf(buf, n, "%s/mikropuhe", app && *app ? app : ".");
    for (p = buf; *p; p++)
        if (*p == '\\') *p = '/';
#else
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(buf, n, "%s/mikropuhe", xdg);
    else snprintf(buf, n, "%s/.local/share/mikropuhe", home ? home : ".");
#endif
}

/* The file a voice is loaded from: -v PATH; else one's own copy, if there is one; else the shipped one. */
static void voice_path(const program *pg, int index, char *buf, size_t n)
{
    if (index == 3) {
        snprintf(buf, n, "%s", pg->voice_file);
        return;
    }
    if (!pg->voices_dir_given) {
        char dir[900];
        user_dir(dir, sizeof dir);
        snprintf(buf, n, "%s/%s", dir, VOICE_FILE[index]);
        if (access(buf, R_OK) == 0) return;
    }
    snprintf(buf, n, "%s/%s", pg->voices_dir, VOICE_FILE[index]);
}
/* A voice other than the three, given by name: NAME.SMP among one's own voices, in the voices
 * directory, or in this repository's voices/ (where the synthetic voice Kone is). */
static int other_voice(const program *pg, const char *name, const char *argv0, char *buf, size_t n)
{
    char up[64], dir[900];
    size_t i;
    int k;
    if (strlen(name) >= sizeof up - 4 || !*name) return 0;
    for (i = 0; name[i]; i++) up[i] = (char)toupper((unsigned char)name[i]);
    strcpy(up + i, ".SMP");
    for (k = 0; k < 3; k++) {
        if (k == 0) user_dir(dir, sizeof dir);
        else if (k == 1) snprintf(dir, sizeof dir, "%s", pg->voices_dir);
        else {
            program_dir(dir, sizeof dir - 16, argv0);
            strcat(dir, "/../../voices");
        }
        if (k == 0 && pg->voices_dir_given) continue;
        snprintf(buf, n, "%s/%s", dir, up);
        if (access(buf, R_OK) == 0) return 1;
    }
    return 0;
}

static const char OPTION_CODES[] = "NOPKORKRVVOIKNPETPJANKRISOPEIT"; /* DS:0x012f */

/* ---- text encoding ---------------------------------------------------------------------------- */

/* UTF-8 -> CP437, the program's character set. Unknown characters become spaces. */
static char *utf8_to_cp437(const char *in)
{
    static const struct {
        unsigned cp;
        uint8_t ch;
    } map[] = {{0xe4, 0x84}, {0xf6, 0x94}, {0xe5, 0x86}, {0xc4, 0x8e}, {0xd6, 0x99}, {0xc5, 0x8f}, {0xfc, 0x81},
               {0xdc, 0x9a}, {0xe9, 0x82}, {0xe6, 0x91}, {0xc6, 0x92}, {0xa3, 0x9c}, {0xb0, 0xf8}, {0xa7, 0x15},
               {0xb2, 0xfd}, {0x3c6, 0xed}, {0x207f, 0xfc}, {0x221a, 0xfb}, {0x2013, '-'}, {0x2014, '-'},
               {0x201c, '"'}, {0x201d, '"'}, {0x2019, '\''}, {0xa0, ' '}, {0x20ac, 'e'}};
    char *out = malloc(strlen(in) + 1), *o = out;
    const unsigned char *p = (const unsigned char *)in;

    if (!out) return NULL;
    while (*p) {
        unsigned cp = *p++;
        int extra = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
        size_t i;
        if (extra) cp &= 0x3fu >> extra;
        while (extra-- && (*p & 0xc0) == 0x80) cp = cp << 6 | (*p++ & 0x3f);
        if (cp < 0x80) {
            *o++ = (char)cp;
            continue;
        }
        if ((cp == 0x308 || cp == 0x30a) && o > out) {
            /* a letter followed by a combining diaeresis or ring: how macOS writes ä ö å in file
             * names and in some pasted text */
            static const char base[] = "aouAOU", dia[] = "\x84\x94\x81\x8e\x99\x9a";
            const char *b = strchr(base, o[-1]);
            if (cp == 0x308 && b && o[-1]) o[-1] = dia[b - base];
            else if (cp == 0x30a && o[-1] == 'a') o[-1] = (char)0x86;
            else if (cp == 0x30a && o[-1] == 'A') o[-1] = (char)0x8f;
            continue;
        }
        for (i = 0; i < sizeof map / sizeof map[0]; i++)
            if (map[i].cp == cp) break;
        *o++ = i < sizeof map / sizeof map[0] ? (char)map[i].ch : ' ';
    }
    *o = 0;
    return out;
}

/* ---- program layer ---------------------------------------------------------------------------- */

static void trace_cb(mp_state *st, const char *what, const char *s)
{
    program *pg = (program *)((char *)st - offsetof(program, st));
    int i;
    if (!pg->trace) return;
    if (what[0] == 'S') {
        fprintf(pg->trace, "S %u", st->ring_size);
        for (i = 0; i < MP_NSETTINGS; i++) fprintf(pg->trace, " %d", st->set[i]);
        fputc('\n', pg->trace);
    } else if (what[0] == 'W') {
        if (getenv("PUHU_TRACE_FRAMES")) { /* debugging the stale-stack model */
            const uint8_t *f = mp_frame(st, st->path);
            fprintf(pg->trace, "F ");
            for (i = 0; i < 0x360; i++) fprintf(pg->trace, "%02x", f[i]);
            fprintf(pg->trace, " %d\n", st->path);
        }
        fprintf(pg->trace, "G %d %d %d %d\nW ", st->level, st->first, st->paren, st->set[MP_VOLUME]);
        for (; *s; s++) fprintf(pg->trace, "%02x", (unsigned char)*s);
        fputc('\n', pg->trace);
    } else if (what[0] == 'P') {
        fprintf(pg->trace, "P %s\n", s);
    } else {
        fprintf(pg->trace, "E\n");
    }
}

static int load_voice(program *pg, int index)
{
    char path[1024], err[1200];
    mp_voice v;
    uint8_t stack[sizeof pg->st.stack];
    mp_sink sink = pg->st.sink;

    voice_path(pg, index, path, sizeof path);
    if (mp_voice_load(&v, path, err, sizeof err) != 0) {
        fprintf(stderr, "puhu: %s\n", err);
        return -1;
    }
    memcpy(stack, pg->st.stack, sizeof stack);
    mp_voice_free(&pg->voice);
    pg->voice = v;
    pg->voice_index = index;
    mp_init(&pg->st, &pg->voice, sink);
    memcpy(pg->st.stack, stack, sizeof stack);
    pg->st.trace = trace_cb;
    memcpy(pg->set, pg->st.set, sizeof pg->set); /* the voice's saved settings become current (0x12398) */
    return 0;
}

/* 0x10669: an option at p. mode 0 only recognises it, 2 applies it. Returns its length, 0 if none. */
static int parse_option(program *pg, const char *p, int mode)
{
    int i, has_digit;
    if (*p != '/' && *p != '+' && *p != '-') return 0;
    p++;
    for (i = 0; i < 10; i++)
        if (strncasecmp(p, OPTION_CODES + 3 * i, 3) == 0) break;
    if (i == 10) return 0;
    has_digit = i < 6;
    if (has_digit && !(p[3] >= '0' && p[3] <= '9')) return 0;
    if (mode != 0) {
        if (i < 6) {
            pg->set[i] = (int16_t)(p[3] - '0');
        } else if (i < 9) {
            if (i - 6 != pg->voice_index && load_voice(pg, i - 6) != 0) exit(1);
        }
        mp_set(&pg->st, pg->set);
        memcpy(pg->set, pg->st.set, sizeof pg->set);
    }
    return has_digit + 4;
}

/* 0x108ef */
static void speak_run(program *pg, const char *text, size_t len)
{
    char *copy = malloc(len + 1);
    size_t i;
    if (!copy) return;
    memcpy(copy, text, len);
    copy[len] = 0;
    for (i = 0; i < len; i++)
        if (copy[i] == '\n') copy[i] = ' ';
    mp_speak(&pg->st, copy);
    free(copy);
}

/* 0x1099c without the screen output: speak a piece, applying options found inside it */
static void speak_piece(program *pg, const char *text, size_t len)
{
    const char *seg = text, *p = text, *end = text + len;

    while (p < end) {
        int n = parse_option(pg, p, 0);
        if (n == 0) {
            p++;
            continue;
        }
        pg->set[MP_SENTENCE_PAUSE] = pg->st.ring_size < 32000;
        mp_set(&pg->st, pg->set);
        speak_run(pg, seg, (size_t)(p - seg));
        n = parse_option(pg, p, 2);
        p += n;
        seg = p;
    }
    pg->set[MP_SENTENCE_PAUSE] = pg->st.ring_size < 32000;
    mp_set(&pg->st, pg->set);
    speak_run(pg, seg, (size_t)(end - seg));
}

/* 0x10ce0: cut at . : ? ! followed by a space or a newline, and speak piece by piece */
static void speak_text(program *pg, const char *text)
{
    const char *start = text;
    while (*start) {
        const char *cur = start, *p;
        for (;;) {
            p = strpbrk(cur, ".:?!");
            if (!p) {
                p = cur + strlen(cur);
                break;
            }
            if (p[1] == ' ' || p[1] == '\n') {
                p += 2;
                break;
            }
            if (p[1] == 0) {
                p++;
                break;
            }
            cur = p + 1;
        }
        /* the library's own sentence pauses are on only with a small ring buffer (DS:0x1a34) */
        pg->set[MP_SENTENCE_PAUSE] = pg->st.ring_size < 32000;
        mp_set(&pg->st, pg->set);
        speak_piece(pg, start, (size_t)(p - start));
        pg->set[MP_SENTENCE_PAUSE] = 1;
        mp_set(&pg->st, pg->set);
        start = p;
    }
}

/* UTF-8 (or CP437 with raw) text as it came in -> what speak_text wants. Returns a new string. */
#ifdef _WIN32
static int valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int extra = *p < 0x80 ? 0 : *p >= 0xc2 && *p < 0xe0 ? 1 : *p >= 0xe0 && *p < 0xf0 ? 2 : *p >= 0xf0 && *p < 0xf5 ? 3 : -1;
        if (extra < 0) return 0;
        for (p++; extra > 0; extra--, p++)
            if ((*p & 0xc0) != 0x80) return 0;
    }
    return 1;
}

static int text_from_file; /* -f: the text is a file, not what a console program wrote into a pipe */

/* Windows programs do not agree on an encoding: cmd's "echo" writes the console's code page (850
 * or 437 for Finnish), Notepad used to save the "ANSI" one (1252). Text that is not UTF-8 is read
 * in the one that fits where it came from. Returns a new UTF-8 string, or NULL to use the text as
 * it is. */
static char *windows_text(const char *s)
{
    DWORD cp = text_from_file ? 0 : GetConsoleCP(), k;
    wchar_t *w;
    char *u = NULL;
    int n;
    if (valid_utf8(s)) return NULL;
    if (!cp && GetLocaleInfoW(LOCALE_USER_DEFAULT, (text_from_file ? LOCALE_IDEFAULTANSICODEPAGE : LOCALE_IDEFAULTCODEPAGE) | LOCALE_RETURN_NUMBER,
                              (LPWSTR)&k, sizeof k / sizeof(WCHAR)))
        cp = k;
    if (!cp || cp == CP_UTF8) return NULL;
    n = MultiByteToWideChar(cp, 0, s, -1, NULL, 0);
    w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w && MultiByteToWideChar(cp, 0, s, -1, w, n)) {
        int m = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
        u = m > 0 ? malloc((size_t)m) : NULL;
        if (u && !WideCharToMultiByte(CP_UTF8, 0, w, -1, u, m, NULL, NULL)) free(u), u = NULL;
    }
    free(w);
    return u;
}

/* The arguments as UTF-8 whatever the system's code page is, and standard output left as it is
 * written: a WAV, and the test outputs, must not get their line ends changed. */
static void windows_start(int *argc, char ***argv)
{
    int n = 0, i;
    wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
    char **a = w ? calloc((size_t)n + 1, sizeof *a) : NULL;
    _setmode(_fileno(stdout), _O_BINARY);
    if (!a) return;
    for (i = 0; i < n; i++) {
        int k = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
        a[i] = k > 0 ? malloc((size_t)k) : NULL;
        if (!a[i] || !WideCharToMultiByte(CP_UTF8, 0, w[i], -1, a[i], k, NULL, NULL)) return; /* keep the C library's */
    }
    LocalFree(w);
    *argc = n;
    *argv = a;
}
#endif

/* One line from f, its line end included, as getline() gives it; 0 at the end of the input. */
static size_t read_line(char **line, size_t *cap, FILE *f)
{
    size_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 2 > *cap) {
            size_t want = *cap ? *cap * 2 : 256;
            char *p = realloc(*line, want);
            if (!p) return 0;
            *line = p;
            *cap = want;
        }
        (*line)[n++] = (char)c;
        if (c == '\n') break;
    }
    if (n) (*line)[n] = 0;
    return n;
}

static char *prepare(const char *text, int raw)
{
    char *cp, *r, *w;
#ifdef _WIN32
    char *u = raw ? NULL : windows_text(text);
    cp = raw ? strdup(text) : utf8_to_cp437(u ? u : text);
    free(u);
#else
    cp = raw ? strdup(text) : utf8_to_cp437(text);
#endif
    if (!cp) return NULL;
    /* DOS text files: CR LF is one line end, and 0x1A ends the text */
    for (r = w = cp; *r && *r != 0x1a; r++)
        if (!(*r == '\r' && r[1] == '\n')) *w++ = *r;
    *w = 0;
    return cp;
}

static char *read_all(FILE *f)
{
    size_t cap = 65536, len = 0, n;
    char *buf = malloc(cap);
    while (buf && (n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (cap - len < 4096) buf = realloc(buf, cap *= 2);
    }
    if (buf) buf[len] = 0;
    return buf;
}

/* ---- what the full-screen interface needs from the program (ui.h) -------------------------------- */

static int ui_speak(void *ctx, const char *text)
{
    program *pg = ctx;
    mp_speak(&pg->st, text);
    mp_out_finish(pg->out);
    if (!pg->ui_silent && pg->out->len && mp_out_deliver(pg->out, NULL, pg->alsa_device) != 0) pg->ui_silent = 2;
    pg->out->len = 0;
    return pg->ui_silent == 2;
}

static void ui_apply(void *ctx)
{
    program *pg = ctx;
    mp_set(&pg->st, pg->set);
}

static void ui_select_voice(void *ctx, int old)
{
    program *pg = ctx;
    if (load_voice(pg, pg->voice_index) != 0) pg->voice_index = old;
}

static const char *ui_voice_name(void *ctx) { return ((program *)ctx)->voice.name; }

/* 0x11563: the settings block is written into the voice file, after its name */
static int ui_save(void *ctx)
{
    program *pg = ctx;
    char path[1024];
    unsigned char block[46];
    FILE *f;
    int i, len, ok;

    voice_path(pg, pg->voice_index, path, sizeof path);
    if (pg->voice_index == 3) {
        f = fopen(path, "r+b"); /* a voice file named with -v: the user's own */
    } else {
        /* A shipped voice is never written, whatever the file's permissions say: the voices
         * directory holds the originals. Settings go into a copy in the user's directory; with
         * --voices-dir, which rules such copies out, saving fails as on a write-protected disk. */
        char dir[900], copy[1024], buf[65536];
        size_t k;
        user_dir(dir, sizeof dir);
        snprintf(copy, sizeof copy, "%s/%s", dir, VOICE_FILE[pg->voice_index]);
        if (pg->voices_dir_given) return 5;
        if (strcmp(path, copy) != 0) { /* not yet a copy: make one */
            FILE *in = fopen(path, "rb"), *out;
            for (k = 1; dir[k]; k++) /* mkdir -p */
                if (dir[k] == '/') {
                    dir[k] = 0;
                    MKDIR(dir);
                    dir[k] = '/';
                }
            MKDIR(dir);
            out = in ? fopen(copy, "wb") : NULL;
            while (out && (k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, out);
            if (in) fclose(in);
            if (!out || fclose(out) != 0) return 5;
        }
        f = fopen(copy, "r+b");
    }
    if (!f) return 5;
    memset(block, 0xff, sizeof block);
    for (i = 0; i < MP_NSETTINGS; i++) {
        block[2 * i] = (unsigned char)(pg->set[i] & 0xff);
        block[2 * i + 1] = (unsigned char)((pg->set[i] >> 8) & 0xff);
    }
    ok = fseek(f, 51, SEEK_SET) == 0 && (len = fgetc(f)) != EOF && fseek(f, 52 + len, SEEK_SET) == 0 &&
         fwrite(block, 1, sizeof block, f) == sizeof block;
    return fclose(f) == 0 && ok ? 0 : 5;
}

static void ui_leaving(void *ctx)
{
    program *pg = ctx;
    if (pg->ui_silent == 2)
        fputs("puhu: the speech could not be played on this machine; 'puhu --why-silent' says why.\n", stderr);
}

static void usage(FILE *f)
{
    fputs("Usage: puhu [options] [text...]\n"
          "Speaks Finnish text with the MikroPuhe 1.0 voices. Text comes from the arguments,\n"
          "from -f FILE, or from standard input.\n\n"
          "Text and voice\n"
          "  -f FILE            read the text from FILE (UTF-8)\n"
          "  -v VOICE           janne, kristian or sophia (default janne); the name of another voice\n"
          "                     file in the voices folder (kone: a synthetic one); or the path of one\n"
          "  -s N               speed 0..8 (nopeus), 0 slowest\n"
          "  -p N               pitch 0..8 (korkeus)\n"
          "  -r N               pitch variation 0..8 (korkeuden vaihtelu)\n"
          "  -V N               volume 0..8 (voimakkuus); 4 is unity, the voices are saved at 8, which clips\n"
          "  --consonants 0|1   consonant emphasis (konsonanttien painotus)\n"
          "  --stress 0|1       first-syllable stress (ensimm\xc3\xa4isen tavun painotus)\n"
          "  --cp437            the text is already CP437, do not convert from UTF-8\n"
          "  --ui               the original's full-screen interface (the default when puhu is started\n"
          "                     at a terminal with no text); --lines speaks typed lines instead\n\n"
          "Output device (what the speech is played on, emulated)\n"
          "  -d, --device DEV   dac        the samples as a D/A converter plays them: 8-bit, 22000 Hz (default)\n"
          "                     pcspeaker  the PC speaker the program was written for: its 1-bit pulse train\n"
          "  --machine NAME     pcspeaker: whose delay loops time the pulses: ideal, 486, 386 (default), at, xt\n"
          "  --timing L,O[,E]   pcspeaker: your own calibration in ns: LOOP pass, sample overhead, unmeasured extra\n"
          "  --speaker MODEL    pcspeaker: cone (a small loudspeaker, default) or none (the electrical signal)\n"
          "  --rate HZ          pcspeaker: output sample rate, 22050..96000 (default 48000)\n"
          "  --nominal-pauses   dac: a pause of n lasts n samples, not the PC's n * 64 timer ticks\n\n"
          "Where it goes\n"
          "  -o FILE            write FILE.wav, FILE.mp3 or FILE.m4a; '-' writes WAV to standard output\n"
          "                     (also when standard output is a file or a pipe: puhu ... > hei.wav)\n"
          "  --play             play even though standard output is a file or a pipe\n"
#ifdef _WIN32
          "                     (on Windows the speech is played through the system's sound output)\n"
#else
          "  -D, --alsa-device NAME   play on this ALSA device (default: 'default', else an external player)\n"
#endif
          "  --list-devices     list output devices, machines and ALSA devices, and exit\n"
          "  --version          print the version\n"
          "  --why-silent       explain why nothing may be heard on this machine, and what to do\n"
          "                     (environment: PUHU_PLAY_CMD = your own player command, WAV on its stdin)\n"
          "  --voices-dir DIR   directory with JANNE.SMP, KRISTIAN.SMP, SOPHIA.SMP, used as it is\n"
          "                     (default: $MIKROPUHE_VOICES, else the folder of the program if the voices\n"
          "                     are in it, else this repository's original/files; a voice saved with F2 is\n"
#ifdef _WIN32
          "                     copied to %APPDATA%\\mikropuhe and used from there)\n\n"
#else
          "                     copied to ~/.local/share/mikropuhe and used from there)\n\n"
#endif
          "Settings not given keep the values saved in the voice file. The original's options also\n"
          "work inside the text: /NOP5 /KOR3 /KRV2 /VOI4 /KNP1 /ETP0 /JAN /KRI /SOP.\n",
          f);
}

static void list_devices(void)
{
    const mp_pcspk_timing *t;
    puts("Output devices (--device):");
    puts("  dac        the samples as a D/A converter plays them: 8-bit unsigned, 22000 Hz");
    puts("  pcspeaker  virtual PC speaker: the pulse-width modulated 1-bit signal of the original's player");
    puts("\nMachines for the PC speaker (--machine), with the calibration the original would measure:");
    for (t = MP_PCSPK_MACHINES; t->name; t++) {
        uint16_t table[257];
        unsigned b, widths = 1;
        mp_pcspk_table(t->loop_ns, t->overhead_ns, table);
        for (b = 2; b < 256; b++) widths += table[b + 1] != table[b]; /* the table never decreases */
        printf("  %-6s %-32s loop %5u ns, overhead %5u ns, %3u different pulse widths\n", t->name, t->what,
               (unsigned)t->loop_ns, (unsigned)t->overhead_ns, widths);
    }
    puts("\nALSA devices (--alsa-device):");
    mp_out_list_alsa();
}

int main(int argc, char **argv)
{
    static program pg;
    static mp_out out;
    const char *outpath = NULL, *file = NULL, *voice = "janne", *stack_file = NULL;
    const char *device = "dac", *machine = "386", *speaker = "cone", *timing = NULL, *alsa_device = NULL;
    mp_pcspk_timing custom = {"custom", "--timing", 0, 0, 0};
    int16_t opt[6] = {-1, -1, -1, -1, -1, -1};
    int raw = 0, i, vi = 0, stack_bp = 0, stream = 0, rc = 0, force_play = 0, ui = 0;
    const char *ui_keys = NULL;
    unsigned rate = 48000;
    static char found[1100];
    char dir[1024], *text = NULL, *cp;
    size_t tlen = 0;

#ifdef _WIN32
    windows_start(&argc, &argv);
#endif
    pg.voices_dir = getenv("MIKROPUHE_VOICES");
    if (!pg.voices_dir) {
        /* voice files next to the program, if they are there; else this repository's */
        size_t k;
        program_dir(dir, sizeof dir - 32, argv[0]);
        k = strlen(dir);
        strcat(dir, "/JANNE.SMP");
        if (access(dir, R_OK) == 0) dir[k] = 0;
        else strcpy(dir + k, "/../../original/files");
        pg.voices_dir = dir;
    }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        static const char *const flags[6] = {"-s", "-p", "-r", "-V", "--consonants", "--stress"};
        int k;
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(a, "--version") == 0) {
            puts("puhu " PUHU_VERSION " - MikroPuhe 1.0 (Timehouse Oy, 1990) re-implemented in C for Linux, macOS and Windows");
            return 0;
        }
        if (strcmp(a, "--list-devices") == 0) {
            list_devices();
            return 0;
        }
        if (strcmp(a, "--why-silent") == 0) {
            mp_out_why_silent();
            return 0;
        }
        if (strcmp(a, "--pc-speaker") == 0) device = "pcspeaker", machine = "ideal", speaker = "none"; /* old name */
        else if ((strcmp(a, "-d") == 0 || strcmp(a, "--device") == 0) && val) device = val, i++;
        else if (strcmp(a, "--machine") == 0 && val) machine = val, i++;
        else if (strcmp(a, "--timing") == 0 && val) timing = val, i++;
        else if (strcmp(a, "--speaker") == 0 && val) speaker = val, i++;
        else if (strcmp(a, "--rate") == 0 && val) rate = (unsigned)atoi(val), i++;
        else if ((strcmp(a, "-D") == 0 || strcmp(a, "--alsa-device") == 0) && val) alsa_device = val, i++;
        else if (strcmp(a, "--play") == 0) force_play = 1;
        else if (strcmp(a, "--ui") == 0) ui = 1;
        else if (strcmp(a, "--lines") == 0) ui = -1;
        else if (strcmp(a, "--ui-script") == 0 && val) ui_keys = val, ui = 1, i++; /* tests */
        else if (strcmp(a, "--nominal-pauses") == 0) out.nominal_pauses = 1;
        else if (strcmp(a, "--cp437") == 0) raw = 1;
        else if (strcmp(a, "--events") == 0) out.events = 1; /* tests */
        else if (strcmp(a, "--trace") == 0 && val) pg.trace = fopen(val, "w"), i++; /* tests */
        else if (strcmp(a, "--stack-image") == 0 && val) stack_file = val, i++; /* tests */
        else if (strcmp(a, "--stack-bp") == 0 && val) stack_bp = (int)strtol(val, NULL, 0), i++; /* tests */
        else if (strcmp(a, "-o") == 0 && val) outpath = val, i++;
        else if (strcmp(a, "-f") == 0 && val) file = val, i++;
        else if (strcmp(a, "-v") == 0 && val) voice = val, i++;
        else if (strcmp(a, "--voices-dir") == 0 && val) pg.voices_dir = val, pg.voices_dir_given = 1, i++;
        else if (strcmp(a, "--") == 0) {
            i++;
            break;
        } else {
            for (k = 0; k < 6; k++)
                if (strcmp(a, flags[k]) == 0 && val) break;
            if (k < 6) {
                opt[k] = (int16_t)atoi(val);
                i++;
            } else if (a[0] == '-' && a[1] != 0 && !isdigit((unsigned char)a[1]) && parse_option(&pg, a, 0) == 0) {
                fprintf(stderr, "puhu: unknown option %s\n", a);
                usage(stderr);
                return 2;
            } else {
                break;
            }
        }
    }
    if (strchr(voice, '/') || (strlen(voice) > 4 && strcasecmp(voice + strlen(voice) - 4, ".smp") == 0)) {
        pg.voice_file = voice; /* a path: a voice file of one's own (tools/smptool.py makes them) */
        vi = 3;
    } else if (strncasecmp(voice, "janne", strlen(voice)) != 0 && strncasecmp(voice, "kristian", strlen(voice)) != 0 &&
               strncasecmp(voice, "sophia", strlen(voice)) != 0 && other_voice(&pg, voice, argv[0], found, sizeof found)) {
        pg.voice_file = found; /* another voice by name: NAME.SMP in one of the voice folders */
        vi = 3;
    } else if (strncasecmp(voice, "k", 1) == 0) vi = 1;
    else if (strncasecmp(voice, "s", 1) == 0) vi = 2;
    else if (strncasecmp(voice, "j", 1) != 0) {
        fprintf(stderr, "puhu: unknown voice %s (janne, kristian, sophia, the name of another voice file in the\n"
                        "      voices folder, or the path of a voice file)\n", voice);
        return 2;
    }

    /* "puhu ... > hei.wav" and "puhu ... | aplay": WAV to standard output, unless playing was asked for */
    if (!outpath && !alsa_device && !force_play && !out.events) {
        struct stat st;
        if (!isatty(STDOUT_FILENO) && fstat(STDOUT_FILENO, &st) == 0 && (S_ISREG(st.st_mode) || S_ISFIFO(st.st_mode)))
            outpath = "-";
    }
    if (file) {
        FILE *f = fopen(file, "rb");
#ifdef _WIN32
        text_from_file = 1;
#endif
        if (!f) {
            fprintf(stderr, "puhu: %s: %s\n", file, strerror(errno));
            return 1;
        }
        text = read_all(f);
        fclose(f);
    } else if (i < argc) {
        for (; i < argc; i++) { /* arguments joined with single spaces, as 0x10e29 does */
            size_t n = strlen(argv[i]);
            text = realloc(text, tlen + n + 2);
            if (!text) return 1;
            memcpy(text + tlen, argv[i], n);
            tlen += n;
            text[tlen++] = ' ';
        }
        text[tlen - 1] = 0;
    } else if (ui_keys || (ui == 1 && !outpath) ||
               (ui == 0 && !outpath && !out.events && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) {
        ui = 2; /* started with nothing to say, at a terminal: the full-screen interface, as the original did */
    } else if (outpath || out.events) {
        text = read_all(stdin);
    } else {
        stream = 1; /* playing from standard input: speak each line as it arrives */
    }
    if (!text && !stream && ui != 2) return 1;

    if (strcasecmp(device, "dac") == 0) {
        mp_out_init_dac(&out);
    } else if (strcasecmp(device, "pcspeaker") == 0 || strcasecmp(device, "pcspk") == 0) {
        const mp_pcspk_timing *t = mp_pcspk_machine(machine);
        int cone = strcasecmp(speaker, "cone") == 0;
        if (timing) {
            unsigned l = 0, o = 0, e = 0;
            if (sscanf(timing, "%u,%u,%u", &l, &o, &e) < 2 || l == 0) {
                fputs("puhu: --timing wants LOOP_NS,OVERHEAD_NS[,EXTRA_NS], for example 400,2500,300\n", stderr);
                return 2;
            }
            custom.loop_ns = l, custom.overhead_ns = o, custom.extra_ns = e;
            t = &custom;
        }
        if (!t || (!cone && strcasecmp(speaker, "none") != 0) || rate < 22050 || rate > 96000) {
            fputs("puhu: --machine is one of ideal, 486, 386, at, xt; --speaker is cone or none; "
                  "--rate is 22050..96000\n", stderr);
            return 2;
        }
        if (mp_pcspk_too_slow(t->loop_ns, t->overhead_ns)) { /* the original's own verdict and words (0x15291) */
            fputs("puhu: T\xc3\xa4m\xc3\xa4 laitteisto on liian hidas puhumiseen.\n"
                  "      (loop + overhead reach 45454 ns: this machine cannot make one pulse per sample)\n", stderr);
            return 1;
        }
        mp_out_init_pcspeaker(&out, t, rate, cone ? MP_SPEAKER_CONE : MP_SPEAKER_NONE);
    } else {
        fprintf(stderr, "puhu: unknown output device %s (dac, pcspeaker)\n", device);
        return 2;
    }

    pg.st.sink = mp_out_sink(&out);
    if (load_voice(&pg, vi) != 0) return 1;
    if (stack_file) { /* tests: start from the stack bytes the original had (see tests/check_e2e.py) */
        FILE *f = fopen(stack_file, "rb");
        static uint8_t img[0x2000];
        size_t n = f ? fread(img, 1, sizeof img, f) : 0;
        int lo = stack_bp - 0x500; /* stack[0x500] is bp of a word-path call */
        if (f) fclose(f);
        if (lo >= 0 && (size_t)lo + sizeof pg.st.stack <= n) memcpy(pg.st.stack, img + lo, sizeof pg.st.stack);
    }
    for (i = 0; i < 6; i++)
        if (opt[i] >= 0) pg.set[i] = opt[i];
    mp_set(&pg.st, pg.set);
    memcpy(pg.set, pg.st.set, sizeof pg.set);

    if (ui == 2) {
        mp_ui_host host = {&pg, ui_speak, pg.set, ui_apply, &pg.voice_index, ui_select_voice, ui_voice_name, ui_save,
                           ui_leaving};
        pg.out = &out;
        pg.alsa_device = alsa_device;
        out.quiet = 1;
        out.events = 0;
        if (ui_keys) {
            static int keys[4096];
            int n = 0;
            char *end;
            for (; n < 4096; n++) {
                long k = strtol(ui_keys, &end, 0);
                if (end == ui_keys) break;
                keys[n] = (int)k;
                ui_keys = end;
            }
            pg.ui_silent = 1;
            rc = mp_ui_script(&host, keys, n);
        } else {
            rc = mp_ui_run(&host); /* does not return: it ends the program on Esc */
        }
    } else if (stream) {
        char *line = NULL;
        size_t cap = 0;
        while (read_line(&line, &cap, stdin) > 0) {
            cp = prepare(line, raw);
            if (!cp) break;
            speak_text(&pg, cp);
            free(cp);
            mp_out_finish(&out);
            if (out.len && mp_out_deliver(&out, NULL, alsa_device) != 0) {
                rc = 1;
                break;
            }
            out.len = 0;
        }
        free(line);
    } else {
        cp = prepare(text, raw);
        if (!cp) return 1;
        speak_text(&pg, cp);
        free(cp);
        mp_out_finish(&out);
        if (!out.events) {
            if (out.len == 0) {
                fputs("puhu: nothing to say\n", stderr);
                rc = 1;
            } else {
                rc = mp_out_deliver(&out, outpath, alsa_device) == 0 ? 0 : 1;
            }
        }
    }
    if (pg.trace) fclose(pg.trace);
    free(text);
    free(out.data);
    mp_voice_free(&pg.voice);
    return rc;
}
