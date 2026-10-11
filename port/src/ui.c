/*
 * The full-screen interface, after segments 111c (screens) and 1594 (the input field) of PUHU.EXE.
 *
 * The original draws with Turbo C's console functions on the 80x25 text screen. Here the same calls
 * (gotoxy, textattr, putch, clreol, clrscr; coordinates from 1) draw into a buffer of 25x80 character
 * cells with CGA attributes. A terminal backend shows that buffer with ANSI escape sequences and
 * reads keys; a script backend feeds key codes and prints the buffer, so the result can be compared
 * cell by cell with the original running in the emulator (tests/check_ui.py).
 *
 * Key codes are the original's (0x125e6): a character, or 0x100 + scan code for the extended keys.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "ui.h"
#include "ui_tables.h"

#define K_ENTER 0x0d
#define K_ESC 0x1b
#define K_BS 0x08
#define K_CLEAR 0x7f /* Ctrl-Backspace on the PC keyboard */
#define K_F1 0x13b
#define K_F2 0x13c
#define K_HOME 0x147
#define K_UP 0x148
#define K_LEFT 0x14b
#define K_RIGHT 0x14d
#define K_END 0x14f
#define K_DOWN 0x150
#define K_DEL 0x153
#define K_CTRL_LEFT 0x173
#define K_CTRL_RIGHT 0x174

/* the colour set of 0x11ada for an 80-column colour screen */
#define A_TEXT 0x0f      /* DS:0x1a39, DS:0x1a3b */
#define A_SELECTED 0x1f  /* DS:0x1a37 */
#define A_BAR 0x1f       /* DS:0x1a3d */
#define A_BAR_DIM 0x17   /* DS:0x1a3f */

typedef struct {
    unsigned char ch, at;
} cell;

typedef struct ui {
    const mp_ui_host *host;
    cell scr[25][80];
    int cx, cy; /* cursor, from 1 */
    unsigned char attr;
    /* backends */
    const int *keys;
    int nkeys, script;
#ifdef _WIN32
    HANDLE con_in, con_out;
    DWORD saved_in, saved_out;
    UINT saved_cp;
#else
    struct termios saved_tty;
#endif
    int tty_raw;
    char caption[400]; /* terminal only: what was just said, shown as text when it could not be heard */
} ui;

static void ui_end(ui *u, const char *how, int status);

/* ---- console calls ------------------------------------------------------------------------------- */

static void gotoxy(ui *u, int x, int y)
{
    u->cx = x;
    u->cy = y;
}

static void textattr(ui *u, int a) { u->attr = (unsigned char)a; }

static void putch(ui *u, unsigned char c)
{
    if (u->cy >= 1 && u->cy <= 25 && u->cx >= 1 && u->cx <= 80) {
        u->scr[u->cy - 1][u->cx - 1].ch = c;
        u->scr[u->cy - 1][u->cx - 1].at = u->attr;
    }
    u->cx++;
}

static void cputs(ui *u, const char *s)
{
    for (; *s; s++) putch(u, (unsigned char)*s);
}

static void clreol(ui *u)
{
    int x;
    for (x = u->cx; x <= 80; x++) {
        u->scr[u->cy - 1][x - 1].ch = ' ';
        u->scr[u->cy - 1][x - 1].at = u->attr;
    }
}

static void clrscr(ui *u)
{
    for (u->cy = 1; u->cy <= 25; u->cy++) {
        u->cx = 1;
        clreol(u);
    }
    gotoxy(u, 1, 1);
}

/* ---- terminal ------------------------------------------------------------------------------------ */

/* CP437 -> Unicode for what the interface shows: the frame characters, arrows, and letters */
static unsigned cp437_unicode(unsigned char c)
{
    static const unsigned short high[128] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee,
        0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6,
        0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa,
        0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502,
        0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, 0x2514,
        0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
        0x256c, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518, 0x250c,
        0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
        0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229, 0x2261, 0x00b1, 0x2265, 0x2264, 0x2320,
        0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0};
    static const unsigned short low[32] = {
        0x0020, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25d8, 0x25cb, 0x25d9,
        0x2642, 0x2640, 0x266a, 0x266b, 0x263c, 0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7,
        0x25ac, 0x21a8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc};
    if (c < 0x20) return low[c];
    if (c == 0x7f) return 0x2302;
    return c < 0x80 ? c : high[c - 0x80];
}

static unsigned char unicode_cp437(unsigned cp)
{
    unsigned c;
    if (cp < 0x80) return (unsigned char)cp;
    for (c = 0x80; c < 0x100; c++)
        if (cp437_unicode((unsigned char)c) == cp) return (unsigned char)c;
    return 0;
}

