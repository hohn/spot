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
 * spot: A minimalistic and fast text editor.
 * Dedicated to my son who was only a 4mm `spot' in his first ultrasound.
 */

#ifdef __linux__
#define _XOPEN_SOURCE 500
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lbuf.h"
#include "lcurses.h"

/* Convert lowercase letter to control character */
#define C(c) ((c) - 'a' + 1)
#define ESC 27

/*
 * KEY BINDINGS
 */
#define SETMARK 0
/* Command multiplier */
#define CMDMULT C('u')
/* Previous */
#define UP C('p')
/* Next */
#define DOWN C('n')
/* Backwards */
#define LEFT C('b')
/* Forwards */
#define RIGHT C('f')
/* Antes */
#define HOME C('a')
#define ENDLINE C('e')
#define DEL C('d')
#define BKSPACE C('h')
/* Wipe */
#define CUT C('w')
/* Yank */
#define PASTE C('y')
/* Reverse kill */
#define CUTTOSOL C('r')
/* Kill */
#define CUTTOEOL C('k')
#define SEARCH C('s')
/* Level cursor on screen */
#define CENTRE C('l')
/* Deactivates the mark or exits the command line */
#define CMDEXIT C('g')
#define TRIMCLEAN C('t')
/* Quote hex */
#define INSERTHEX C('q')

/*
 * Cx prefix
 */
#define SAVE C('s')
#define INSERTFILE 'i'
#define RENAME C('w')
/* Open file */
#define NEWBUF C('f')
/* Close without prompting to save */
#define CLOSE C('c')
/* Left arrow key moves left one buffer */
/* Right arrow key moves right one buffer */

/*
 * ESC prefix
 */
#define COPY 'w'
#define REPSEARCH 'n'
#define REDRAW 'L'
#define STARTBUF '<'
#define ENDBUF '>'
#define MATCHBRACE 'm'

/* Default number of spare text buffer pointers. Must be at least 1 */
#define SPARETB 10


/* Structure to keep track of the text buffers */
struct tb {
    struct buffer **z;          /* Text buffers */
    size_t u;                   /* Used number of text buffers */
    size_t n;                   /* Total number of text buffers */
    size_t a;                   /* The index of the active text buffer */
};

int insert_hex(struct buffer *b, size_t mult)
{
    /* Inserts a two digit hex char from the keyboard */
    unsigned char hex[2];       /* Hexadecimal array */
    int key;
    size_t i;
    for (i = 0; i < 2; ++i) {
        key = GETCH();
        if (ISASCII((unsigned int) key)
            && isxdigit((unsigned char) key)) {
            if (isdigit((unsigned char) key))
                *(hex + i) = key - '0';
            else if (islower((unsigned char) key))
                *(hex + i) = 10 + key - 'a';
            else if (isupper((unsigned char) key))
                *(hex + i) = 10 + key - 'A';
        } else {
            return 1;
        }
    }
    return insert_char(b, *hex * 16 + *(hex + 1), mult);
}

struct tb *init_tb(size_t req)
{
    /* Initialises structure to keep track of the text buffers */
    struct tb *z;
    size_t n, s;
    if (AOF(req, SPARETB))
        return NULL;
    n = req + SPARETB;
    if (MOF(n, sizeof(struct buffer *)))
        return NULL;
    s = n * sizeof(struct buffer *);
    if ((z = malloc(sizeof(struct tb))) == NULL)
        return NULL;
    if ((z->z = malloc(s)) == NULL) {
        free(z);
        return NULL;
    }
    z->n = n;
    z->u = 0;
    z->a = 0;
    return z;
}

void free_tb(struct tb *z)
{
    size_t i;
    if (z != NULL) {
        for (i = 0; i < z->u; ++i)
            free_buffer(*(z->z + i));
        free(z->z);
        free(z);
    }
}

int new_buffer(struct tb *z, char *fn)
{
    struct stat st;
    size_t new_n, new_s;
    struct buffer **t;
    struct buffer *b;           /* Buffer shortcut */
    /* Grow to take more text buffers */
    if (z->u == z->n) {
        if (AOF(z->u, SPARETB))
            return 1;
        new_n = z->u + SPARETB;
        if (MOF(new_n, sizeof(struct buffer *)))
            return 1;
        new_s = new_n * sizeof(struct buffer *);
        if ((t = realloc(z->z, new_s)) == NULL)
            return 1;
        z->z = t;
        z->n = new_n;
    }
    b = *(z->z + z->u);         /* Create shortcut */
    if (fn != NULL && !stat(fn, &st)) {
        /* File exists */
        if (!((st.st_mode & S_IFMT) == S_IFREG))
            return 1;
        if (st.st_size < 0)
            return 1;
        if ((b = init_buffer((size_t) st.st_size)) == NULL)
            return 1;
        if (rename_buffer(b, fn)) {
            free_buffer(b);
            return 1;
        }
        if (insert_file(b, fn)) {
            free_buffer(b);
            return 1;
        }
    } else {
        /* New file */
        if ((b = init_buffer(0)) == NULL)
            return 1;
        if (fn != NULL && rename_buffer(b, fn)) {
            free_buffer(b);
            return 1;
        }
    }
    /* Clear mod indicator */
    b->mod = 0;

    /* Success */
    *(z->z + z->u) = b;         /* Link back */
    z->a = z->u;                /* Set active text buffer to the new one */
    ++z->u;                     /* Increase the number of used text buffers */
    return 0;
}

int draw_screen(struct graph *g, struct buffer *b, struct mem *sb,
                struct buffer *cl, int cla, int cr, int centre, int hard)
{
    /* Virtually draw screen (and clear unused sections on the go) */
    char *q, ch;                /* Generic pointer and value */
    char *t;                    /* Status bar temporary pointer */
    size_t s;                   /* Status bar temporary size */
    size_t h, w;                /* Screen height and width */
    size_t y, x;                /* Transient coordinates */
    size_t cy, cx;              /* Final coordinates of the cursor */
    /* Height of text buffer portion of the screen */
    size_t th;                  /* Text portion screen height */
    size_t ci = b->g - b->a;    /* Cursor index */
    size_t ta;                  /* Text screen area */
    size_t hth;                 /* Half the text screen height */
    size_t hta;                 /* Half the text screen area */
    size_t row_up;              /* Number of rows to reverse scan for */
    size_t cap;                 /* Reverse scan cutoff */
    size_t i;                   /* Filename character index */
    int r;                      /* Return value of graphics macro functions */

    if (clear_screen(g, hard))
        return 1;
    hard = 0;
    GET_MAX(g, h, w);

    /* Do graphics only if screen is big enough */
    if (!h || !w)
        return 1;

    th = h > 2 ? h - 2 : 1;
    ta = th * w;
    hth = th > 2 ? th / 2 : 1;
    hta = hth * w;

    /*
     * Text buffer:
     */

  text_draw:

    /* Request to centre or cursor is definitely off the screen */
    if (centre || ci < b->d || ci - b->d >= ta) {
        q = b->g;
        row_up = hth + 1;
        cap = hta;
        while (q != b->a && row_up && cap) {
            --cap;
            if (*--q == '\n')
                --row_up;
        }
        if (q != b->a)
            ++q;
        b->d = q - b->a;
    }

    /* Print before the gap */
    q = b->a + b->d;
    r = 0;
    while (!r && q != b->g) {
        ch = *q++;
        PRINT_CH(g, ch, r);
    }

    /* Record cursor's screen location */
    GET_CURSOR(g, cy, cx);

    /* Cursor is outside of text portion of screen */
    if (cy >= th) {
        MOVE_CURSOR(g, 0, 0, r);
        CLEAR_DOWN(g, r);
        /* Draw from cursor if centreing has already been tried */
        if (centre)
            b->d = b->g - b->a;
        else
            centre = 1;         /* Try centreing */
        goto text_draw;
    }

    /* Print after the gap */
    q = b->c;
    r = 0;
    while (!r && q <= b->e) {
        ch = *q++;
        PRINT_CH(g, ch, r);
    }

    /* Stop if screen is only one row high */
    if (h == 1)
        return 0;

    /*
     * Status bar:
     */

    /* Move to status bar */
    r = 0;
    MOVE_CURSOR(g, th, 0, r);
    if (r)
        return 1;
    CLEAR_DOWN(g, r);

    /* Check that memory allocation is large enough */
    if (AOF(w, 1))
        return 1;
    s = w + 1;
    if (s > sb->s) {
        if ((t = malloc(s)) == NULL)
            return 1;
        free(sb->p);
        sb->p = t;
        sb->s = s;
    }

    if (snprintf(sb->p, sb->s,
                 "%c%c %s (%lu, %lu)%c",
                 cr ? '!' : ' ', b->mod ? '*' : ' ', b->fn, b->r, b->col,
                 b->m_set ? 'm' : ' ') < 0)
        return 1;

    /* Print status bar */
    for (i = 0; i < w; ++i) {
        ch = *(sb->p + i);
        if (ch == '\0')
            break;
        PRINT_CH(g, ch, r);
        GET_CURSOR(g, y, x);
        if (y != th)
            break;
    }

    /* Stop if screen is only two rows high */
    if (h == 2)
        return 0;

    /*
     * Command line buffer:
     */

  cl_draw:

    /* Move to command line */
    r = 0;
    MOVE_CURSOR(g, th + 1, 0, r);
    if (r)
        return 1;
    CLEAR_DOWN(g, r);

    /* Print before the gap */
    q = cl->a + cl->d;
    r = 0;
    while (!r && q != cl->g) {
        ch = *q++;
        PRINT_CH(g, ch, r);
    }
    /* Cursor is off the screen */
    if (r) {
        cl->d = cl->g - cl->a;  /* Draw from cursor */
        goto cl_draw;
    }

    /* If the command line is active, record cursor's screen location */
    if (cla)
        GET_CURSOR(g, cy, cx);

    /* Print after the gap */
    q = cl->c;
    r = 0;
    while (!r && q <= cl->e) {
        ch = *q++;
        PRINT_CH(g, ch, r);
    }

    /* Position the cursor in its final location */
    r = 0;
    MOVE_CURSOR(g, cy, cx, r);
    if (r)
        return 1;

    return 0;
}

int main(int argc, char **argv)
{
    int ret = 0;                /* Editor's return value */
    int running = 1;            /* Editor is running */
    struct graph *g;            /* Used for lcurses */
    struct tb *z = NULL;        /* The text buffers */
    struct buffer *cl = NULL;   /* Command line buffer */
    int cla = 0;                /* Command line buffer is active */
    /* Operation for which the command line is being used */
    char operation = '\0', operation_copy = '\0';
    char *cl_str = NULL;        /* Command line buffer converted to a string */
    /* Bad character table for the Quick Search algorithm */
    size_t bad[UCHAR_MAX + 1];
    struct mem *se = NULL;      /* Search memory */
    struct mem *p = NULL;       /* Paste memory */
    struct mem *sb = NULL;      /* Status bar */
    int cr = 0;                 /* Command return value */
    int centre = 0;             /* Request to centre the cursor */
    int hard = 0;               /* Request to clear hard the entire screen */
    struct buffer *cb;          /* Shortcut to the cursor's buffer */
    /* Keyboard key (one physical key can send multiple) */
    int key;
    int digit;                  /* Numerical digit */
    size_t mult;                /* Command multiplier (cannot be zero) */
    size_t i;                   /* Generic index */

    if ((g = init_graphics()) == NULL)
        return 1;

    /* Process command line arguments */
    if (argc > 1) {
        if ((z = init_tb(argc - 1)) == NULL) {
            ret = 1;
            goto clean_up;
        }
        /* Load files into buffers */
        for (i = 0; i < (size_t) (argc - 1); ++i)
            if (new_buffer(z, *(argv + i + 1))) {
                ret = 1;
                goto clean_up;
            }
        /* Go back to first buffer */
        z->a = 0;
    } else {
        if ((z = init_tb(1)) == NULL) {
            ret = 1;
            goto clean_up;
        }
        /* Start empty buffer */
        if (new_buffer(z, NULL)) {
            ret = 1;
            goto clean_up;
        }
    }

    /* Initialise command line buffer */
    if ((cl = init_buffer(0)) == NULL) {
        ret = 1;
        goto clean_up;
    }
    /* Initialise search memory */
    if ((se = init_mem()) == NULL) {
        ret = 1;
        goto clean_up;
    }
    /* Initialise paste memory */
    if ((p = init_mem()) == NULL) {
        ret = 1;
        goto clean_up;
    }

    /* Initialise status bar memory */
    if ((sb = init_mem()) == NULL) {
        ret = 1;
        goto clean_up;
    }

    /* Editor loop */
    while (running) {

        /* Top of the editor loop */
      top:

        draw_screen(g, *(z->z + z->a), sb, cl, cla, cr, centre, hard);
        /* Clear centre request */
        centre = 0;
        /* Deactivate clear hard */
        hard = 0;

        refresh_screen(g);

        /* Reset command return value */
        cr = 0;

        /* Shortcut to the cursor's buffer */
        cb = cla ? cl : *(z->z + z->a);

        /* Reset command multiplier */
        mult = 1;
        if ((key = GETCH()) == CMDMULT) {
            /* Read multiplier number */
            mult = 0;
            key = GETCH();
            while (isdigit(key)) {
                if (MOF(mult, 10)) {
                    cr = 1;
                    goto top;
                }
                mult *= 10;
                digit = key - '0';
                if (AOF(mult, digit)) {
                    cr = 1;
                    goto top;
                }
                mult += digit;
                key = GETCH();
            }
        }
        /* mult cannot be zero */
        if (!mult)
            mult = 1;

        /* Remap special keyboard keys */
#ifdef _WIN32
        if (key == 0xE0) {
            key = GETCH();
            switch (key) {
            case 'H':
                key = UP;
                break;
            case 'P':
                key = DOWN;
                break;
            case 'K':
                key = LEFT;
                break;
            case 'M':
                key = RIGHT;
                break;
            case 'S':
                key = DEL;
                break;
            case 'G':
                key = HOME;
                break;
            case 'O':
                key = ENDLINE;
                break;
            }
        }
#endif

        if (key == ESC) {
            key = GETCH();
            switch (key) {
            case STARTBUF:
                start_of_buffer(cb);
                goto top;
            case ENDBUF:
                end_of_buffer(cb);
                goto top;
            case REPSEARCH:
                cr = search(cb, se, bad);
                goto top;
            case MATCHBRACE:
                cr = match_brace(cb);
                goto top;
            case COPY:
                cr = copy_region(cb, p, 0);
                goto top;
            case REDRAW:
                hard = 1;
                goto top;
#ifndef _WIN32
            case '[':
                key = GETCH();
                switch (key) {
                case 'A':
                    key = UP;
                    break;
                case 'B':
                    key = DOWN;
                    break;
                case 'D':
                    key = LEFT;
                    break;
                case 'C':
                    key = RIGHT;
                    break;
                case '3':
                    if ((key = GETCH()) == '~')
                        key = DEL;
                    break;
                case 'H':
                    key = HOME;
                    break;
                case 'F':
                    key = ENDLINE;
                    break;
                }
                break;
#endif
            }
        }

        /* Remap Backspace key */
        if (key == 8 || key == 0x7F)
            key = BKSPACE;

        /* Remap carriage return */
        if (key == '\r')
            key = '\n';

        switch (key) {
        case LEFT:
            cr = move_left(cb, mult);
            goto top;
        case RIGHT:
            cr = move_right(cb, mult);
            goto top;
        case UP:
            cr = up_line(cb, mult);
            goto top;
        case DOWN:
            cr = down_line(cb, mult);
            goto top;
        case HOME:
            start_of_line(cb);
            goto top;
        case ENDLINE:
            end_of_line(cb);
            goto top;
        case DEL:
            cr = delete_char(cb, mult);
            goto top;
        case BKSPACE:
            cr = backspace_char(cb, mult);
            goto top;
        case TRIMCLEAN:
            trim_clean(cb);
            goto top;
        case SETMARK:
            set_mark(cb);
            goto top;
        case CUT:
            cr = copy_region(cb, p, 1);
            goto top;
        case PASTE:
            cr = paste(cb, p, mult);
            goto top;
        case CENTRE:
            centre = 1;
            goto top;
        case SEARCH:
            delete_buffer(cl);
            cla = 1;
            operation = 'S';
            goto top;
        case CUTTOSOL:
            cr = cut_to_sol(cb, p);
            goto top;
        case CUTTOEOL:
            cr = cut_to_eol(cb, p);
            goto top;
        case CMDEXIT:
            if (cb->m_set) {
                /* Deactivate mark */
                cb->m_set = 0;
            } else if (cla) {
                /* Exit command line */
                cla = 0;
                operation = '\0';
            }
            goto top;
        case INSERTHEX:
            cr = insert_hex(cb, mult);
            goto top;
        }

        if (key == C('x')) {
            key = GETCH();
            switch (key) {
            case CLOSE:
                running = 0;
                break;
            case SAVE:
                cr = write_buffer(cb, cb->fn, 1);
                goto top;
            case RENAME:
                delete_buffer(cl);
                cla = 1;
                operation = 'R';
                goto top;
            case INSERTFILE:
                delete_buffer(cl);
                cla = 1;
                operation = 'I';
                goto top;
            case NEWBUF:
                delete_buffer(cl);
                cla = 1;
                operation = 'N';
                goto top;
            }
            /* Left and right buffer, respectively */
#ifdef _WIN32
            if (key == 0xE0) {
                key = GETCH();
                switch (key) {
                case 'K':
                    z->a ? --z->a : (cr = 1);
                    goto top;
                case 'M':
                    z->a != z->u - 1 ? ++z->a : (cr = 1);
                    goto top;
                }
            }
#else
            if (key == 0x1B && (key = GETCH()) == '[') {
                key = GETCH();
                switch (key) {
                case 'D':
                    z->a ? --z->a : (cr = 1);
                    goto top;
                case 'C':
                    z->a != z->u - 1 ? ++z->a : (cr = 1);
                    goto top;
                }
            }
#endif
        }

        /* Execute command line */
        if (key == '\n' && cla) {
            cla = 0;
            operation_copy = operation;
            operation = '\0';
            switch (operation_copy) {
            case 'R':
                if (buffer_to_str(cl, &cl_str)) {
                    cr = 1;
                    goto top;
                }
                cr = rename_buffer(*(z->z + z->a), cl_str);
                goto top;
            case 'S':
                if (buffer_to_mem(cl, se)) {
                    cr = 1;
                    goto top;
                }
                if (se->u > 1) {
                    set_bad(bad, se);
                }
                cr = search(*(z->z + z->a), se, bad);
                goto top;
            case 'I':
                if (buffer_to_str(cl, &cl_str)) {
                    cr = 1;
                    goto top;
                }
                cr = insert_file(*(z->z + z->a), cl_str);
                goto top;
            case 'N':
                if (buffer_to_str(cl, &cl_str)) {
                    cr = 1;
                    goto top;
                }
                cr = new_buffer(z, cl_str);
                goto top;
            }
        }

        if (ISASCII((unsigned int) key)
            && (isgraph((unsigned char) key)
                || key == ' ' || key == '\t' || key == '\n'))
            cr = insert_char(cb, key, mult);
    }

  clean_up:
    if (close_graphics(g))
        ret = 1;
    /* Free memory */
    free_buffer(cl);
    free_tb(z);
    free_mem(se);
    free_mem(p);
    free_mem(sb);
    free(cl_str);
    return ret;
}
