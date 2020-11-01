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

/*
 * README:
 * Depending on your system:
 * > cl spot.c
 * or
 * $ cc spot.c && mv a.out spot
 * and place spot.exe or spot somewhere in your PATH.
 *
 * spot:
 * - Is cross-platform.
 * - Does not use any curses library.
 * - Uses a double buffering method for graphics which has no flickering.
 * - Uses the gap buffer method for text with transactional operations.
 * - Uses the Quick Search algorithm with a reusable bad character table on
     repeated searches.
 */

#ifdef __linux__
#define _XOPEN_SOURCE 500
#endif

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define REPSEARCH '/'
#define REDRAW '-'
#define STARTBUF '<'
#define ENDBUF '>'
#define MATCHBRACE '='

/* Number of spaces used to display a tab */
#define TABSIZE 4

/*
 * Default gap size. Must be at least 1.
 * It is good to set small while testing, say 2, but BUFSIZ is a sensible
 * choice for real use (to limit the expense of growing the gap).
 */
#define GAP BUFSIZ

/* Default number of spare text buffer pointers. Must be at least 1 */
#define SPARETB 10

/*
 * End Of Buffer CHaracter. This cannot be deleted, but will not be written to
 * file.
 */
#define EOBCH '~'

/* size_t integer overflow tests */
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

/* ASCII test for unsigned input that could be larger than UCHAR_MAX */
#define ISASCII(u) ((u) <= 127)

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

/* One character operations with no out of bounds checking */
/* Move left */
#define LCH() (*--b->c = *--b->g)
/* Move right */
#define RCH() (*b->g++ = *b->c++)
/* Delete */
#define DCH() (b->c++)

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

/*
 * Gap buffer structure.
 * The cursor is always to the immediate right of the gap.
 */
struct buffer {
    char *fn;                   /* Filename where the buffer will save to */
    char *a;                    /* Start of buffer */
    char *g;                    /* Start of gap */
    char *c;                    /* Cursor (after gap) */
    char *e;                    /* End of buffer */
    size_t d;                   /* Draw start index */
    size_t m;                   /* Mark index */
    int m_set;                  /* Mark set indicator */
};

/* Structure to keep track of the text buffers */
struct tb {
    struct buffer **z;          /* Text buffers */
    size_t u;                   /* Used number of text buffers */
    size_t n;                   /* Total number of text buffers */
    size_t a;                   /* The index of the active text buffer */
};

/* Memory structure: used for copy and paste, and search */
struct mem {
    char *p;                    /* Pointer to memory */
    size_t u;                   /* Used memory amount (<= s) */
    size_t s;                   /* Memory size */
};

int move_left(struct buffer *b, size_t mult)
{
    /*
     * Moves the cursor left mult positions.
     * Text before the old gap is copied into the right-hand side of the old
     * gap.
     */
    if (mult > (size_t) (b->g - b->a))
        return 1;
    memmove(b->c - mult, b->g - mult, mult);
    b->g -= mult;
    b->c -= mult;
    return 0;
}

int move_right(struct buffer *b, size_t mult)
{
    /*
     * Moves the cursor right mult positions.
     * Text after the old gap is copied into the left-hand side of the old gap.
     */
    if (mult > (size_t) (b->e - b->c))
        return 1;
    memmove(b->g, b->c, mult);
    b->g += mult;
    b->c += mult;
    return 0;
}

void start_of_buffer(struct buffer *b)
{
    /* No need to check return value, as only fails if out of bounds */
    move_left(b, b->g - b->a);
}

void end_of_buffer(struct buffer *b)
{
    /* No need to check return value, as only fails if out of bounds */
    move_right(b, b->e - b->c);
}

void start_of_line(struct buffer *b)
{
    /* Moves the cursor to the start of the line */
    while (b->g != b->a && *(b->g - 1) != '\n')
        LCH();
}

void end_of_line(struct buffer *b)
{
    /* Moves the cursor to the end of the line */
    while (*b->c != '\n' && b->c != b->e)
        RCH();
}

size_t col_index(struct buffer *b)
{
    /*
     * Returns the column index of the cursor in the memory (not the screen),
     * starting from zero.
     */
    char *q = b->g;
    while (q != b->a && *(q - 1) != '\n')
        --q;
    return b->g - q;
}

