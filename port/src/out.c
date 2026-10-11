/* Output: emulated device -> PCM in memory -> WAV, MP3, standard output or ALSA. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#undef popen
#undef pclose
#define popen(cmd, mode) _popen(cmd, mode "b") /* pipes carry WAV data: binary */
#define pclose _pclose
#endif

#include "out.h"

static void append(mp_out *o, const void *p, size_t n, int fill)
{
    if (o->len + n > o->cap) {
        o->cap = (o->len + n) * 2 + 65536;
        o->data = realloc(o->data, o->cap);
        if (!o->data) {
            fputs("puhu: out of memory\n", stderr);
            exit(1);
        }
    }
    if (p) memcpy(o->data + o->len, p, n);
    else memset(o->data + o->len, fill, n);
    o->len += n;
}

/* ---- the two devices ---------------------------------------------------------------------------- */

static void on_samples(void *ctx, const uint8_t *p, size_t n)
{
    mp_out *o = ctx;
    if (o->events) {
        size_t i;
        fputs("s ", stdout);
        for (i = 0; i < n; i++) printf("%02x", p[i]);
        putchar('\n');
    } else if (o->device == MP_DEV_PCSPEAKER) {
        mp_pcspk_samples(&o->speaker, p, n);
    } else {
        append(o, p, n, 0);
    }
}

static void on_pause(void *ctx, unsigned n)
{
    mp_out *o = ctx;
    if (o->events) {
        printf("p %u\n", n);
    } else if (o->device == MP_DEV_PCSPEAKER) {
        mp_pcspk_pause(&o->speaker, n);
    } else if (o->nominal_pauses) {
        append(o, NULL, n, 0x80);
    } else { /* the PC timed a pause with its timer chip: n * 64 ticks */
        double exact = n * 64.0 * MP_SAMPLE_RATE / MP_PIT_HZ + o->carry;
        size_t whole = (size_t)exact;
        o->carry = exact - (double)whole;
        append(o, NULL, whole, 0x80);
    }
}

static void on_speaker_pcm(void *ctx, const int16_t *p, size_t n)
{
    append(ctx, p, n * sizeof *p, 0);
}

/* The samples as a D/A converter would play them: 8-bit, 22,000 Hz. */
void mp_out_init_dac(mp_out *o)
{
    o->device = MP_DEV_DAC;
    o->rate = MP_SAMPLE_RATE;
    o->bits = 8;
}

/* The pulse train of the PC speaker, rendered at `rate` in 16 bits. */
void mp_out_init_pcspeaker(mp_out *o, const mp_pcspk_timing *machine, unsigned rate, int speaker)
{
    o->device = MP_DEV_PCSPEAKER;
    o->rate = rate;
    o->bits = 16;
    mp_pcspk_init(&o->speaker, machine, rate, speaker, on_speaker_pcm, o);
}

mp_sink mp_out_sink(mp_out *o)
{
    mp_sink s = {on_samples, on_pause, o};
    return s;
}

void mp_out_finish(mp_out *o)
{
    if (o->device == MP_DEV_PCSPEAKER && !o->events) mp_pcspk_flush(&o->speaker);
}

/* ---- delivery ----------------------------------------------------------------------------------- */

static void put_le(FILE *f, unsigned long v, int bytes)
{
    while (bytes--) {
        fputc((int)(v & 0xff), f);
        v >>= 8;
    }
}

static int write_wav(FILE *f, const mp_out *o)
{
    unsigned bytes = o->bits / 8;
    fwrite("RIFF", 1, 4, f);
    put_le(f, 36 + (unsigned long)o->len, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4);
    put_le(f, 1, 2);
    put_le(f, 1, 2);
    put_le(f, o->rate, 4);
    put_le(f, (unsigned long)o->rate * bytes, 4);
    put_le(f, bytes, 2);
    put_le(f, o->bits, 2);
    fwrite("data", 1, 4, f);
    put_le(f, (unsigned long)o->len, 4);
    if (o->bits == 8) {
        fwrite(o->data, 1, o->len, f);
    } else { /* held in host order, written little-endian */
        const int16_t *p = (const int16_t *)(const void *)o->data;
        size_t i;
        for (i = 0; i < o->len / 2; i++) put_le(f, (uint16_t)p[i], 2);
    }
    return ferror(f) ? -1 : 0;
}

static int have(const char *cmd)
{
    char buf[300];
#ifdef _WIN32
    snprintf(buf, sizeof buf, "where %s >NUL 2>&1", cmd);
#else
    snprintf(buf, sizeof buf, "command -v %s >/dev/null 2>&1", cmd);
#endif
    return system(buf) == 0;
}

