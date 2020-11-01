/*
 * Copyright (c) 2020 Logan Ryan McLintock
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * lcurses: A minimalistic double buffering curses module.
 */

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <sys/ioctl.h>
/* #include <sys/wait.h> */
#include <termios.h>
#include <unistd.h>
#endif

/*
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Number of spaces used to display a tab */
#define TABSIZE 4

/* ANSI escape sequences */
#define CLEAR_SCREEN() printf("\033[2J")
#define CLEAR_LINE() printf("\033[2K")
#define MOVE_CURSOR(y, x) printf("\033[%lu;%luH", (unsigned long) (y), \
    (unsigned long) (x))

#ifdef _WIN32
#define GETCH() _getch()
#else
#define GETCH() getchar()
#endif

/* size_t integer overflow tests */
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))


#define VPUTCH(ch) do { \
    uch = (unsigned char) ch; \
    if (isgraph(uch) || uch == ' ') { \
        *(ns + v++) = uch; \
    } else if (uch == '\n') { \
        do { \
            *(ns + v++) = ' '; \
        } while (v % w); \
    } else if (uch == '\t') { \
        for (i = 0; i < TABSIZE; ++i) \
            *(ns + v++) = ' '; \
    } else if (uch >= 1 && uch <= 26) { \
        *(ns + v++) = '^'; \
        *(ns + v++) = 'A' + uch - 1; \
    } else if (!uch) { \
        *(ns + v++) = '\\'; \
        *(ns + v++) = '0'; \
    } else { \
        *(ns + v++) = uch / 16 >= 10 ? uch / 16 - 10 + 'A' : uch / 16 + '0'; \
        *(ns + v++) = uch % 16 >= 10 ? uch % 16 - 10 + 'A' : uch % 16 + '0'; \
    } \
} while (0)

struct graph {
    char *ns;                   /* Next screen (virtual) */
    char *cs;                   /* Current screen (virtual) */
    size_t vms;                 /* Virtual memory size */
    size_t h;                   /* Screen height (real) */
    size_t w;                   /* Screen width (real) */
    size_t sa;                  /* Screen area (real) */
    size_t v;                   /* Virtual index */
#ifndef _WIN32
    struct termios t_orig;      /* Original terminal attributes */
#endif
};

int get_screen_size(size_t * height, size_t * width)
{
    /* Gets the screen size */
#ifdef _WIN32
    HANDLE out;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE)
        return 1;
    if (!GetConsoleScreenBufferInfo(out, &info))
        return 1;
    *height = info.srWindow.Bottom - info.srWindow.Top + 1;
    *width = info.srWindow.Right - info.srWindow.Left + 1;
    return 0;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return 1;
    *height = ws.ws_row;
    *width = ws.ws_col;
    return 0;
#endif
}

int clear_screen(struct graph *g, int hard)
{
    size_t new_h, new_w;
    char *tmp_ns, *tmp_cs;
    if (get_screen_size(&new_h, &new_w)) {
        return 1;
    }
    /* Clear hard or change in screen dimensions */
    if (hard || new_h != g->h || new_w != g->w) {
        g->h = new_h;
        g->w = new_w;
        if (MOF(g->h, g->w))
            return 1;
        g->sa = g->h * g->w;
        /* Bigger screen */
        if (g->vms < g->sa) {
            if ((tmp_ns = calloc(g->sa, 1)) == NULL) {
                return 1;
            }
            if ((tmp_cs = calloc(g->sa, 1)) == NULL) {
                free(tmp_ns);
                return 1;
            }
            free(g->ns);
            g->ns = tmp_ns;
            free(g->cs);
            g->cs = tmp_cs;
            g->vms = g->sa;
            /* Clear the physical screen */
            CLEAR_SCREEN();
            return 0;
        }
        /*
         * Clear the virtual current screen. No need to erase the
         * virtual screen beyond the physical screen size.
         */
        memset(g->cs, ' ', g->sa);
        CLEAR_SCREEN();
    }
    /* Clear the virtual next screen */
    memset(g->ns, ' ', g->sa);
    return 0;
}

int close_graphics(struct graph *g)
{
    int ret = 0;
    CLEAR_SCREEN();
#ifndef _WIN32
    if (tcsetattr(STDIN_FILENO, TCSANOW, &g->t_orig))
        ret = 1;
#endif
    if (g != NULL) {
        free(g->ns);
        free(g->cs);
        free(g);
    }
    return ret;
}

struct graph *init_graphics(void)
{
    struct graph *g;
#ifdef _WIN32
    HANDLE out;
    DWORD mode;
#else
    struct termios term_orig, term_new;
#endif
#ifdef _WIN32
    /* Check input is from a terminal */
    if (!_isatty(_fileno(stdin)))
        return NULL;
    /* Turn on interpretation of VT100-like escape sequences */
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE)
        return NULL;
    if (!GetConsoleMode(out, &mode))
        return NULL;
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return NULL;
#else
    if (!isatty(STDIN_FILENO))
        return NULL;
    /* Change terminal input to raw and no echo */
    if (tcgetattr(STDIN_FILENO, &term_orig))
        return NULL;
    term_new = term_orig;
    cfmakeraw(&term_new);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term_new))
        return NULL;
#endif
    if ((g = malloc(sizeof(struct graph))) == NULL)
        return NULL;
    g->ns = NULL;
    g->cs = NULL;
    g->vms = 0;
    g->h = 0;
    g->w = 0;
    g->sa = 0;
    g->v = 0;
#ifndef _WIN32
    g->t_orig = term_orig;
#endif
    if (clear_screen(g, 1)) {
        close_graphics(g);
        return NULL;
    }
    return g;
}

void diff_draw(struct graph *g)
{
    /* Physically draw the screen where the virtual screens differ */
    int in_pos = 0;             /* In position for printing */
    char ch;
    for (g->v = 0; g->v < g->sa; ++g->v) {
        if ((ch = *(g->ns + g->v)) != *(g->cs + g->v)) {
            if (!in_pos) {
                /* Top left corner is (1, 1) not (0, 0) so need to add one */
                MOVE_CURSOR(g->v / g->w + 1,
                            g->v - (g->v / g->w) * g->w + 1);
                in_pos = 1;
            }
            putchar(ch);
        } else {
            in_pos = 0;
        }
    }
}

void refresh_screen(struct graph *g)
{
    char *t;
    diff_draw(g);

    /* Swap virtual screens */
    t = g->cs;
    g->cs = g->ns;
    g->ns = t;
}

/*
        -- Do graphics only if screen is big enough --
        if (new_h >= 1 && new_w >= 1) {
            draw_screen(*(z->z + z->a), cl, cla, cr, h, w, ns, &cy, &cx,
                        centre);
            -- Clear centre request --
            centre = 0;
            -- Top left corner is (1, 1) not (0, 0) so need to add one --
            MOVE_CURSOR(cy + 1, cx + 1);
*/