int up_line(struct buffer *b, size_t mult)
{
    /* Moves the cursor up mult lines */
    size_t orig_coli, eol_coli;
    char *q;
    orig_coli = col_index(b);   /* Get the original column index */
    q = b->g - orig_coli;       /* Jump to start of the line */
    /* Move up mult lines, will stop at the end of the line */
    while (mult && q != b->a)
        if (*--q == '\n')
            --mult;
    if (mult)
        return 1;
    /*
     * Physically move the cursor in the memory up mult lines,
     * so that col_index can be called.
     */
    move_left(b, b->g - q);
    /* Get the column index at the end of the new line */
    eol_coli = col_index(b);
    /* Move back along the line to same column index, if possible */
    if (eol_coli > orig_coli)
        q -= eol_coli - orig_coli;
    move_left(b, b->g - q);
    return 0;
}

int down_line(struct buffer *b, size_t mult)
{
    /* Moves the cursor down mult lines */
    size_t coli = col_index(b); /* Get the existing column index */
    char *q;
    q = b->c;
    /* Move down mult lines */
    while (mult && q != b->e)
        if (*q++ == '\n')
            --mult;
    if (mult)
        return 1;
    /* Move forward along the to the original column index, if possible */
    while (coli-- && q != b->e && *q != '\n')
        ++q;
    /* Physically move the cursor in the memory */
    move_right(b, q - b->c);
    return 0;
}

int match_brace(struct buffer *b)
{
    /* Moves the cursor to the matching brace */
    size_t depth = 1;           /* Depth inside nested braces */
    char orig = *b->c, target, *q;
    int right;                  /* Movement direction */
    /* Cannot match EOBCH */
    if (b->c == b->e)
        return 0;
    switch (orig) {
    case '(':
        target = ')';
        right = 1;
        break;
    case '<':
        target = '>';
        right = 1;
        break;
    case '[':
        target = ']';
        right = 1;
        break;
    case '{':
        target = '}';
        right = 1;
        break;
    case ')':
        target = '(';
        right = 0;
        break;
    case '>':
        target = '<';
        right = 0;
        break;
    case ']':
        target = '[';
        right = 0;
        break;
    case '}':
        target = '{';
        right = 0;
        break;
    default:
        /* Nothing to match */
        return 0;
    }
    if (right) {
        q = b->c + 1;
        while (q != b->e) {
            if (*q == target)
                --depth;
            else if (*q == orig)
                ++depth;
            if (!depth)
                break;
            ++q;
        }
        if (!depth) {
            move_right(b, q - b->c);
            return 0;
        }
        return 1;
    }
    /* Left */
    if (b->g == b->a)
        return 1;
    q = b->g - 1;
    while (1) {
        if (*q == target)
            --depth;
        else if (*q == orig)
            ++depth;
        if (!depth || q == b->a)
            break;
        --q;
    }
    if (!depth) {
        move_left(b, b->g - q);
        return 0;
    }
    return 1;
}

void delete_buffer(struct buffer *b)
{
    /* Soft delete buffer */
    b->g = b->a;
    b->c = b->e;
    b->m_set = 0;
    b->m = 0;
}

void trim_clean(struct buffer *b)
{
    /*
     * Trims (deletes) trailing whitespace and cleans (deletes all characters
     * that are not ASCII graph, space, tab, or newline.
     */
    int nl_enc = 0;             /* Trailing \n char has been encountered */
    int at_eol = 0;             /* At end of line */
    end_of_buffer(b);
    /* Empty buffer */
    if (b->g == b->a)
        return;
    /* Move to the left of the EOBCH */
    LCH();
    /*
     * Process the end of the file up until the first graph character, that is
     * the trailing characters at the end of the file. This is done backwards
     * starting from the end of the buffer. The first newline character
     * encountered will be preserved.
     */
    while (!isgraph((unsigned char) *b->c)) {
        if (!nl_enc && *b->c == '\n')
            nl_enc = 1;
        else
            DCH();
        /* Start of buffer */
        if (b->g == b->a)
            break;
        else
            LCH();
    }
    /*
     * Process the rest of the file, keeping track if the cursor is at the end
     * of the line (in which case whitespace is trimmed too).
     */
    while (1) {
        if (*b->c == '\n')
            at_eol = 1;
        else if (isgraph((unsigned char) *b->c))
            at_eol = 0;
        else if (at_eol)
            DCH();
        else if (*b->c != ' ' && *b->c != '\t')
            DCH();
        /* Start of buffer */
        if (b->g == b->a)
            break;
        else
            LCH();
    }
    b->m_set = 0;
}