static int pipe_wav(const char *cmd, const mp_out *o)
{
    FILE *p = popen(cmd, "w");
    if (!p) return -1;
    write_wav(p, o);
    return pclose(p) == 0 ? 0 : -1;
}

static void shell_quote(char *dst, size_t n, const char *src)
{
    size_t k = 0;
    if (n < 4) return;
#ifdef _WIN32
    dst[k++] = '"'; /* cmd.exe: double quotes; a file name cannot contain one */
    for (; *src && k + 3 < n; src++)
        if (*src != '"') dst[k++] = *src;
    dst[k++] = '"';
#else
    dst[k++] = '\'';
    for (; *src && k + 6 < n; src++) {
        if (*src == '\'') {
            memcpy(dst + k, "'\\''", 4);
            k += 4;
        } else {
            dst[k++] = *src;
        }
    }
    dst[k++] = '\'';
#endif
    dst[k] = 0;
}

#ifdef HAVE_ALSA
static void alsa_silent(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
    (void)file, (void)line, (void)function, (void)err, (void)fmt;
}

/* quiet: this is only an attempt on the default device, and failing is not an error */
static int play_alsa(const mp_out *o, const char *device, int quiet)
{
    snd_pcm_t *pcm;
    size_t frame = o->bits / 8, left = o->len / frame;
    const uint8_t *p = o->data;
    int err;

    if (quiet) snd_lib_error_set_handler(alsa_silent);
    err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (quiet) snd_lib_error_set_handler(NULL);
    if (err < 0) {
        if (!quiet) fprintf(stderr, "puhu: ALSA device %s: %s\n", device, snd_strerror(err));
        return -1;
    }
    /* 1 = let ALSA convert rate and format if the device cannot take ours (needs a "plug" device) */
    err = snd_pcm_set_params(pcm, o->bits == 16 ? SND_PCM_FORMAT_S16 : SND_PCM_FORMAT_U8,
                             SND_PCM_ACCESS_RW_INTERLEAVED, 1, o->rate, 1, 200000);
    if (err < 0) {
        if (!quiet)
            fprintf(stderr, "puhu: ALSA device %s does not take %u Hz %u-bit mono: %s\n"
                        "      (a raw hw: device does no conversion; try plughw: instead)\n",
                device, o->rate, o->bits, snd_strerror(err));
        snd_pcm_close(pcm);
        return -1;
    }
    while (left > 0) {
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, p, left);
        if (n < 0) n = snd_pcm_recover(pcm, (int)n, 1);
        if (n < 0) {
            if (!quiet) fprintf(stderr, "puhu: ALSA write on %s: %s\n", device, snd_strerror((int)n));
            snd_pcm_close(pcm);
            return -1;
        }
        p += (size_t)n * frame;
        left -= (size_t)n;
    }
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    return 0;
}

void mp_out_list_alsa(void)
{
    void **hints, **h;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
        puts("  (ALSA gave no device list)");
        return;
    }
    for (h = hints; *h; h++) {
        char *name = snd_device_name_get_hint(*h, "NAME"), *desc = snd_device_name_get_hint(*h, "DESC"),
             *io = snd_device_name_get_hint(*h, "IOID");
        if (name && (!io || strcmp(io, "Output") == 0)) {
            char *nl = desc ? strchr(desc, '\n') : NULL;
            if (nl) *nl = 0;
            printf("  %-44s %s\n", name, desc ? desc : "");
        }
        free(name);
        free(desc);
        free(io);
    }
    snd_device_name_free_hint(hints);
}
#else
void mp_out_list_alsa(void)
{
#ifdef _WIN32
    puts("  (none: on Windows puhu plays through the system's default sound output)");
#else
    puts("  (this puhu was built without ALSA; playback goes through pw-play, aplay, paplay, afplay or ffplay)");
#endif
}
#endif

