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

#ifndef LCURSES_H
#define LCURSES_H

/* Number of spaces used to display a tab (must be at least */
#define TABSIZE 4


#ifdef _WIN32
#define GETCH() _getch()
#else
#define GETCH() getchar()
#endif

#define MOVE_CURSOR(g, y, x) g->v = (y) * g->w + (x)

#define GET_CURSOR(g, y, x) do { \
    y = g->v / g->w; \
    x = g->v % g->w; \
} while (0)

#define GET_MAX(g, y, x) do { \
    y = g->h; \
    x = g->w; \
} while (0)

#define PRINT_CH(g, ch, ret) do { \
    if (g->v < g->sa) { \
        if (isgraph((unsigned char) ch) || ch == ' ') { \
            *(g->ns + g->v++) = ch; \
        } else if (ch == '\n') { \
            *(g->ns + g->v++) = ' '; \
            if (g->v % g->w) \
                g->v = (g->v / g->w + 1) * g->w; \
        } else if (ch == '\t') { \
            memset(g->ns + g->v, ' ', TABSIZE); \
            g->v += TABSIZE; \
        } else if (ch == '\0') { \
            *(g->ns + g->v++) = '\\'; \
            *(g->ns + g->v++) = '0'; \
        } else { \
            *(g->ns + g->v++) = (unsigned char) ch / 16 < 10 \
                ? (unsigned char) ch / 16 + '0' \
                : (unsigned char) ch / 16 + 'A'; \
            *(g->ns + g->v++) = (unsigned char) ch % 16 < 10 \
                ? (unsigned char) ch % 16 + '0' \
                : (unsigned char) ch % 16 + 'A'; \
        } \
        ret = 0; \
    } else { \
        ret = 1; \
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

struct graph *init_graphics(void);
int close_graphics(struct graph *g);
int vmemprint(struct graph *g, size_t screen_limit, char *p,
              size_t p_size);
int clear_screen(struct graph *g, int hard);
void refresh_screen(struct graph *g);

#endif