void set_bad(size_t * bad, struct mem *se)
{
    /* Sets the bad character table for the Quick Search algorithm */
    unsigned char *pat = (unsigned char *) se->p;       /* Search pattern */
    size_t i;
    for (i = 0; i <= UCHAR_MAX; ++i)
        *(bad + i) = se->u + 1;
    for (i = 0; i < se->u; ++i)
        *(bad + *(pat + i)) = se->u - i;
}

int search(struct buffer *b, struct mem *se, size_t * bad)
{
    /*
     * Forward search, excludes cursor and EOBCH.
     * Uses the Quick Search algorithm.
     */
    char *q, *stop, *q_copy, *pat;
    size_t patlen;
    int found = 0;
    size_t s;
    if (!se->u)
        return 1;
    if (b->e - b->c <= 1)
        return 1;
    s = b->e - b->c - 1;
    if (se->u > s)
        return 1;
    if (se->u == 1) {
        /* Single character patterns */
        q = memchr(b->c + 1, *se->p, s);
        if (q == NULL)
            return 1;
    } else {
        /* Quick Search algorithm */
        q = b->c + 1;
        stop = b->e - se->u;    /* Inclusive */
        while (q <= stop) {
            q_copy = q;
            pat = se->p;
            patlen = se->u;
            /* Compare pattern to text */
            do {
                if (*q_copy++ != *pat++)
                    break;
            } while (--patlen);
            /* Match found */
            if (!patlen) {
                found = 1;
                break;
            }
            /* Jump using the bad character table */
            q += *(bad + (unsigned char) *(q + se->u));
        }
        if (!found)
            return 1;

    }
    move_right(b, q - b->c);    /* Must be in bounds */
    return 0;
}

int grow_gap(struct buffer *b, size_t req)
{
    /*
     * Increases the gap.
     * Only called when an insert is planned with a size greater than the
     * current gap size. It is OK for the gap to stay zero, so long as an
     * insert is not planned. The gap is increased so that the resultant gap
     * can fit the planned insert plus the default GAP (to avoid having to
     * grow the gap again soon afterwards), or so that the whole buffer is
     * doubled (to protect against repeated inserts), whichever is larger.
     * grow_gap does not change the mark.
     */
    size_t rg, min_increase, current_size, target_size, increase;
    char *t, *tc;
    if (AOF(req, GAP))
        return 1;
    rg = req + GAP;
    /* Only call grow_gap if req > (b->c - b->g) */
    min_increase = rg - (b->c - b->g);
    current_size = b->e - b->a + 1;
    increase = current_size > min_increase ? current_size : min_increase;
    if (AOF(current_size, increase))
        return 1;
    target_size = current_size + increase;
    if ((t = malloc(target_size)) == NULL)
        return 1;
    memcpy(t, b->a, b->g - b->a);
    tc = t + (b->c - b->a) + increase;
    memcpy(tc, b->c, b->e - b->c + 1);
    b->g = t + (b->g - b->a);
    b->c = tc;
    b->e = t + target_size - 1;
    free(b->a);
    b->a = t;
    return 0;
}

int insert_char(struct buffer *b, char ch, size_t mult)
{
    /*
     * Inserts the same char mult times into the left-hand side of the old gap.
     */
    if (mult > (size_t) (b->c - b->g) && grow_gap(b, mult))
        return 1;
    memset(b->g, ch, mult);
    b->g += mult;
    b->m_set = 0;
    return 0;
}

int delete_char(struct buffer *b, size_t mult)
{
    /* Deletes mult chars by expanding the gap rightwards */
    if (mult > (size_t) (b->e - b->c))
        return 1;
    b->c += mult;
    b->m_set = 0;
    return 0;
}