#ifdef _WIN32
/* Windows plays a WAV image from memory itself; no player program is needed. */
static int play_windows(const mp_out *o)
{
    unsigned bytes = o->bits / 8;
    size_t total = 44 + o->len, i;
    unsigned char *img = malloc(total), *p;
    BOOL ok;
    if (!img) return -1;
    memcpy(img, "RIFF", 4);
    p = img + 4;
#define LE(v, n) for (i = 0; i < (n); i++) *p++ = (unsigned char)(((unsigned long)(v)) >> (8 * i))
    LE(36 + o->len, 4);
    memcpy(p, "WAVEfmt ", 8), p += 8;
    LE(16, 4); LE(1, 2); LE(1, 2); LE(o->rate, 4); LE((unsigned long)o->rate * bytes, 4); LE(bytes, 2); LE(o->bits, 2);
    memcpy(p, "data", 4), p += 4;
    LE(o->len, 4);
#undef LE
    memcpy(p, o->data, o->len); /* 16-bit data is held in host order, which on Windows is little-endian */
    ok = PlaySoundA((LPCSTR)img, NULL, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
    free(img);
    return ok ? 0 : -1;
}
#else
/* Is there anything on this machine a player could play through? */
static int sound_reachable(void)
{
#ifdef __linux__
    return access("/dev/snd/controlC0", R_OK | W_OK) == 0 || system("pactl info >/dev/null 2>&1") == 0;
#else
    return 1;
#endif
}

/* Try the players that exist, quietly, until one manages to play. */
static int play_external(const mp_out *o)
{
    static const struct {
        const char *name, *cmd;
        int wants_file; /* cannot read a pipe */
        int blind;      /* exits with success even when it could not open a sound device */
    } players[] = {{"pw-play", "pw-play -", 0, 0},
                   {"aplay", "aplay -q", 0, 0},
                   {"paplay", "paplay", 1, 0},
                   {"afplay", "afplay", 1, 0},
                   {"ffplay", "ffplay -loglevel quiet -nodisp -autoexit -", 0, 1}};
    char cmd[300];
    size_t i;

    for (i = 0; i < sizeof players / sizeof players[0]; i++) {
        int rc;
        if (!have(players[i].name)) continue;
        if (players[i].blind && !sound_reachable()) continue;
        if (players[i].wants_file) {
            char tmp[] = "/tmp/puhu-XXXXXX.wav"; /* afplay goes by the ending of the file name */
            int fd = mkstemps(tmp, 4);
            FILE *f = fd >= 0 ? fdopen(fd, "wb") : NULL;
            if (!f) continue;
            write_wav(f, o);
            fclose(f);
            snprintf(cmd, sizeof cmd, "%s %s 2>/dev/null", players[i].cmd, tmp);
            rc = system(cmd);
            unlink(tmp);
        } else {
            snprintf(cmd, sizeof cmd, "%s 2>/dev/null", players[i].cmd);
            rc = pipe_wav(cmd, o);
        }
        if (rc == 0) return 0;
    }
    return -1;
}

/* Last resort inside a VS Code remote terminal: the machine has no usable sound device, but the
 * editor on the other end can play a file. Returns 0 if the recording was handed to it. */
static int open_in_vscode(const mp_out *o)
{
    char path[200], cmd[300];
    FILE *f;
    if (o->quiet || !getenv("VSCODE_IPC_HOOK_CLI") || !have("code")) return -1;
    snprintf(path, sizeof path, "/tmp/puhu-%ld.wav", (long)getuid());
    f = fopen(path, "wb");
    if (!f) return -1;
    write_wav(f, o);
    if (fclose(f) != 0) return -1;
    snprintf(cmd, sizeof cmd, "code %s >/dev/null 2>&1", path);
    if (system(cmd) != 0) return -1;
    fprintf(stderr, "puhu: no sound device can be used from this session, so the recording was opened in\n"
                    "      VS Code instead (%s): press play there. See 'puhu --why-silent'.\n", path);
    return 0;
}

#endif /* not Windows */

void mp_out_why_silent(void)
{
#ifdef _WIN32
    puts("Why puhu may not be able to play here, and what to do:\n");
    puts("- On Windows puhu plays through the system's own sound output (PlaySound). If that is silent,\n"
         "  check the output device and the volume in the Windows sound settings.");
    puts("- Or write a file: puhu -o hei.wav \"hei\"");
#else
    int dev = access("/dev/snd", F_OK) == 0, ok = access("/dev/snd/controlC0", R_OK | W_OK) == 0;
    puts("Why puhu may not be able to play here, and what to do:\n");
#ifdef HAVE_ALSA
    puts("- This puhu plays through ALSA (device 'default', or the one given with -D).");
#else
    puts("- This puhu was built without ALSA and relies on pw-play, aplay, paplay, afplay or ffplay.");
#endif
#ifdef __APPLE__
    puts("- On macOS puhu plays through afplay, which is part of the system. If that is silent, check\n"
         "  the output device and volume:  afplay /System/Library/Sounds/Ping.aiff");
    (void)dev, (void)ok;
    return;
#endif
    if (!dev) puts("- There is no /dev/snd: this system has no ALSA sound cards.");
    else if (!ok)
        puts("- /dev/snd exists but this user may not open it. On a machine reached over SSH that is\n"
             "  normal: the devices belong to group 'audio' or to whoever sits at the screen.\n"
             "  To allow it:   sudo usermod -aG audio $USER     (then log in again)");
    else puts("- /dev/snd is accessible. Try 'puhu --list-devices' and name a device with -D, e.g. -D plughw:1,0.");
    puts("- In a VS Code remote terminal puhu opens the recording in the editor; press play there.");
    puts("- To have it play by itself on the machine you sit at, send it through an SSH tunnel:\n"
         "  set PUHU_PLAY_CMD='nc -N 127.0.0.1 4714' here and run  puhu-listen --tunnel THIS-HOST  there\n"
         "  (port/puhu-listen; step by step in docs/playback.md, \"Over SSH\").");
    puts("- To hear it on the machine you are sitting at, let this one only make the audio:\n"
         "    ssh HOST 'puhu -o - \"hei\"' | ffplay -nodisp -autoexit -loglevel error -\n"
         "    ssh HOST 'puhu -o - \"hei\"' > /tmp/hei.wav && afplay /tmp/hei.wav        (macOS)\n"
         "- Or write a file: puhu -o hei.wav \"hei\"   /   puhu -o hei.mp3 \"hei\"");
#endif
}

int mp_out_deliver(const mp_out *o, const char *path, const char *alsa_device)
{
    char cmd[1200], q[1024];

    if (path && strcmp(path, "-") == 0) return write_wav(stdout, o);
    if (path) {
        size_t n = strlen(path);
        if (n > 4 && strcasecmp(path + n - 4, ".mp3") == 0) {
            shell_quote(q, sizeof q, path);
            if (have("lame")) snprintf(cmd, sizeof cmd, "lame --quiet -V 4 - %s", q);
            else if (have("ffmpeg")) snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -i - %s", q);
            else {
                fputs("puhu: MP3 output needs lame or ffmpeg"
#ifdef __APPLE__
                      "; on macOS, -o FILE.m4a works without either"
#endif
                      "\n", stderr);
                return -1;
            }
            return pipe_wav(cmd, o);
        } else if (n > 4 && strcasecmp(path + n - 4, ".m4a") == 0) {
            /* AAC: with macOS's own afconvert (it wants a file to read), else with ffmpeg */
            shell_quote(q, sizeof q, path);
#ifndef _WIN32
            if (have("afconvert")) {
                char tmp[] = "/tmp/puhu-XXXXXX.wav";
                int fd = mkstemps(tmp, 4), rc;
                FILE *f = fd >= 0 ? fdopen(fd, "wb") : NULL;
                if (!f) return -1;
                write_wav(f, o);
                fclose(f);
                snprintf(cmd, sizeof cmd, "afconvert -f m4af -d aac %s %s", tmp, q);
                rc = system(cmd);
                unlink(tmp);
                return rc == 0 ? 0 : -1;
            }
#endif
            if (have("ffmpeg")) {
                snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -i - %s", q);
                return pipe_wav(cmd, o);
            }
            fputs("puhu: M4A output needs afconvert (macOS) or ffmpeg\n", stderr);
            return -1;
        } else {
            FILE *f = fopen(path, "wb");
            int rc;
            if (!f) {
                fprintf(stderr, "puhu: %s: %s\n", path, strerror(errno));
                return -1;
            }
            rc = write_wav(f, o);
            return fclose(f) != 0 ? -1 : rc;
        }
    }
    /* PUHU_PLAY_CMD: a command of your own that gets the WAV on its standard input, for example
     * one that sends it through an SSH tunnel to the machine you sit at (docs/playback.md). */
    if (!alsa_device && getenv("PUHU_PLAY_CMD") && *getenv("PUHU_PLAY_CMD")) {
        if (pipe_wav(getenv("PUHU_PLAY_CMD"), o) == 0) return 0;
        if (!o->quiet) fputs("puhu: PUHU_PLAY_CMD failed; trying the usual ways\n", stderr);
    }
#ifdef HAVE_ALSA
    if (alsa_device) return play_alsa(o, alsa_device, o->quiet);
    if (play_alsa(o, "default", 1) == 0) return 0;
#else
    if (alsa_device) {
        fputs("puhu: built without ALSA, so --alsa-device is not available\n", stderr);
        return -1;
    }
#endif
#ifdef _WIN32
    if (play_windows(o) == 0) return 0;
    if (!o->quiet) fputs("puhu: could not play through the Windows sound output. Use -o FILE, or see 'puhu --why-silent'.\n", stderr);
    return -1;
#else
    if (play_external(o) == 0) return 0;
    if (open_in_vscode(o) == 0) return 0;
    if (!o->quiet)
        fputs("puhu: could not play: no sound device is usable from this session, and no player worked.\n"
              "      Use -o FILE, or see 'puhu --why-silent'.\n", stderr);
    return -1;
#endif
}