static void put_utf8(FILE *f, unsigned cp)
{
    if (cp < 0x80) {
        fputc((int)cp, f);
    } else if (cp < 0x800) {
        fputc((int)(0xc0 | cp >> 6), f);
        fputc((int)(0x80 | (cp & 0x3f)), f);
    } else {
        fputc((int)(0xe0 | cp >> 12), f);
        fputc((int)(0x80 | ((cp >> 6) & 0x3f)), f);
        fputc((int)(0x80 | (cp & 0x3f)), f);
    }
}

/* Two things the original did not have, drawn over rows 21 to 24, which it leaves empty. They are
 * not part of the screen buffer, so the comparison with the original does not see them.
 *  - What was just said, as text, when it could not be played: the help (F1) is speech only.
 *  - Other keys for F1 and F2, which terminal programs often keep for themselves. */
static void overlay(ui *u)
{
    static const char hint[] = "F1 = Ctrl-A   F2 = Ctrl-P";
    const char *p = u->caption;
    int row;
    for (row = 21; row <= 23 && *p; row++) {
        int n = (int)strlen(p), i;
        if (n > 74) { /* break at a space */
            for (n = 74; n > 40 && p[n] != ' '; n--) {}
        }
        printf("\033[%d;4H\033[0;36m", row);
        for (i = 0; i < n; i++) put_utf8(stdout, cp437_unicode((unsigned char)(p[i] == '\n' ? ' ' : p[i])));
        p += n;
        while (*p == ' ') p++;
    }
    printf("\033[24;%dH\033[0;90m%s", 80 - (int)sizeof hint, hint);
}