int backspace_char(struct buffer *b, size_t mult)
{
    /* Backspaces mult chars by expanding the gap leftwards */
    if (mult > (size_t) (b->g - b->a))
        return 1;
    b->g -= mult;
    b->m_set = 0;
    return 0;
}

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

struct buffer *init_buffer(size_t req)
{
    /*
     * Initialises a buffer.
     * req increases the gap size, to avoid growing the gap when the size of
     * a planned insert is known in advance. The end of buffer character,
     * EOBCH, is added.
     */
    struct buffer *b;
    size_t rg;
    if (AOF(req, GAP))
        return NULL;
    rg = req + GAP;
    if ((b = malloc(sizeof(struct buffer))) == NULL)
        return NULL;
    if ((b->a = malloc(rg)) == NULL) {
        free(b);
        return NULL;
    }
    b->fn = NULL;
    b->g = b->a;
    *(b->e = b->a + rg - 1) = EOBCH;
    b->c = b->e;
    b->d = 0;
    b->m = 0;
    b->m_set = 0;
    return b;
}

void free_buffer(struct buffer *b)
{
    if (b != NULL) {
        free(b->fn);
        free(b->a);
        free(b);
    }
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

struct mem *init_mem(void)
{
    struct mem *p;
    if ((p = malloc(sizeof(struct mem))) == NULL)
        return NULL;
    p->p = NULL;
    p->u = 0;
    p->s = 0;
    return p;
}

void free_mem(struct mem *p)
{
    if (p != NULL) {
        free(p->p);
        free(p);
    }
}

int get_file_size(char *fn, size_t * fs)
{
    /* Gets the file size for regular files */
    struct stat st;
    if (stat(fn, &st))
        return 1;
    if (!((st.st_mode & S_IFMT) == S_IFREG))
        return 1;
    if (st.st_size < 0)
        return 1;
    *fs = (size_t) st.st_size;
    return 0;
}

int insert_file(struct buffer *b, char *fn)
{
    /*
     * Inserts a file.
     * The file will be inserted into the right-hand side of the old gap,
     * so that the inserted text will commence from the new cursor.
     */
    size_t fs;
    FILE *fp;
    if (get_file_size(fn, &fs))
        return 1;
    if (!fs)
        return 0;
    if (fs > (size_t) (b->c - b->g) && grow_gap(b, fs))
        return 1;
    if ((fp = fopen(fn, "rb")) == NULL)
        return 1;
    if (fread(b->c - fs, 1, fs, fp) != fs) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp))
        return 1;
    b->c -= fs;
    b->m_set = 0;
    return 0;
}

int write_buffer(struct buffer *b, char *fn, int backup_req)
{
    /*
     * Writes a buffer to file. If the file already exists, then it will be
     * renamed to have a '~' suffix (to serve as a backup).
     */
    struct stat st;
    int backup_ok = 0;
    size_t len, bk_s, num;
    char *backup_fn;
    FILE *fp;
    if (fn == NULL)
        return 1;
    /* Create backup of the regular file */
    if (backup_req && !stat(fn, &st) && st.st_mode & S_IFREG) {
        len = strlen(fn);
        if (AOF(len, 2))
            return 1;
        bk_s = len + 2;
        if ((backup_fn = malloc(bk_s)) == NULL)
            return 1;
        memcpy(backup_fn, fn, len);
        *(backup_fn + len) = '~';
        *(backup_fn + len + 1) = '\0';
#ifdef _WIN32
        if (!MoveFileEx(fn, backup_fn, MOVEFILE_REPLACE_EXISTING)) {
            free(backup_fn);
            return 1;
        }
#else
        if (rename(fn, backup_fn)) {
            free(backup_fn);
            return 1;
        }
#endif
        free(backup_fn);
        backup_ok = 1;
    }
    if ((fp = fopen(fn, "wb")) == NULL)
        return 1;
    num = (size_t) (b->g - b->a);
    if (fwrite(b->a, 1, num, fp) != num) {
        fclose(fp);
        return 1;
    }
    num = (size_t) (b->e - b->c);
    if (fwrite(b->c, 1, num, fp) != num) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp))
        return 1;
#ifndef _WIN32
    if (backup_ok && chmod(fn, st.st_mode & 0777))
        return 1;
#endif
    return 0;
}

