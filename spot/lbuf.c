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
 * lbuf: A minimalistic gap buffer module.
 */

#ifdef __linux__
#define _XOPEN_SOURCE 500
#endif

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lbuf.h"

#ifndef ST_OVERFLOW_TESTS
#define ST_OVERFLOW_TESTS
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))
#endif

/* ASCII test for unsigned input that could be larger than UCHAR_MAX */
#define ISASCII(u) ((u) <= 127)

void set_col_index(struct buffer *b)
{
    /*
     * Sets the column index of the cursor in the memory (not the screen),
     * starting from zero.
     */
    char *q = b->g;
    while (q != b->a && *(q - 1) != '\n')
        --q;
    b->col = b->g - q;
}

int move_left(struct buffer *b, size_t mult)
{
    /*
     * Moves the cursor left mult positions.
     * Text before the old gap is copied into the right-hand side of the old
     * gap.
     */
    if (mult > (size_t) (b->g - b->a))
        return 1;
    while (mult--)
        LCH(b);
    set_col_index(b);
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
    while (mult--)
        RCH(b);
    set_col_index(b);
    return 0;
}

void start_of_buffer(struct buffer *b)
{
    while (b->g != b->a)
        LCH_NO_R(b);
    b->r = 1;
    b->col = 0;
}

void end_of_buffer(struct buffer *b)
{
    while (b->c != b->e)
        RCH(b);
    set_col_index(b);
}

void start_of_line(struct buffer *b)
{
    /* Moves the cursor to the start of the line */
    while (b->g != b->a && *(b->g - 1) != '\n')
        LCH(b);
    b->col = 0;
}

void end_of_line(struct buffer *b)
{
    /* Moves the cursor to the end of the line */
    while (*b->c != '\n' && b->c != b->e)
        RCH(b);
    set_col_index(b);
}

int up_line(struct buffer *b, size_t mult)
{
    /* Moves the cursor up mult lines */
    size_t orig_coli, eol_coli;
    char *q;
    orig_coli = b->col;         /* Get the original column index */
    q = b->g - orig_coli;       /* Jump to start of the line */
    /* Move up mult lines, will stop at the end of the line */
    while (mult && q != b->a)
        if (*--q == '\n')
            --mult;
    if (mult)
        return 1;
    /* Physically move the cursor in the memory up mult lines */
    move_left(b, b->g - q);
    /* Get the column index at the end of the new line */
    eol_coli = b->col;
    /* Move back along the line to same column index, if possible */
    if (eol_coli > orig_coli)
        q -= eol_coli - orig_coli;
    move_left(b, b->g - q);
    return 0;
}

int down_line(struct buffer *b, size_t mult)
{
    /* Moves the cursor down mult lines */
    size_t coli = b->col;       /* Get the existing column index */
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
    b->r = 1;
    b->col = 0;
    b->m = 0;
    b->mr = 1;
    b->m_set = 0;
    b->mod = 1;
}

void trim_clean(struct buffer *b)
{
    /*
     * Trims (deletes) trailing whitespace and cleans (deletes all characters
     * that are not ASCII graph, space, tab, or newline.
     */
    int nl_enc = 0;             /* Trailing \n char has been encountered */
    int at_eol = 0;             /* At end of line */
    int del = 0;                /* At least one char deleted indicator */
    end_of_buffer(b);
    /* Empty buffer */
    if (b->g == b->a)
        return;
    /* Move to the left of the EOBCH */
    LCH_NO_R(b);
    /*
     * Process the end of the file up until the first graph character, that is
     * the trailing characters at the end of the file. This is done backwards
     * starting from the end of the buffer. The first newline character
     * encountered will be preserved.
     */
    while (!isgraph((unsigned char) *b->c)) {
        if (!nl_enc && *b->c == '\n') {
            nl_enc = 1;
        } else {
            DCH(b);
            del = 1;
        }

        /* Start of buffer */
        if (b->g == b->a)
            break;
        else
            LCH_NO_R(b);
    }
    /*
     * Process the rest of the file, keeping track if the cursor is at the end
     * of the line (in which case whitespace is trimmed too).
     */
    while (1) {
        if (*b->c == '\n') {
            at_eol = 1;
        } else if (isgraph((unsigned char) *b->c)) {
            at_eol = 0;
        } else if (at_eol) {
            DCH(b);
            del = 1;
        } else if (*b->c != ' ' && *b->c != '\t') {
            DCH(b);
            del = 1;
        }

        /* Start of buffer */
        if (b->g == b->a)
            break;
        else
            LCH_NO_R(b);
    }
    b->r = 1;
    b->col = 0;
    if (del) {
        b->m_set = 0;
        b->mod = 1;
    }
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

static int grow_gap(struct buffer *b, size_t req)
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
    if (ch == '\n') {
        b->r += mult;
        b->col = 0;
    } else {
        b->col += mult;
    }
    b->m_set = 0;
    b->mod = 1;
    return 0;
}

int delete_char(struct buffer *b, size_t mult)
{
    /*
     * Deletes mult chars by expanding the gap rightwards.
     * Does not affect the row number or column index.
     */
    if (mult > (size_t) (b->e - b->c))
        return 1;
    b->c += mult;
    b->m_set = 0;
    b->mod = 1;
    return 0;
}

int backspace_char(struct buffer *b, size_t mult)
{
    /* Backspaces mult chars by expanding the gap leftwards */
    if (mult > (size_t) (b->g - b->a))
        return 1;
    while (mult--)
        BSPC(b);
    set_col_index(b);
    b->m_set = 0;
    b->mod = 1;
    return 0;
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
    b->r = 1;
    b->col = 0;
    b->d = 0;
    b->m = 0;
    b->mr = 1;
    b->m_set = 0;
    b->mod = 0;
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

struct mem *init_mem(void)
{
    struct mem *p;
    if ((p = malloc(sizeof(struct mem))) == NULL)
        return NULL;
    p->p = NULL;
    p->u = 0;
    p->s = 0;
    p->rows = 0;
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
    b->mod = 1;
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
    /* Nothing to do if saving an unmodified buffer to its default filename */
    if (b->fn != NULL && !strcmp(b->fn, fn) && !b->mod)
        return 0;
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
    /* Clear mod on a successful save to the default buffer filename */
    if (b->fn != NULL && !strcmp(b->fn, fn))
        b->mod = 0;
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
    /* Set mod indicator so that a subsequent save will work */
    b->mod = 1;
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
    b->mr = b->r;
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
    /* Mark before cursor */
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
        p->rows = b->r - b->mr;
        if (del) {
            b->g -= s;
            b->r -= p->rows;
            set_col_index(b);
            b->mod = 1;
        }
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
        p->rows = b->mr - b->r;
        if (del) {
            b->c += s;
            b->mod = 1;
        }
    }
    b->m_set = 0;
    return 0;
}

int paste(struct buffer *b, struct mem *p, size_t mult)
{
    size_t s;
    if (MOF(p->u, mult))
        return 1;
    s = p->u * mult;
    if (s > (size_t) (b->c - b->g) && grow_gap(b, s))
        return 1;
    while (mult--) {
        memcpy(b->g, p->p, p->u);
        b->g += p->u;
        b->r += p->rows;
    }
    set_col_index(b);
    b->m_set = 0;
    b->mod = 1;
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