/* the whole buffer to the terminal; CGA colour numbers to ANSI ones */
static void show(ui *u)
{
    static const int ansi[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    int r, c, last = -1;
    if (u->script) return;
    fputs("\033[?25l", stdout);
    for (r = 0; r < 25; r++) {
        printf("\033[%d;1H", r + 1);
        for (c = 0; c < 80; c++) {
            int at = u->scr[r][c].at;
            if (at != last) {
                int fg = at & 15, bg = (at >> 4) & 7;
                printf("\033[0;%d;%dm", (fg & 8 ? 90 : 30) + ansi[fg & 7], 40 + ansi[bg]);
                last = at;
            }
            put_utf8(stdout, cp437_unicode(u->scr[r][c].ch));
        }
    }
    overlay(u);
    printf("\033[0m\033[%d;%dH\033[?25h", u->cy < 1 ? 1 : u->cy > 25 ? 25 : u->cy, u->cx < 1 ? 1 : u->cx > 80 ? 80 : u->cx);
    fflush(stdout);
}

#ifdef _WIN32
/* ---- keys from the Windows console ---------------------------------------------------------- */

/* One key event as the original's key code; 0 for an event it has no code for. The console hands
 * over what the PC keyboard sent under DOS: Backspace is 0x08 and Ctrl-Backspace is 0x7f. */
static int console_key(const KEY_EVENT_RECORD *k)
{
    DWORD st = k->dwControlKeyState;
    int ctrl = (st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    unsigned ch = k->uChar.UnicodeChar;

    if (!k->bKeyDown) /* a character typed as Alt + digits arrives when Alt is let go */
        return k->wVirtualKeyCode == VK_MENU && ch ? unicode_cp437(ch) : 0;
    switch (k->wVirtualKeyCode) {
    case VK_F1: return K_F1;
    case VK_F2: return K_F2;
    case VK_UP: return K_UP;
    case VK_DOWN: return K_DOWN;
    case VK_LEFT: return ctrl ? K_CTRL_LEFT : K_LEFT;
    case VK_RIGHT: return ctrl ? K_CTRL_RIGHT : K_RIGHT;
    case VK_HOME: return K_HOME;
    case VK_END: return K_END;
    case VK_DELETE: return K_DEL;
    case VK_BACK: return ctrl ? K_CLEAR : K_BS;
    case VK_RETURN: return K_ENTER;
    case VK_ESCAPE: return K_ESC;
    }
    /* Alt-1 and Alt-2, the left Alt only: the right one is AltGr, which types characters */
    if ((st & LEFT_ALT_PRESSED) && !(st & RIGHT_ALT_PRESSED) && !ctrl) {
        if (k->wVirtualKeyCode == '1') return K_F1;
        if (k->wVirtualKeyCode == '2') return K_F2;
    }
    /* Ctrl-A, for "Apu", and Ctrl-P, for "Puhearvot": by character, or by key where none is given */
    if (ch == 0x01 || (ctrl && !ch && k->wVirtualKeyCode == 'A')) return K_F1;
    if (ch == 0x10 || (ctrl && !ch && k->wVirtualKeyCode == 'P')) return K_F2;
    if (ch < 0x80) return (int)ch;
    return unicode_cp437(ch);
}

/* Is a key waiting? Events that are not keys (key releases, window changes) are dropped on the way. */
static int byte_ready(ui *u)
{
    INPUT_RECORD r;
    DWORD n;
    while (PeekConsoleInputW(u->con_in, &r, 1, &n) && n == 1) {
        if (r.EventType == KEY_EVENT && console_key(&r.Event.KeyEvent)) return 1;
        ReadConsoleInputW(u->con_in, &r, 1, &n);
    }
    return 0;
}

static int terminal_key(ui *u)
{
    INPUT_RECORD r;
    DWORD n;
    for (;;) {
        int key;
        if (!ReadConsoleInputW(u->con_in, &r, 1, &n) || n != 1) ui_end(u, "exit", 0); /* the console went away */
        if (r.EventType == KEY_EVENT && (key = console_key(&r.Event.KeyEvent)) != 0) return key;
    }
}

#define KEY_WAITING(u) byte_ready(u)
#else
#define KEY_WAITING(u) byte_ready(0)

static int byte_ready(int ms)
{
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int read_byte(void)
{
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

/* One key from the terminal as the original's key code, 0 for something it has no code for. */
static int terminal_key(ui *u)
{
    static const struct {
        const char *seq;
        int key;
    } map[] = {{"OP", K_F1}, {"[11~", K_F1}, {"[[A", K_F1}, {"OQ", K_F2}, {"[12~", K_F2}, {"[[B", K_F2},
               {"[A", K_UP}, {"OA", K_UP}, {"[B", K_DOWN}, {"OB", K_DOWN}, {"[C", K_RIGHT}, {"OC", K_RIGHT},
               {"[D", K_LEFT}, {"OD", K_LEFT}, {"[H", K_HOME}, {"OH", K_HOME}, {"[1~", K_HOME}, {"[7~", K_HOME},
               {"[F", K_END}, {"OF", K_END}, {"[4~", K_END}, {"[8~", K_END}, {"[3~", K_DEL},
               {"[1;5D", K_CTRL_LEFT}, {"[5D", K_CTRL_LEFT}, {"[1;5C", K_CTRL_RIGHT}, {"[5C", K_CTRL_RIGHT},
               {"[3;5~", K_CLEAR}, {"1", K_F1}, {"2", K_F2}}; /* the last two: Alt-1, Alt-2 */
    char seq[16];
    size_t n = 0, i;
    int c = read_byte();

    if (c < 0) ui_end(u, "exit", 0); /* the terminal went away */
    if (c == '\r' || c == '\n') return K_ENTER;
    if (c == 0x7f) return K_BS;   /* the Backspace key of today's terminals */
    if (c == 0x08) return K_CLEAR; /* Ctrl-Backspace, or Ctrl-H */
    if (c == 0x01) return K_F1;    /* Ctrl-A, for "Apu": F1 often never reaches a program in a terminal */
    if (c == 0x10) return K_F2;    /* Ctrl-P, for "Puhearvot" */
    if (c == 0x1b) {
        if (!byte_ready(40)) return K_ESC;
        while (n < sizeof seq - 1 && byte_ready(5)) {
            int b = read_byte();
            if (b < 0) break;
            seq[n++] = (char)b;
            if (n > 1 && ((b >= 'A' && b <= 'Z') || b == '~')) break;
        }
        seq[n] = 0;
        for (i = 0; i < sizeof map / sizeof map[0]; i++)
            if (strcmp(seq, map[i].seq) == 0) return map[i].key;
        return 0;
    }
    if (c >= 0xc0) { /* UTF-8 */
        unsigned cp = (unsigned)c & (c >= 0xe0 ? 0x0f : 0x1f);
        int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        while (extra-- > 0 && byte_ready(5)) cp = cp << 6 | ((unsigned)read_byte() & 0x3f);
        return unicode_cp437(cp);
    }
    return c < 0x80 ? c : 0;
}
#endif

/* 0x1261d: is a key waiting? */
static int kbhit(ui *u)
{
    if (u->script) return 0; /* scripted keys arrive only when the program waits for them */
    return KEY_WAITING(u);
}

/* 0x1260d: wait for a key */
static int getkey(ui *u)
{
    if (u->script) {
        if (u->nkeys == 0) ui_end(u, "waits", 0);
        u->nkeys--;
        return *u->keys++;
    }
    show(u);
    return terminal_key(u);
}

static void ui_end(ui *u, const char *how, int status)
{
    int r, c;
    if (u->script) {
        printf("{\"end\": \"%s\", \"cursor\": [%d, %d], \"chars\": [", how, u->cy - 1, u->cx - 1);
        for (r = 0; r < 25; r++) {
            printf("%s\"", r ? ", " : "");
            for (c = 0; c < 80; c++) printf("%02x", u->scr[r][c].ch);
            putchar('"');
        }
        printf("], \"attrs\": [");
        for (r = 0; r < 25; r++) {
            printf("%s\"", r ? ", " : "");
            for (c = 0; c < 80; c++) printf("%02x", u->scr[r][c].at);
            putchar('"');
        }
        printf("]}\n");
    } else {
#ifndef _WIN32
        if (u->tty_raw) tcsetattr(STDIN_FILENO, TCSANOW, &u->saved_tty);
#endif
        fputs("\033[0m\033[?25h\033[?1049l", stdout); /* back to the screen from before */
        for (c = 0; c < 80 && u->scr[0][c].ch; c++) put_utf8(stdout, cp437_unicode(u->scr[0][c].ch));
        putchar('\n');
        fflush(stdout);
#ifdef _WIN32
        if (u->tty_raw) {
            SetConsoleOutputCP(u->saved_cp);
            SetConsoleMode(u->con_out, u->saved_out);
            SetConsoleMode(u->con_in, u->saved_in);
        }
#endif
        if (u->host->leaving) u->host->leaving(u->host->ctx);
    }
    exit(status);
}

static void speak(ui *u, const char *text)
{
    u->caption[0] = 0;
    show(u);
    if (u->host->speak(u->host->ctx, text) != 0) snprintf(u->caption, sizeof u->caption, "%s", text);
}

/* ---- the input field: segment 1594 ---------------------------------------------------------------- */

typedef struct field {
    const char *label;   /* +0x00 */
    char *buf;           /* +0x04 */
    const char *allowed; /* +0x08 characters accepted, NULL = all */
    int maxlen;          /* +0x0c */
    int len;             /* +0x0e */
    int x, right, row;   /* +0x10, +0x12, +0x14 */
    int boxed;           /* +0x16 */
    int a_label, a_text, a_fill, a_box; /* +0x17..+0x1a */
    int fx, width;       /* +0x1b first text column, +0x1d visible characters */
    int scroll, cur;     /* +0x1f first visible character, +0x21 cursor position in the text */
} field;

/* 0x1594c */
static void field_cursor(ui *u, field *f) { gotoxy(u, f->cur - f->scroll + f->fx, f->row + f->boxed); }

/* 0x15981 */
static void put_at(ui *u, int row, int col, const char *s, int attr)
{
    textattr(u, attr);
    gotoxy(u, col, row);
    cputs(u, s);
}

/* 0x15d60: a frame from (x1, y1) to (x2, y2); the inside of the middle rows is left alone */
static void frame(ui *u, int x1, int x2, int y1, int y2, int attr, int style)
{
    const unsigned char *p = UI_FRAME[style == 1 ? 0 : 1];
    int x, y;
    textattr(u, attr);
    for (y = y1; y <= y2; y++) {
        gotoxy(u, x1, y);
        putch(u, p[0]);
        if (y == y1 || y == y2)
            for (x = x1; x + 1 != x2; x++) putch(u, p[1]);
        else gotoxy(u, x2, y);
        putch(u, p[2]);
        if (y == y1 || y + 1 == y2) p += 3;
    }
}

/* 0x159d8: the visible part of the text */
static void field_redraw(ui *u, field *f)
{
    char tmp[82];
    int saved = u->attr, row = f->row + f->boxed, n;
    memset(tmp, ' ', (size_t)f->width);
    tmp[f->width] = 0;
    put_at(u, row, f->fx, tmp, f->a_fill);
    n = f->len - f->scroll < f->width ? f->len - f->scroll : f->width;
    if (n > 0) memcpy(tmp, f->buf + f->scroll, (size_t)n);
    tmp[n > 0 ? n : 0] = 0;
    put_at(u, row, f->fx, tmp, f->a_text);
    field_cursor(u, f);
    textattr(u, saved);
}

/* 0x15ac6: the visible part from text position pos on */
static void field_redraw_from(ui *u, field *f, int pos)
{
    char tmp[82];
    int saved = u->attr, row = f->row + f->boxed, off = pos - f->scroll, col = f->fx + off, n;
    if (f->width != off) {
        memset(tmp, ' ', (size_t)(f->width - off));
        tmp[f->width - off] = 0;
        put_at(u, row, col, tmp, f->a_fill);
    }
    n = f->len - pos < f->width - off ? f->len - pos : f->width - off;
    if (n > 0) memcpy(tmp, f->buf + pos, (size_t)n);
    tmp[n > 0 ? n : 0] = 0;
    put_at(u, row, col, tmp, f->a_text);
    field_cursor(u, f);
    textattr(u, saved);
}

/* 0x15bd5: cursor to pos, scrolling towards the start of the text when it comes within 4 of the edge */
static void field_left(ui *u, field *f, int pos, int redraw)
{
    int margin = f->width < 10 ? 0 : 4, scroll = f->scroll;
    if (pos - scroll < margin && scroll > 0) scroll = pos > margin ? pos - margin : 0;
    if (scroll == f->scroll) {
        if (!redraw) {
            f->cur = pos;
            field_cursor(u, f);
        } else {
            int from = pos < f->cur ? pos : f->cur;
            f->cur = pos;
            field_redraw_from(u, f, from);
        }
    } else {
        f->scroll = scroll;
        f->cur = pos;
        field_redraw(u, f);
    }
}

/* 0x15c85: the same towards the end */
static void field_right(ui *u, field *f, int pos, int redraw)
{
    int margin = f->width < 10 ? 0 : 4;
    int t = pos + margin < f->len ? pos + margin : f->len;
    if (f->width + f->scroll - 1 < t) {
        int scroll = f->len < pos + margin ? f->len - f->width : pos + margin - f->width;
        f->cur = pos;
        f->scroll = scroll + 1;
        field_redraw(u, f);
    } else if (!redraw) {
        f->cur = pos;
        field_cursor(u, f);
    } else {
        int from = pos < f->cur ? pos : f->cur;
        f->cur = pos;
        field_redraw_from(u, f, from);
    }
}

/* 0x15f91: work out the geometry; the cursor goes to the end of the text */
static void field_init(field *f)
{
    f->len = (int)strlen(f->buf);
    f->fx = (int)strlen(f->label) + f->x + (f->boxed ? 1 : 0);
    f->width = f->right - f->fx;
    f->cur = f->len;
    f->scroll = f->len - f->width > 0 ? f->len - f->width : 0;
}

/* 0x15e71 with its second argument 0: frame, label, text */
static void field_draw(ui *u, field *f)
{
    char tmp[82];
    int saved = u->attr, n = f->right - f->fx;
    if (f->boxed) frame(u, f->fx - 1, f->right, f->row, f->row + 2, f->a_box, 1);
    memset(tmp, ' ', (size_t)n);
    tmp[n] = 0;
    put_at(u, f->row + f->boxed, f->fx, tmp, f->a_fill);
    put_at(u, f->row + f->boxed, f->x, f->label, f->a_label); /* 0x15e2c */
    field_redraw(u, f);
    textattr(u, saved);
}

static int is_delimiter(char c) { return c != 0 && strchr(UI_WORD_DELIMITERS, c) != NULL; }

/* 0x1606a: one key for the field. 0 = not a key of mine. */
static int field_key(ui *u, field *f, int key)
{
    int si = f->cur;
    switch (key) {
    case 0:
        return 0;
    case K_LEFT:
        if (si != 0) field_left(u, f, si - 1, 0);
        break;
    case K_RIGHT:
        if (f->len != si) field_right(u, f, si + 1, 0);
        break;
    case K_CTRL_LEFT: /* to the start of the word before */
        if (si == 0) break;
        do si--;
        while (si != -1 && is_delimiter(f->buf[si]));
        if (si >= 0) {
            do si--;
            while (si != -1 && !is_delimiter(f->buf[si]));
        }
        field_left(u, f, si + 1, 0);
        break;
    case K_CTRL_RIGHT: /* to the start of the next word */
        if (f->len == si) break;
        while (f->len > si && !is_delimiter(f->buf[si])) si++;
        while (f->len > si && is_delimiter(f->buf[si])) si++;
        field_right(u, f, si, 0);
        break;
    case K_HOME:
        if (si != 0) field_left(u, f, 0, 0);
        break;
    case K_END:
        if (f->len != si) field_right(u, f, f->len, 0);
        break;
    case K_BS:
        if (si == 0) break;
        memmove(f->buf + si - 1, f->buf + si, (size_t)(f->len - si + 1));
        f->len--;
        field_left(u, f, si - 1, 1);
        break;
    case K_DEL:
        if (f->len == si) break;
        memmove(f->buf + si, f->buf + si + 1, (size_t)(f->len - si));
        f->len--;
        field_left(u, f, si, 1);
        break;
    case K_CLEAR:
        f->buf[0] = 0;
        f->len = f->cur = f->scroll = 0;
        field_redraw(u, f);
        break;
    case K_ENTER:
        return -1;
    default: /* a character: always inserted */
        if (key > 0xff || key < 0x20) return 0;
        if (f->allowed && !strchr(f->allowed, key)) return 0;
        if (f->len >= f->maxlen) break;
        memmove(f->buf + si + 1, f->buf + si, (size_t)(f->len - si + 1));
        f->buf[si] = (char)key;
        f->len++;
        field_right(u, f, si + 1, 1);
        break;
    }
    return 1;
}

/* ---- screens: segment 111c ---------------------------------------------------------------------- */

/* 0x1121b: a key bar, centred on a row. $ switches to bright, & to dim. */
static void key_bar(ui *u, const char *s, int row)
{
    const char *p;
    int n = 0;
    gotoxy(u, 1, row);
    textattr(u, A_BAR);
    clreol(u);
    for (p = s; *p; p++)
        if (*p != '$' && *p != '&') n++;
    gotoxy(u, 0x28 - (n >> 1), row);
    textattr(u, A_BAR);
    for (p = s; *p; p++) {
        if (*p == '$') textattr(u, A_BAR);
        else if (*p == '&') textattr(u, A_BAR_DIM);
        else putch(u, (unsigned char)*p);
    }
}

/* the original's banner with its serial number; its format is "... %-06lu ..." */
static void banner(char *buf, size_t n)
{
    const char *spec = strchr(UI_BANNER_FMT, '%'), *rest = spec ? strchr(spec, 'u') : NULL;
    if (!spec || !rest) {
        snprintf(buf, n, "%s", UI_BANNER_FMT);
        return;
    }
    snprintf(buf, n, "%.*s%-6lu%s", (int)(spec - UI_BANNER_FMT), UI_BANNER_FMT, UI_SERIAL, rest + 1);
}

/* 0x11bd5: the main key bar and the title row */
static void title(ui *u)
{
    char buf[82];
    key_bar(u, UI_BAR_MAIN, 0x19);
    textattr(u, A_BAR);
    gotoxy(u, 1, 1);
    clreol(u);
    banner(buf, sizeof buf);
    gotoxy(u, 0x28 - (int)(strlen(buf) >> 1), 1);
    cputs(u, buf);
}

/* 0x111cf: Esc */
static void quit(ui *u)
{
    char buf[82];
    textattr(u, 7);
    clrscr(u);
    banner(buf, sizeof buf);
    cputs(u, buf);
    gotoxy(u, 1, 0x18);
    speak(u, UI_BYE);
    ui_end(u, "exit", 0);
}

/* 0x1203a: a message in a box, read aloud, until Enter. Space repeats it. */
static void error_box(ui *u, int error)
{
    const char *msg = error == 5 ? UI_ERR_PROTECTED : error == 8 ? UI_ERR_MEMORY : UI_ERR_NOT_FOUND;
    cell saved[25][80];
    int sx = u->cx, sy = u->cy, sattr = u->attr, i, key, cur_x = 1, cur_y = 1;

    memcpy(saved, u->scr, sizeof saved);
    frame(u, 5, 0x4b, 10, 0xe, 0x70, 1);
    textattr(u, 0x70);
    for (i = 0; i < 3; i++) {
        char line[0x46];
        const char *text = i == 0 ? msg : i == 2 ? UI_PRESS_ENTER : NULL;
        memset(line, ' ', 0x45);
        line[0x45] = 0;
        if (text) {
            int len = (int)strlen(text);
            memcpy(line + 0x23 - (len >> 1), text, (size_t)len);
            if (i == 2) {
                cur_x = 0x29 - (len >> 1);
                cur_y = 0xd;
            }
        }
        gotoxy(u, 6, i + 0xb);
        cputs(u, line);
    }
    gotoxy(u, cur_x, cur_y);
    key = ' ';
    do {
        if (key == ' ') {
            speak(u, msg);
            if (!kbhit(u)) speak(u, UI_PRESS_ENTER);
        }
        key = getkey(u);
    } while (key != K_ENTER);
    memcpy(u->scr, saved, sizeof saved);
    gotoxy(u, sx, sy);
    textattr(u, sattr);
}

static const char *voice_name(ui *u)
{
    int v = *u->host->voice;
    return v >= 0 && v < 3 ? UI_VOICE_NAME[v] : u->host->voice_name(u->host->ctx);
}

/* 0x112c6: one row of the settings screen, optionally read aloud; the cursor ends on its value */
static void draw_row(ui *u, int idx, int value, int aloud)
{
    char line[0x48], spoken[200];
    const char *label = UI_ROWS[idx].label, *name = UI_ROWS[idx].spoken ? UI_ROWS[idx].spoken : label;
    int len = 0, off = 1;

    gotoxy(u, 5, idx + 9);
    memset(line, ' ', 0x47);
    line[0x47] = 0;
    spoken[0] = 0;
    switch (UI_ROWS[idx].type) {
    case 1:
        len = sprintf(line + 1, "%s %s", value ? UI_ON : UI_OFF, label);
        snprintf(spoken, sizeof spoken, "%s: %s", name, value ? UI_ON : UI_OFF);
        break;
    case 2:
        len = sprintf(line + 1, "%s %d", label, value);
        snprintf(spoken, sizeof spoken, "%s: %d", name, value);
        off = (int)strlen(label) + 2;
        break;
    case 4:
        len = sprintf(line + 1, "%c  %s", value ? 'K' : 'S', label);
        snprintf(spoken, sizeof spoken, "%s: %s", name, value ? UI_C_AS_K : UI_C_AS_S);
        break;
    case 5:
        len = snprintf(line + 1, 0x46, "%s %s", label, voice_name(u));
        snprintf(spoken, sizeof spoken, "%s: %s", name, voice_name(u));
        off = (int)strlen(label) + 2;
        break;
    }
    line[len + 1] = ' ';
    cputs(u, line);
    if (aloud) speak(u, spoken);
    gotoxy(u, off + 5, idx + 9);
}

/* 0x11684: another voice was chosen: load it and show its settings */
static void voice_changed(ui *u, int old)
{
    int i;
    u->host->select_voice(u->host->ctx, old);
    textattr(u, A_TEXT);
    for (i = 0; i < 12; i++)
        if (UI_ROWS[i].type != 3 && UI_ROWS[i].type != 5) draw_row(u, i, u->host->set[UI_ROWS[i].word], 0);
    textattr(u, A_SELECTED);
}

/* 0x11713: stay on one row until the selection moves or Enter ends the screen. Returns the next
 * row, or -1. */
static int settings_row(ui *u, int si)
{
    const mp_ui_host *h = u->host;
    int16_t *val = &h->set[UI_ROWS[si].word];
    int di = 0, stay = 1, type = UI_ROWS[si].type;

    textattr(u, A_SELECTED);
    draw_row(u, si, *val, 1);
    do {
        const char *msg = NULL;
        char num[16];
        int key = 0, changed = 0;
        while (kbhit(u) || key == 0) key = getkey(u); /* of several keys typed ahead the last one counts */
        switch (key) {
        case K_F1:
            speak(u, UI_HELP_SETTINGS);
            while (kbhit(u)) getkey(u);
            break;
        case K_F2: {
            int err = h->save(h->ctx);
            if (err == 0) {
                speak(u, UI_SAVED);
            } else {
                error_box(u, err);
                draw_row(u, si, *val, 1);
            }
            break;
        }
        case K_UP:
            if (si == 0) speak(u, UI_FIRST);
            else di = -1;
            break;
        case K_DOWN:
            if (si == 11) speak(u, UI_LAST);
            else di = 1;
            break;
        case '+':
            changed = 1;
            if (type == 1) *val = 1, msg = UI_ON;
            else if (type == 2) {
                if (++*val == 9) *val = 8;
            } else if (type == 4) *val = 1, msg = UI_C_AS_K;
            else if (type == 5) {
                if (*h->voice < 2) {
                    int old = (*h->voice)++;
                    voice_changed(u, old);
                }
                msg = voice_name(u);
            }
            break;
        case '-':
            changed = 1;
            if (type == 1) *val = 0, msg = UI_OFF;
            else if (type == 2) {
                if (--*val == -1) *val = 0;
            } else if (type == 4) *val = 0, msg = UI_C_AS_S;
            else if (type == 5) {
                if (*h->voice > 0 && *h->voice < 3) {
                    int old = (*h->voice)--;
                    voice_changed(u, old);
                }
                msg = voice_name(u);
            }
            break;
        case ' ':
            draw_row(u, si, *val, 1);
            break;
        case K_ESC:
            quit(u);
            break;
        case K_ENTER:
            speak(u, UI_OK);
            stay = 0;
            break;
        }
        if (changed) { /* 0x1197b */
            if (type == 2) {
                snprintf(num, sizeof num, "%d", *val);
                msg = num;
            }
            h->apply(h->ctx);
            draw_row(u, si, *val, 0);
            if (msg) speak(u, msg);
        }
    } while (stay && di == 0);
    textattr(u, A_TEXT);
    draw_row(u, si, *val, 0);
    if (di != 0) do si += di;
        while (UI_ROWS[si].type == 3);
    return stay ? si : -1;
}

/* 0x11a26: the settings screen (F2) */
static void settings(ui *u)
{
    static int current; /* DS:0x03ad: the row is remembered between visits */
    int i, next;
    key_bar(u, UI_BAR_SETTINGS, 0x19);
    textattr(u, A_TEXT);
    for (i = 0; i < 12; i++)
        if (UI_ROWS[i].type != 3) draw_row(u, i, u->host->set[UI_ROWS[i].word], 0);
    for (next = current; next != -1; next = settings_row(u, current)) current = next;
    textattr(u, A_TEXT);
    for (i = 9; i != 0x15; i++) {
        gotoxy(u, 1, i);
        clreol(u);
    }
}

/* 0x11c57: the main screen */
static void run(ui *u)
{
    static char history[10][0x100];
    char line[0x100];
    field f;
    int count = 0, cur = 0; /* lines in the history, and which one the arrow keys are on */

    textattr(u, A_TEXT);
    clrscr(u);
    title(u);
    memset(&f, 0, sizeof f);
    f.label = UI_PROMPT;
    f.buf = line;
    f.maxlen = 0xff;
    f.x = 6;
    f.right = 0x4b;
    f.row = 4;
    f.boxed = 1;
    f.a_label = 0x0f; /* 0x11b7f, colour screen */
    f.a_text = 0x0f;
    f.a_fill = 0x1f;
    f.a_box = 0x0f;
    line[0] = 0;
    field_init(&f);
    field_draw(u, &f);
    speak(u, UI_WELCOME);
    while (kbhit(u)) getkey(u);
    for (;;) {
        int key = getkey(u), load = 0, clear = 0;
        if (field_key(u, &f, key) != 0) {
            if (key != K_ENTER || line[0] == 0) continue;
            if (count == 10) { /* the oldest line drops out */
                memmove(history[0], history[1], 0x900);
                count--;
            }
            memcpy(history[count], line, 0x100);
            count++;
            cur = count;
            speak(u, line);
            clear = 1;
        } else if (key == K_ESC) {
            quit(u);
        } else if (key == K_F1) {
            speak(u, UI_HELP_MAIN);
            while (kbhit(u)) getkey(u);
        } else if (key == K_F2) {
            int sx = u->cx, sy = u->cy;
            settings(u);
            title(u);
            gotoxy(u, sx, sy);
        } else if (key == K_UP) {
            if (cur != 0) {
                cur--;
                load = 1;
            }
        } else if (key == K_DOWN) {
            if (cur + 1 < count) {
                cur++;
                load = 1;
            } else if (line[0] != 0) {
                clear = 1;
            }
        }
        if (load) memcpy(line, history[cur], 0x100);
        if (clear) line[0] = 0;
        if (load || clear) {
            field_init(&f);
            field_draw(u, &f);
        }
    }
}

int mp_ui_script(const mp_ui_host *host, const int *keys, int nkeys)
{
    static ui u;
    u.host = host;
    u.keys = keys;
    u.nkeys = nkeys;
    u.script = 1;
    run(&u);
    return 0;
}

int mp_ui_run(const mp_ui_host *host)
{
    static ui u;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    int cols, rows;
    u.host = host;
    u.con_in = GetStdHandle(STD_INPUT_HANDLE);
    u.con_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(u.con_in, &u.saved_in) || !GetConsoleMode(u.con_out, &u.saved_out) ||
        !GetConsoleScreenBufferInfo(u.con_out, &info)) {
        fputs("puhu: the full-screen interface needs a console window (Windows Terminal, PowerShell or cmd).\n"
              "      Use 'puhu --lines' to speak typed lines without it.\n", stderr);
        return 1;
    }
    cols = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (cols < 80 || rows < 25) {
        fprintf(stderr, "puhu: the full-screen interface needs a window of at least 80 x 25; this one is %d x %d.\n"
                        "      Make the window larger, or use 'puhu --lines' to speak typed lines without it.\n",
                cols, rows);
        return 1;
    }
    /* the screen is drawn with the same escape sequences as on a terminal: Windows 10 and later */
    if (!SetConsoleMode(u.con_out, u.saved_out | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        fputs("puhu: this console cannot show the full-screen interface (it needs Windows 10 or later).\n"
              "      Use 'puhu --lines' to speak typed lines without it.\n", stderr);
        return 1;
    }
    /* every key at once and unchanged, Ctrl-C included, as key events with the keyboard's own codes */
    SetConsoleMode(u.con_in, u.saved_in & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT |
                                                 ENABLE_MOUSE_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT));
    u.saved_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IOFBF, 1 << 16); /* a screenful in one write: no character is cut in two */
    u.tty_raw = 1;
#else
    struct termios raw;
    struct winsize ws;
    u.host = host;
    /* the original refused to run in a screen mode other than 80 columns; so does this */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col != 0 && (ws.ws_col < 80 || ws.ws_row < 25)) {
        fprintf(stderr, "puhu: the full-screen interface needs a terminal of at least 80 x 25; this one is %d x %d.\n"
                        "      Make the window larger, or use 'puhu --lines' to speak typed lines without it.\n",
                ws.ws_col, ws.ws_row);
        return 1;
    }
    if (tcgetattr(STDIN_FILENO, &u.saved_tty) == 0) {
        raw = u.saved_tty;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        u.tty_raw = 1;
    }
#endif
    fputs("\033[?1049h\033[2J", stdout); /* a screen of our own, like the DOS program had */
    run(&u);
    return 0;
}