int rename_buffer(struct buffer *b, char *new_name)
{
    /* Sets or changes the filename associated with a buffer */
    size_t len, s;
    char *t;
    if (new_name == NULL)
        return 1;
    len = strlen(new_name);
    if (!len)
        return 1;
    if (AOF(len, 1))
        return 1;
    s = len + 1;
    if ((t = malloc(s)) == NULL)
        return 1;
    memcpy(t, new_name, len);
    *(t + len) = '\0';
    free(b->fn);
    b->fn = t;
    return 0;
}

int buffer_to_str(struct buffer *b, char **p_to_str)
{
    char *t, *q, ch, *p;
    /* OK to add one because of EOBCH */
    size_t s = b->g - b->a + b->e - b->c + 1;
    if ((t = malloc(s)) == NULL)
        return 1;
    p = t;
    /* Strip out \0 chars */
    q = b->a;
    while (q != b->g)
        if ((ch = *q++) != '\0')
            *p++ = ch;
    q = b->c;
    while (q != b->e)
        if ((ch = *q++) != '\0')
            *p++ = ch;
    *p = '\0';
    free(*p_to_str);
    *p_to_str = t;
    return 0;
}

int buffer_to_mem(struct buffer *b, struct mem *m)
{
    size_t s_bg = b->g - b->a;  /* Size before gap */
    size_t s_ag = b->e - b->c;  /* Size after gap */
    size_t s = s_bg + s_ag;     /* Text size of buffer */
    char *t;                    /* Temporary pointer */
    if (s > m->s) {
        if ((t = malloc(s)) == NULL)
            return 1;
        free(m->p);
        m->p = t;
        m->s = s;
    }
    memcpy(m->p, b->a, s_bg);
    memcpy(m->p + s_bg, b->c, s_ag);
    m->u = s;
    return 0;
}

void set_mark(struct buffer *b)
{
    b->m = b->g - b->a;
    b->m_set = 1;
}

int copy_region(struct buffer *b, struct mem *p, int del)
{
    size_t ci = b->g - b->a;    /* Cursor index */
    size_t s;                   /* Size of region */
    char *t;                    /* Temporary pointer */
    if (!b->m_set)
        return 1;
    if (b->m == ci)
        return 0;
    if (b->m < ci) {
        s = ci - b->m;
        if (s > p->s) {
            if ((t = malloc(s)) == NULL)
                return 1;
            free(p->p);
            p->p = t;
            p->s = s;
        }
        memcpy(p->p, b->a + b->m, s);
        p->u = s;
        if (del)
            b->g -= s;
    } else {
        s = b->m - ci;
        if (s > p->s) {
            if ((t = malloc(s)) == NULL)
                return 1;
            free(p->p);
            p->p = t;
            p->s = s;
        }
        memcpy(p->p, b->c, s);
        p->u = s;
        if (del)
            b->c += s;
    }
    b->m_set = 0;
    return 0;
}

int paste(struct buffer *b, struct mem *p, size_t mult)
{
    size_t s;
    if (MOF(p->u, mult))
        return 1;
    s = p->u + mult;
    if (s > (size_t) (b->c - b->g) && grow_gap(b, s))
        return 1;
    while (mult--) {
        memcpy(b->g, p->p, p->u);
        b->g += p->u;
    }
    b->m_set = 0;
    return 0;
}

int cut_to_eol(struct buffer *b, struct mem *p)
{
    if (*b->c == '\n')
        return delete_char(b, 1);
    set_mark(b);
    end_of_line(b);
    return copy_region(b, p, 1);
}

int cut_to_sol(struct buffer *b, struct mem *p)
{
    set_mark(b);
    start_of_line(b);
    return copy_region(b, p, 1);
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
    /* Success */
    *(z->z + z->u) = b;         /* Link back */
    z->a = z->u;                /* Set active text buffer to the new one */
    ++z->u;                     /* Increase the number of used text buffers */
    return 0;
}

#ifdef _WIN32
int setup_graphics(void)
{
    /* Turn on interpretation of VT100-like escape sequences */
    HANDLE out;
    DWORD mode;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE)
        return 1;
    if (!GetConsoleMode(out, &mode))
        return 1;
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return 1;
    return 0;
}
#endif

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

