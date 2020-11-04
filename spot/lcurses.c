
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
#include <io.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcurses.h"

#ifndef ST_OVERFLOW_TESTS
#define ST_OVERFLOW_TESTS
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))
#endif

/* ANSI escape sequences */
#define PHY_CLEAR_SCREEN() printf("\033[2J")
/* Index starts at one. Top left is (1, 1) */
#define PHY_MOVE_CURSOR(y, x) printf("\033[%lu;%luH", (unsigned long) (y), \
    (unsigned long) (x))

static int get_screen_size(size_t * height, size_t * width)
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
    size_t new_h, new_w, req_vms;
    char *tmp_ns, *tmp_cs;
    if (get_screen_size(&new_h, &new_w)) {
        return 1;
    }

    /* Reset virtual index */
    g->v = 0;

    /* Clear hard or change in screen dimensions */
    if (hard || new_h != g->h || new_w != g->w) {
        g->h = new_h;
        g->w = new_w;
        if (MOF(g->h, g->w))
            return 1;
        g->sa = g->h * g->w;
        /*
         * Add TABSIZE to the end of the virtual screen to
         * allow for characters to be printed off the screen.
         * Assumes that tab consumes the most screen space
         * out of all the characters.
         */
        if (AOF(g->sa, TABSIZE))
            return 1;
        req_vms = g->sa + TABSIZE;
        /* Bigger screen */
        if (g->vms < req_vms) {
            if ((tmp_ns = malloc(req_vms)) == NULL) {
                return 1;
            }
            if ((tmp_cs = malloc(req_vms)) == NULL) {
                free(tmp_ns);
                return 1;
            }
            free(g->ns);
            g->ns = tmp_ns;
            free(g->cs);
            g->cs = tmp_cs;
            g->vms = req_vms;
        }
        /*
         * Clear the virtual current screen. No need to erase the
         * virtual screen beyond the physical screen size.
         */
        memset(g->cs, ' ', g->sa);
        PHY_CLEAR_SCREEN();
    }
    /* Clear the virtual next screen */
    memset(g->ns, ' ', g->sa);
    return 0;
}

int close_graphics(struct graph *g)
{
    int ret = 0;
    PHY_CLEAR_SCREEN();
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

static void diff_draw(struct graph *g)
{
    /* Physically draw the screen where the virtual screens differ */
    int in_pos = 0;             /* In position for printing */
    char ch;
    size_t i;
    for (i = 0; i < g->sa; ++i) {
        if ((ch = *(g->ns + i)) != *(g->cs + i)) {
            if (!in_pos) {
                /* Top left corner is (1, 1) not (0, 0) so need to add one */
                PHY_MOVE_CURSOR(i / g->w + 1, i % g->w + 1);
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
    /* Set physical cursor to the position of the virtual cursor */
    if (g->v < g->sa)
        PHY_MOVE_CURSOR(g->v / g->w + 1, g->v % g->w + 1);
    else
        PHY_MOVE_CURSOR(g->h, g->w);
    /* Swap virtual screens */
    t = g->cs;
    g->cs = g->ns;
    g->ns = t;
}