void centre_cursor(struct buffer *b, size_t num_up, size_t limit)
{
    char *q;
    q = b->g;
    while (num_up && limit && q != b->a) {
        --limit;
        if (*--q == '\n')
            --num_up;
    }
    if (num_up) {
        b->d = b->g - b->a;     /* Failed, so draw from cursor */
        return;
    }
    while (q != b->a && *(q - 1) != '\n')
        --q;
    b->d = q - b->a;
}

void draw_screen(struct buffer *b, struct buffer *cl, int cla, int cr,
                 size_t h, size_t w, char *ns, size_t * cy, size_t * cx,
                 int centre)
{
    /* Virtually draw screen (and clear unused sections on the go) */
    char *q;
    unsigned char uch;
    size_t v;                   /* Virtual screen index */
    /* Height of text buffer portion of the screen */
    size_t th = h > 2 ? h - 2 : 1;
    size_t ci = b->g - b->a;    /* Cursor index */
    size_t ta = th * w;         /* Text screen area */
    size_t hth = th > 2 ? th / 2 : 1;   /* Half the text screen height */
    size_t hta = hth * w;       /* Half the text screen area */
    size_t j;                   /* Filename character index */
    size_t len;                 /* Used for printing the filename */
    size_t i;                   /* Index for printing tabs */

    /* Text buffer */

    /*
     * Test to see if cursor is definitely off the screen.
     * In this case we must centre.
     */
    if (ci < b->d || ci - b->d >= ta)
        centre = 1;

  text_draw:
    if (centre)
        centre_cursor(b, hth, hta);

    v = 0;
    q = b->a + b->d;
    /* Print before the gap */
    while (q != b->g && v / w != th) {
        /*
         * When a newline character is encountered, complete the screen
         * line with spaces. The modulus will be zero at the start of
         * the next screen line. The "do" makes it work when the '\n'
         * is the first character on a screen line.
         */
        VPUTCH(*q++);
    }

    if (q != b->g) {
        /* Cursor is outside of text portion of screen */
        if (!centre)
            centre = 1;
        else {
            centre = 0;
            b->d = b->g - b->a; /* Draw from cursor */
        }
        goto text_draw;
    }

    /* Record cursor's screen location */
    *cy = v / w;
    *cx = v % w;
    q = b->c;
    /* Print after the gap */
    do {
        VPUTCH(*q);
        /* Stop if have reached the status bar, before printing there */
        if (v / w == th)
            break;
    } while (q++ != b->e);

    /* Fill in unused text region with spaces */
    while (v / w != th)
        *(ns + v++) = ' ';

    /* Stop if screen is only one row high */
    if (h == 1)
        return;

    /*
     * Status bar:
     * Print indicator if last command failed.
     */
    *(ns + v++) = cr ? '!' : ' ';

    /* Blank space */
    if (w >= 2) {
        *(ns + v++) = ' ';
    }

    /* Print filename */
    if (w > 2 && b->fn != NULL) {
        len = strlen(b->fn);
        /* Truncate filename if screen is not wide enough */
        len = len < w - 4 ? len : w - 4;
        /* Print file name in status bar */
        for (j = 0; j < len; ++j)
            *(ns + v++) = *(b->fn + j);
    }
    /* Complete status bar with spaces */
    while (v / w != h - 1)
        *(ns + v++) = ' ';

    /* Stop if screen is only two rows high */
    if (h == 2)
        return;

    /* Command line buffer */
/*
cl_draw:
    centre_cursor(cl, 0, w);
*/
    q = cl->a + cl->d;
    /* Print before the gap */
    while (q != cl->g) {
        VPUTCH(*q++);
    }


    /* If the command line is active, record cursor's screen location */
    if (cla) {
        *cy = v / w;
        *cx = v % w;
    }
    q = cl->c;
    /* Print after the gap */
    while (1) {
        VPUTCH(*q);
        /* Stop if off the bottom of the screen, before printing there */
        if (v / w == h)
            break;
        if (q == cl->e)
            break;
        ++q;
    }
    /* Fill in unused text region with spaces */
    while (v / w != h)
        *(ns + v++) = ' ';
}

void diff_draw(char *ns, char *cs, size_t sa, size_t w)
{
    /* Physically draw the screen where the virtual screens differ */
    size_t v;                   /* Index */
    int in_pos = 0;             /* In position for printing */
    char ch;
    for (v = 0; v < sa; ++v) {
        if ((ch = *(ns + v)) != *(cs + v)) {
            if (!in_pos) {
                /* Top left corner is (1, 1) not (0, 0) so need to add one */
                MOVE_CURSOR(v / w + 1, v - (v / w) * w + 1);
                in_pos = 1;
            }
            putchar(ch);
        } else {
            in_pos = 0;
        }
    }
}

int main(int argc, char **argv)
{
    int ret = 0;                /* Editor's return value */
    int running = 1;            /* Editor is running */
    size_t h = 0, w = 0;        /* Screen height and width (real) */
    size_t new_h, new_w;        /* New screen height and width (real) */
    size_t cy, cx;              /* Cursor's screen coordinates */
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
    int cr = 0;                 /* Command return value */
    int centre = 0;             /* Request to centre the cursor */
    int redraw = 0;             /* Request to redraw the entire screen */
    struct buffer *cb;          /* Shortcut to the cursor's buffer */
    char *ns = NULL;            /* Next screen (virtual) */
    char *cs = NULL;            /* Current screen (virtual) */
    size_t ss = 0;              /* Screen size (virtual) */
    size_t sa = 0;              /* Terminal screen area (real) */
    /* Keyboard key (one physical key can send multiple) */
    int key;
    int digit;                  /* Numerical digit */
    size_t mult;                /* Command multiplier (cannot be zero) */
    char *t;                    /* Temporary pointer */
    size_t i;                   /* Generic index */
#ifndef _WIN32
    struct termios term_orig, term_new;
#endif

    /* Check input is from a terminal */
#ifdef _WIN32
    if (!_isatty(_fileno(stdin)))
        return 1;
#else
    if (!isatty(STDIN_FILENO))
        return 1;
#endif

#ifndef _WIN32
    /* Change terminal input to raw and no echo */
    if (tcgetattr(STDIN_FILENO, &term_orig))
        return 1;
    term_new = term_orig;
    cfmakeraw(&term_new);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term_new))
        return 1;
#endif

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
#ifdef _WIN32
    setup_graphics();
#endif

    /* Editor loop */
    while (running) {

        /* Top of the editor loop */
      top:
        if (get_screen_size(&new_h, &new_w)) {
            ret = 1;
            goto clean_up;
        }

        /* Do graphics only if screen is big enough */
        if (new_h >= 1 && new_w >= 1) {
            /* Requested redraw or change in screen dimensions */
            if (redraw || new_h != h || new_w != w) {
                h = new_h;
                w = new_w;
                if (h > INT_MAX / w) {
                    ret = 1;
                    goto clean_up;
                }
                sa = h * w;
                /* Bigger screen */
                if (ss < sa) {
                    free(ns);
                    free(cs);
                    if ((t = malloc(sa)) == NULL) {
                        ret = 1;
                        goto clean_up;
                    }
                    ns = t;
                    if ((t = malloc(sa)) == NULL) {
                        ret = 1;
                        goto clean_up;
                    }
                    cs = t;
                    ss = sa;
                }
                /*
                 * Clear the virtual current screen and the physical screen.
                 * There is no need to clear the virtual next screen, as it
                 * clears during printing.
                 */
                memset(cs, ' ', ss);
                CLEAR_SCREEN();
            }

            draw_screen(*(z->z + z->a), cl, cla, cr, h, w, ns, &cy, &cx,
                        centre);
            /* Clear centre request */
            centre = 0;
            diff_draw(ns, cs, sa, w);
            /* Top left corner is (1, 1) not (0, 0) so need to add one */
            MOVE_CURSOR(cy + 1, cx + 1);
            /* Swap virtual screens */
            t = cs;
            cs = ns;
            ns = t;
        }

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
                redraw = 1;
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
    CLEAR_SCREEN();
#ifndef _WIN32
    if (tcsetattr(STDIN_FILENO, TCSANOW, &term_orig))
        ret = 1;
#endif
    /* Free memory */
    free_buffer(cl);
    free_tb(z);
    free_mem(se);
    free_mem(p);
    free(cl_str);
    free(ns);
    free(cs);
    return ret;
}
