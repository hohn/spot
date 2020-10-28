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
 * - Can receive keys from a pipe.
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
#define Cx C('x')
/* Control spacebar */
#define Cspc 0
#define ESC 27

/*
 * KEY BINDINGS
 */
#define SETMARK Cspc
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
/* Kill. Uproot when command multiplier is zero */
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
 * ESC prefix (Alt or Meta)
 */
#define COPY 'w'
#define REPSEARCH '/'
#define REDRAW '-'
#define STARTBUF '<'
#define ENDBUF '>'
#define MATCHBRACE '='
#define REGEXREG 'x'
#define UNDOREGEX 'X'

/*
 * Get a character from the input, which is either the terminal (keyboard)
 * or stdin. Only use in main because of the dependency on term_in.
 */
#ifdef _WIN32
#define GETCH() (term_in ? _getch() : getchar())
#else
#define GETCH() getchar()
#endif

/* Log file template */
#define LOGTEMPLATE ".spot_log_XXXXXXXXXX"

/* Templates for files used by sed */
#define CLTEMPLATE ".c_XXXXXXXXXX"
#define INTEMPLATE ".i_XXXXXXXXXX"
#define OUTTEMPLATE ".o_XXXXXXXXXX"
#define ERRTEMPLATE ".e_XXXXXXXXXX"

#ifdef _WIN32
#define SEDSTR "sed -C -b -r -f"
#else
#define SEDSTR "LC_ALL=C sed -r -f"
#endif

#define SEDFORMAT "%s %s %s 1> %s 2> %s"

/* Max sed command size including trailing \0 character */
#define SEDMAXCMD 100

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

/* ASCII test for unsigned input that could be larger than UCHAR_MAX */
#define ISASCII(u) ((u) <= 127)

/* ANSI escape sequences */
#define CLEAR_SCREEN() printf("\033[2J")
#define CLEAR_LINE() printf("\033[2K")

/* One character operations with no out of bounds checking */
/* Move left */
#define LCH() (*--b->c = *--b->g)
/* Move right */
#define RCH() (*b->g++ = *b->c++)
/* Delete */
#define DCH() (b->c++)

/* Global log file used indicator (logging may not be successful) */
int log_file_used = 0;

/* Global log file pointer */
FILE *logfp = NULL;

/* Log file pointer */
#define LFP (logfp == NULL ? stderr : logfp)

/* Log an error message without errno */
#define LOG(m) do { \
    fprintf(LFP, "%s: %s: %d: %s\n", __FILE__, func, __LINE__, m); \
    fflush(LFP); \
    if (logfp != NULL) log_file_used = 1; \
} while (0)

/* Log an error message with errno */
#define LOGE(m) do { \
    fprintf(LFP, "%s: %s: %d: %s: %s\n", __FILE__, func, __LINE__, m, \
        strerror(errno)); \
    fflush(LFP); \
    if (logfp != NULL) log_file_used = 1; \
} while (0)

/*
 * Sets the return value for the text editor (ret) to 1, indicating failure.
 * Logs an error message (without errno) and jumps to the clean up.
 * Only use in main.
 */
#define QUIT(m) do { \
    LOG(m); \
    ret = 1; \
    goto clean_up; \
} while (0)

/* Same as above but with errno */
#define QUITE(m) do { \
    LOGE(m); \
    ret = 1; \
    goto clean_up; \
} while (0)

/*
 * Deletes a file with error logging. Does not set ret to failure.
 * Does not log an error if the file does not exist.
 */
#define RM(fn) do { \
    if (fn != NULL) { \
        errno = 0; \
        if (remove(fn) && errno != ENOENT) { \
            LOGE("RM macro: remove failed"); \
        } \
    } \
} while (0)

/* size_t integer overflow tests */
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

/*
 * Wrapper macros that perform logging and jump to a label (lb) upon error.
 * They do not set ret. Useful for creating an error handling goto chain.
 */
#define SAFEADD(r, a, b, on_fail) do { \
    if (AOF(a, b)) { \
        LOG("SAFEADD macro: size_t addition overflow"); \
        on_fail; \
    } \
    r = (a) + (b); \
} while (0)

#define SAFEMULT(r, a, b, on_fail) do { \
    if (MOF(a, b)) { \
        LOG("SAFEMULT macro: size_t multiplication overflow"); \
        on_fail; \
    } \
    r = (a) * (b); \
} while (0)

#define LMALLOC(p, s, on_fail) do { \
    errno = 0; \
    if ((p = malloc(s)) == NULL) { \
        LOGE("LMALLOC macro: malloc failed"); \
        on_fail; \
    } \
} while (0)

#define LFOPEN(fp, fn, mode, on_fail) do { \
    errno = 0; \
    if ((fp = fopen(fn, mode)) == NULL) { \
        LOGE("LFOPEN macro: fopen failed"); \
        on_fail; \
    } \
} while (0)

#define LFREAD(p, fs, fp, on_fail) do { \
    errno = 0; \
    if (fread(p, 1, fs, fp) != fs) { \
        if (ferror(fp)) LOGE("LFREAD macro: fread failed"); \
        else if (feof(fp)) LOGE("LFREAD macro: fread read EOF"); \
        on_fail; \
    } \
} while (0)

#define LFWRITE(p, s, fp, on_fail) do { \
    if (fwrite(p, 1, s, fp) != s) { \
        LOG("LFWRITE macro: fwrite failed"); \
        on_fail; \
    } \
} while (0)

#define LFCLOSE(fp, on_fail) do { \
    errno = 0; \
    if (fclose(fp)) { \
        LOGE("LFCLOSE macro: fclose failed"); \
        on_fail; \
    } \
} while (0)

/*
 * Gap buffer structure.
 * The cursor is always to the immediate right of the gap.
 */
struct buffer {
    char *fn;  /* Filename where the buffer will save to */
    char *a;   /* Start of buffer */
    char *g;   /* Start of gap */
    char *c;   /* Cursor (after gap) */
    char *e;   /* End of buffer */
    size_t d;  /* Draw start index */
    size_t m;  /* Mark index */
    int m_set; /* Mark set indicator */
};

/* Structure to keep track of the text buffers */
struct tb {
    struct buffer **z; /* Text buffers */
    size_t u;          /* Used number of text buffers */
    size_t n;          /* Total number of text buffers */
    size_t a;          /* The index of the active text buffer */
};

/* Memory structure: used for copy and paste, and search */
struct mem {
    char *p;  /* Pointer to memory */
    size_t u; /* Used memory amount (less than or equal to the size) */
    size_t s; /* Memory size */
};

int move_left(struct buffer *b, size_t mult)
{
    /*
     * Moves the cursor left mult positions.
     * Text before the old gap is copied into the right-hand side of the old
     * gap.
     */
    char *func = "move_left";
    if (mult > (size_t) (b->g - b->a)) {
        LOG("out of bounds");
        return 1;
    }
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
    char *func = "move_right";
    if (mult > (size_t) (b->e - b->c)) {
        LOG("out of bounds");
        return 1;
    }
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
    while (b->g != b->a && *(b->g - 1) != '\n') LCH();
}

void end_of_line(struct buffer *b)
{
    /* Moves the cursor to the end of the line */
    while (*b->c != '\n' && b->c != b->e) RCH();
}

size_t col_index(struct buffer *b)
{
    /*
     * Returns the column index of the cursor in the memory (not the screen),
     * starting from zero.
     */
    char *q = b->g;
    while (q != b->a && *(q - 1) != '\n') --q;
    return b->g - q;
}

int up_line(struct buffer *b, size_t mult)
{
    /* Moves the cursor up mult lines */
    char *func = "up_line";
    size_t orig_coli, eol_coli;
    char *q;
    orig_coli = col_index(b); /* Get the original column index */
    q = b->g - orig_coli; /* Jump to start of the line */
    /*
     * Move up mult lines, will stop at the end of the line.
     * Please note that mult is not allowed to be zero.
     */
    while (mult && q != b->a) if (*--q == '\n') --mult;
    if (mult) {
        /* Cannot move up enough */
        LOG("out of bounds");
        return 1;
    }
    /*
     * Physically move the cursor in the memory up mult lines,
     * so that col_index can be called.
     */
    move_left(b, b->g - q);
    /* Get the column index at the end of the new line */
    eol_coli = col_index(b);
    /* Move back along the line to same column index, if possible */
    if (eol_coli > orig_coli) q -= eol_coli - orig_coli;
    move_left(b, b->g - q);
    return 0;
}

int down_line(struct buffer *b, size_t mult)
{
    /* Moves the cursor down mult lines */
    char *func = "down_line";
    size_t coli = col_index(b); /* Get the existing column index */
    char *q = b->c;
    /* Move down mult lines */
    while (mult && q != b->e) if (*q++ == '\n') --mult;
    if (mult) {
        /* Cannot move down far enough */
        LOG("out of bounds");
        return 1;
    }
    /* Move forward along the to the original column index, if possible */
    while (coli-- && q != b->e && *q != '\n') ++q;
    /* Physically move the cursor in the memory */
    move_right(b, q - b->c);
    return 0;
}

int match_brace(struct buffer *b)
{
    /* Moves the cursor to the matching brace */
    char *func = "match_brace";
    size_t depth = 1; /* Depth inside nested braces */
    char orig = *b->c, target, *q;
    int right; /* Movement direction */
    if (b->c == b->e) return 0; /* Cannot match EOBCH */
    switch (orig) {
        case '(': target = ')'; right = 1; break;
        case '<': target = '>'; right = 1; break;
        case '[': target = ']'; right = 1; break;
        case '{': target = '}'; right = 1; break;
        case ')': target = '('; right = 0; break;
        case '>': target = '<'; right = 0; break;
        case ']': target = '['; right = 0; break;
        case '}': target = '{'; right = 0; break;
        default: return 0; /* Nothing to match */
    }
    if (right) {
        q = b->c + 1;
        while (q != b->e) {
            if (*q == target) --depth;
            else if (*q == orig) ++depth;
            if (!depth) break;
            ++q;
        }
        if (!depth) {
            move_right(b, q - b->c);
            return 0;
        }
        LOG("match not found");
        return 1;
    }
    /* Left */
    if (b->g == b->a) {
        LOG("cannot move left");
        return 1;
    }
    q = b->g - 1;
    while (1) {
        if (*q == target) --depth;
        else if (*q == orig) ++depth;
        if (!depth || q == b->a) break;
        --q;
    }
    if (!depth) {
        move_left(b, b->g - q);
        return 0;
    }
    LOG("match not found");
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
    int nl_enc = 0; /* Trailing newline character has been encountered */
    int at_eol = 0; /* At end of line */
    end_of_buffer(b);
    if (b->g == b->a) return; /* Empty buffer */
    LCH(); /* Move to the left of the EOBCH, as this cannot be deleted */
    /*
     * Process the end of the file up until the first graph character, that is
     * the trailing characters at the end of the file. This is done backwards
     * starting from the end of the buffer. The first newline character
     * encountered will be preserved.
     */
    while (!isgraph((unsigned char) *b->c)) {
        if (!nl_enc && *b->c == '\n') nl_enc = 1;
        else DCH();
        if (b->g == b->a) break; /* Start of buffer */
        else LCH(); /* Move left */
    }
    /*
     * Process the rest of the file, keeping track if the cursor is at the end
     * of the line (in which case whitespace is trimmed too).
     */
    while (1) {
        if (*b->c == '\n') at_eol = 1;
        else if (isgraph((unsigned char) *b->c)) at_eol = 0;
        else if (at_eol) DCH();
        else if (*b->c != ' ' && *b->c != '\t') DCH();
        if (b->g == b->a) break; /* Start of buffer */
        else LCH(); /* Move left */
    }
    b->m_set = 0;
}

void set_bad(size_t *bad, struct mem *se)
{
    /* Sets the bad character table for the Quick Search algorithm */
    unsigned char *pat = (unsigned char *) se->p; /* Search pattern */
    size_t i;
    for (i = 0; i <= UCHAR_MAX; ++i) *(bad + i) = se->u + 1;
    for (i = 0; i < se->u; ++i) *(bad + *(pat + i)) = se->u - i;
}

int search(struct buffer *b, struct mem *se, size_t *bad)
{
    /*
     * Forward search, excludes cursor and EOBCH.
     * Uses the Quick Search algorithm.
     */
    char *func = "search";
    char *q, *stop, *q_copy, *pat;
    size_t patlen;
    int found = 0;
    size_t s;
    if (!se->u) {
        LOG("empty search mem");
        return 1;
    }
    if (b->e - b->c <= 1) {
        LOG("no buffer text after cursor");
        return 1;
    }
    s = b->e - b->c - 1;
    if (se->u > s) {
        LOG("search mem larger than buffer text after cursor");
        return 1;
    }
    if (se->u == 1) {
        /* Single character patterns */
        q = memchr(b->c + 1, *se->p, s);
        if (q == NULL) {
            LOG("not found");
            return 1;
        }
    } else {
        /* Quick Search algorithm */
        q = b->c + 1;
        stop = b->e - se->u; /* Inclusive */
        while (q <= stop) {
            q_copy = q;
            pat = se->p;
            patlen = se->u;
            /* Compare pattern to text */
            do {
                if (*q_copy++ != *pat++) break;
            } while (--patlen);
            /* Match found */
            if (!patlen) {
                found = 1;
                break;
            }
            /* Jump using the bad character table */
            q += *(bad + (unsigned char) *(q + se->u));
        }
        if (!found) {
            LOG("not found");
            return 1;
        }
    }
    move_right(b, q - b->c); /* Must be in bounds */
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
    char *func = "grow_gap";
    size_t rg, min_increase, current_size, target_size, increase;
    char *t, *tc;
    SAFEADD(rg, req, GAP, return 1);
    /* Only call grow_gap if req > (b->c - b->g) */
    min_increase = rg - (b->c - b->g);
    current_size = b->e - b->a + 1;
    increase = current_size > min_increase ? current_size : min_increase;
    SAFEADD(target_size, current_size, increase, return 1);
    LMALLOC(t, target_size, return 1);
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
    char *func = "insert_char";
    if (mult > (size_t) (b->c - b->g) && grow_gap(b, mult)) {
        LOG("grow_gap failed");
        return 1;
    }
    memset(b->g, ch, mult);
    b->g += mult;
    b->m_set = 0;
    return 0;
}

int delete_char(struct buffer *b, size_t mult)
{
    /* Deletes mult chars by expanding the gap rightwards */
    char *func = "delete_char";
    if (mult > (size_t) (b->e - b->c)) {
        LOG("out of bounds");
        return 1;
    }
    b->c += mult;
    b->m_set = 0;
    return 0;
}

int backspace_char(struct buffer *b, size_t mult)
{
    /* Backspaces mult chars by expanding the gap leftwards */
    char *func = "backspace_char";
    if (mult > (size_t) (b->g - b->a)) {
        LOG("out of bounds");
        return 1;
    }
    b->g -= mult;
    b->m_set = 0;
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
    char *func = "init_buffer";
    struct buffer *b;
    size_t rg;
    SAFEADD(rg, req, GAP, return NULL);
    LMALLOC(b, sizeof(struct buffer), return NULL);
    LMALLOC(b->a, rg, goto fail1);
    b->fn = NULL;
    b->g = b->a;
    *(b->e = b->a + rg - 1) = EOBCH;
    b->c = b->e;
    b->d = 0;
    b->m = 0;
    b->m_set = 0;
    return b;
fail1:
    free(b);
    return NULL;
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
    char *func = "init_tb";
    struct tb *z;
    size_t n, s;
    SAFEADD(n, req, SPARETB, return NULL);
    SAFEMULT(s, n, sizeof(struct buffer *), return NULL);
    LMALLOC(z, sizeof(struct tb), return NULL);
    LMALLOC(z->z, s, goto fail1);
    z->n = n;
    z->u = 0;
    z->a = 0;
    return z;
fail1:
    free(z);
    return NULL;
}

void free_tb(struct tb *z)
{
    size_t i;
    if (z != NULL) {
        for (i = 0; i < z->u; ++i) free_buffer(*(z->z + i));
        free(z->z);
        free(z);
    }
}

struct mem *init_mem(void)
{
    char *func = "init_mem";
    struct mem *p;
    LMALLOC(p, sizeof(struct mem), return NULL);
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

int get_file_size(char *fn, size_t *fs)
{
    /* Gets the file size for regular files */
    char *func = "get_file_size";
    struct stat st;
    errno = 0;
    if (stat(fn, &st)) {
        LOGE("stat failed");
        return 1;
    }
    if (!((st.st_mode & S_IFMT) == S_IFREG)) {
        LOG("not a regular file");
        return 1;
    }
    if (st.st_size < 0) {
        LOG("negative file size");
        return 1;
    }
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
    char *func = "insert_file";
    size_t fs;
    FILE *fp;
    if (get_file_size(fn, &fs)) {
        LOG("get_file_size failed");
        return 1;
    }
    if (!fs) return 0;
    if (fs > (size_t) (b->c - b->g) && grow_gap(b, fs)) {
        LOG("grow_gap failed");
        return 1;
    }
    LFOPEN(fp, fn, "rb", return 1);
    LFREAD(b->c - fs, fs, fp, goto fail1);
    LFCLOSE(fp, return 1);
    b->c -= fs;
    b->m_set = 0;
    return 0;
fail1:
    LFCLOSE(fp, return 1);
    return 1;
}

int replace_region(struct buffer *b, char *fn)
{
    /*
     * Replaces the region with a file. The file is read into memory before
     * the old region is deleted, so that the operation is transactional.
     * The inserted text will be marked as the new region.
     * grow_gap does not change the mark.
     */
    char *func = "replace_region";
    size_t ci = b->g - b->a; /* Cursor index */
    size_t reg_s = b->m < ci ? ci - b->m : b->m - ci; /* Region size */
    size_t fs;
    char *t;
    FILE *fp;
    if (!b->m_set) {
        LOG("no region");
        return 1;
    }
    if (get_file_size(fn, &fs)) {
        LOG("get_file_size failed");
        return 1;
    }
    if (!fs) return 0;
    /* Get temporary memory */
    LMALLOC(t, fs, return 1);
    LFOPEN(fp, fn, "rb", goto fail1);
    LFREAD(t, fs, fp, goto fail2);
    LFCLOSE(fp, goto fail1);
    /* The current region will be recycled */
    if (fs > reg_s && fs - reg_s > (size_t) (b->c - b->g)
        && grow_gap(b, fs - reg_s)) {
        LOG("grow_gap failed");
        goto fail1;
    }
    /* Cursor at end of the region */
    if (b->m < ci) {
        backspace_char(b, ci - b->m); /* Will not be out of bounds */
        memcpy(b->g, t, fs); /* Copy memory to left-hand side of gap */
        b->g += fs;
    } else {
        delete_char(b, b->m - ci); /* Will not be out of bounds */
        memcpy(b->c - fs, t, fs); /* Copy memory to right-hand side of gap */
        b->c -= fs;
    }
    /* backspace_char and delete_char unset the mark, so set it again */
    b->m_set = 1;
    free(t);
    return 0;
fail2:
    LFCLOSE(fp, goto fail1);
fail1:
    free(t);
    return 1;
}

int write_buffer(struct buffer *b, char *fn, int backup_req)
{
    /*
     * Writes a buffer to file. If the file already exists, then it will be
     * renamed to have a '~' suffix (to serve as a backup).
     */
    char *func = "write_buffer";
    struct stat st;
    int backup_ok = 0;
    size_t len, bk_s, num;
    char *backup_fn;
    FILE *fp;
    if (fn == NULL) return 1;
    /* Create backup of the regular file */
    if (backup_req && !stat(fn, &st) && st.st_mode & S_IFREG) {
        len = strlen(fn);
        SAFEADD(bk_s, len, 2, return 1);
        LMALLOC(backup_fn, bk_s, return 1);
        memcpy(backup_fn, fn, len);
        *(backup_fn + len) = '~';
        *(backup_fn + len + 1) = '\0';
#ifdef _WIN32
        if (!MoveFileEx(fn, backup_fn, MOVEFILE_REPLACE_EXISTING)) {
            LOG("MoveFileEx failed");
            free(backup_fn);
            return 1;
        }
#else
        errno = 0;
        if (rename(fn, backup_fn)) {
            LOGE("rename failed");
            free(backup_fn);
            return 1;
        }
#endif
        free(backup_fn);
        backup_ok = 1;
    }
    LFOPEN(fp, fn, "wb", return 1);
    num = (size_t) (b->g - b->a);
    LFWRITE(b->a, num, fp, goto fail1);
    num = (size_t) (b->e - b->c);
    LFWRITE(b->c, num, fp, goto fail1);
    LFCLOSE(fp, return 1);
#ifndef _WIN32
    if (backup_ok && chmod(fn, st.st_mode & 0777)) {
        LOG("chmod failed");
        return 1;
    }
#endif
    return 0;
fail1:
    LFCLOSE(fp, return 1);
    return 1;
}

int rename_buffer(struct buffer *b, char *new_name)
{
    /* Sets or changes the filename associated with a buffer */
    char *func = "rename_buffer";
    size_t len, s;
    char *t;
    if (new_name == NULL) {
        LOG("new name is NULL");
        return 1;
    }
    len = strlen(new_name);
    if (!len) {
        LOG("new name is an empty string");
        return 1;
    }
    SAFEADD(s, len, 1, return 1);
    LMALLOC(t, s, return 1);
    memcpy(t, new_name, len);
    *(t + len) = '\0';
    free(b->fn);
    b->fn = t;
    return 0;
}

int buffer_to_str(struct buffer *b, char **p_to_str)
{
    char *func = "buffer_to_str";
    char *t, *q, ch, *p;
    /* OK to add one because of EOBCH */
    size_t s = b->g - b->a + b->e - b->c + 1;
    LMALLOC(t, s, return 1);
    p = t;
    /* Strip out \0 chars */
    q = b->a;
    while (q != b->g) if ((ch = *q++) != '\0') *p++ = ch;
    q = b->c;
    while (q != b->e) if ((ch = *q++) != '\0') *p++ = ch;
    *p = '\0';
    free(*p_to_str);
    *p_to_str = t;
    return 0;
}

int buffer_to_mem(struct buffer *b, struct mem *m)
{
    char *func = "buffer_to_mem";
    size_t s_bg = b->g - b->a; /* Size before gap */
    size_t s_ag = b->e - b->c; /* Size after gap */
    size_t s = s_bg + s_ag;    /* Text size of buffer */
    char *t;                   /* Temporary pointer */
    if (s > m->s) {
        LMALLOC(t, s, return 1);
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

int write_region(struct buffer *b, char *fn)
{
    /* Writes the region to file */
    char *func = "write_region";
    FILE *fp;
    size_t num;
    size_t ci = b->g - b->a; /* Cursor index */
    if (!b->m_set) {
        LOG("no region");
        return 1;
    }
    if (b->m == ci) return 0; /* Empty region, nothing to do */
    LFOPEN(fp, fn, "wb", return 1);
    /* Mark before cursor */
    if (b->m < ci) {
        num = (size_t) (b->g - b->a) - b->m;
        LFWRITE(b->a + b->m, num, fp, goto fail1);
    } else {
        num = b->m - (size_t) (b->c - b->a);
        LFWRITE(b->c, num, fp, goto fail1);
    }
    LFCLOSE(fp, return 1);
    return 0;
fail1:
    LFCLOSE(fp, return 1);
    return 1;
}

int copy_region(struct buffer *b, struct mem *p, int del)
{
    char *func = "copy_region";
    size_t ci = b->g - b->a; /* Cursor index */
    size_t s;                /* Size of region */
    char *t;                 /* Temporary pointer */
    if (!b->m_set) {
        LOG("no region");
        return 1;
    }
    if (b->m == ci) return 0;
    if (b->m < ci) {
        s = ci - b->m;
        if (s > p->s) {
            LMALLOC(t, s, return 1);
            free(p->p);
            p->p = t;
            p->s = s;
        }
        memcpy(p->p, b->a + b->m, s);
        p->u = s;
        if (del) b->g -= s;
    } else {
        s = b->m - ci;
        if (s > p->s) {
            LMALLOC(t, s, return 1);
            free(p->p);
            p->p = t;
            p->s = s;
        }
        memcpy(p->p, b->c, s);
        p->u = s;
        if (del) b->c += s;
    }
    b->m_set = 0;
    return 0;
}

int paste(struct buffer *b, struct mem *p, size_t mult)
{
    char *func = "paste";
    size_t s;
    SAFEMULT(s, p->u, mult, return 1);
    if (s > (size_t) (b->c - b->g) && grow_gap(b, s)) {
        LOG("grow_gap failed");
        return 1;
    }
    while (mult--) {
        memcpy(b->g, p->p, p->u);
        b->g += p->u;
    }
    b->m_set = 0;
    return 0;
}

int cut_to_eol(struct buffer *b, struct mem *p)
{
    char *func = "cut_to_eol";
    if (*b->c == '\n') return delete_char(b, 1);
    set_mark(b);
    end_of_line(b);
    if (copy_region(b, p, 1)) {
        LOG("copy_region failed");
        return 1;
    }
    return 0;
}

int cut_to_sol(struct buffer *b, struct mem *p)
{
    char *func = "cut_to_sol";
    set_mark(b);
    start_of_line(b);
    if (copy_region(b, p, 1)) {
        LOG("copy_region failed");
        return 1;
    }
    return 0;
}

int sys_cmd(char *cmd)
{
    /* Executes a system command */
    char *func = "sys_cmd";
    int r;
    errno = 0;
    if ((r = system(cmd)) == -1) {
        LOGE("system failed");
        return 1;
    }
#ifdef _WIN32
    if (!r) return 0;
#else
    if (WIFEXITED(r) && !WEXITSTATUS(r)) return 0;
#endif
    LOG("system child failed");
    return 1;
}

#ifdef _WIN32
int setup_graphics(void)
{
    /* Turn on interpretation of VT100-like escape sequences */
    char *func = "setup_graphics";
    HANDLE out;
    DWORD mode;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE) {
        LOG("GetStdHandle failed");
        return 1;
    }
    if (!GetConsoleMode(out, &mode)) {
        LOG("GetConsoleMode failed");
        return 1;
    }
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        LOG("SetConsoleMode failed");
        return 1;
    }
    return 0;
}
#endif

int get_screen_size(size_t *height, size_t *width)
{
    /* Gets the screen size */
    char *func = "get_screen_size";
#ifdef _WIN32
    HANDLE out;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE) {
        LOG("GetStdHandle failed");
        return 1;
    }
    if(!GetConsoleScreenBufferInfo(out, &info)) {
        LOG("GetConsoleScreenBufferInfo failed");
        return 1;
    }
    *height = info.srWindow.Bottom - info.srWindow.Top + 1;
    *width = info.srWindow.Right - info.srWindow.Left + 1;
    return 0;
#else
    struct winsize ws;
    errno = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        LOGE("ioctl failed");
        return 1;
    }
    *height = ws.ws_row;
    *width = ws.ws_col;
    return 0;
#endif
}

void put_z(size_t a)
{
    /* Print a size_t value to stdout */
    size_t b = a, m = 1;
    while (b /= 10) m *= 10;
    do {
        putchar(a / m + '0');
        a %= m;
    } while (m /= 10);
}

void move_cursor(size_t y, size_t x)
{
    /* Move the graphical cursor on the screen */
    printf("\033[");
    put_z(y);
    putchar(';');
    put_z(x);
    putchar('H');
}

void reverse_scan(struct buffer *b, size_t th, size_t w, int centre)
{
    /*
     * This is the most complicated function in the text editor.
     * It does a reverse scan to see if the cursor would be on the screen.
     * If the cursor would be off the screen, or if the user requested to
     * centre, then draw start will be set so that the cursor will end up
     * on the middle line.
     */

    size_t ci = b->g - b->a;          /* Cursor index */
    size_t ta = th * w;               /* Text screen area */
    size_t hth = th > 2 ? th / 2 : 1; /* Half the text screen height */
    size_t hta = hth * w;             /* Half the text screen area */
    size_t target_h;                  /* The height to stop scanning */
    size_t height_count = 0;          /* Keeping track of the height while scanning */
    /*
     * Keeping track of the width while scanning.
     * Start from one to count the cursor.
     */
    size_t width_count = 1;
    char *q;                       /* Pointer used for the reverse scan */
    char *q_stop;                  /* Pointer where the reverse scan is to stop (inclusive) */
    char *q_d = b->a + b->d;       /* Current draw start pointer */
    /*
     * Pointer to the draw start postion that results in the cursor being centred.
     * Only used when centreing is not planned.
     */
    char *q_centre = NULL;

    /*
     * If the cursor is at the start of the buffer, then must
     * draw start from the start of the buffer.
     */
    if (b->g == b->a) {
        b->d = 0;
        return;
    }

     /*
      * Test to see if cursor is definitely on screen.
      * If the user has not requested to centre, then no action is required.
      */
     if (ci >= b->d && ci - b->d < th && !centre) return;

     /*
      * Test to see if cursor is definitely off the screen.
      * In this case we must centre.
      */
     if (ci < b->d || ci - b->d >= ta) centre = 1;

    /*
     * Set the stopping postion to limit the reverse scan as much as possible,
     * and to reduce the number of checks that must be done each loop iteration.
     */
    if (centre) {
        /*
         * Make sure we don't over shoot the start of the buffer.
         * Minus one to account for the cursor.
         */
        if (ci < hta - 1) q_stop = b->a;
        else q_stop = b->g - (hta - 1);
        target_h = hth;
    } else {
        if (ci < ta - 1) q_stop = b->a;
        else q_stop = b->g - (ta - 1);
        /* Don't go past current draw start */
        if (q_stop < q_d) q_stop = q_d;
        target_h = th;
    }

    /* Process cursor if the screen is one wide */
    if (w == 1) {
        width_count = 0;
        ++height_count;
        /* If the text area is one by one, then draw from the cursor */
        if (target_h == 1) {
            b->d = ci;
            return;
        }
    }

    q = b->g - 1; /* Start from the character to the left of the gap */

    while (1) {
        /*
         * Process char:
         * Test for full line and test if the target height has been reached.
         */
        if (*q == '\n' || ++width_count > w) {
            width_count = 0;
            ++height_count;
            /* Record draw start position for centreing */
            if (height_count == hth) {
                q_centre = q;
                /* Increase if stopped on a new line character */
                if (*q_centre == '\n') ++q_centre;
            }
            /*
             * Stop when target height has been reached.
             * This is to prevent unnecessary scanning.
             * In this sitution q_centre will be used.
             */
            if (height_count == target_h) {
                q = q_centre;
                break;
            }
        }

        /* To avoid decrementing to in front of memory allocation */
        if (q == q_stop) break;
        --q;
    }

    /* Set draw start */
    b->d = q - b->a;
}

void draw_screen(struct buffer *b, struct buffer *cl, int cla, int cr, size_t h,
    size_t w, char *ns, size_t *cy, size_t *cx, int centre)
{
    /* Virtually draw screen (and clear unused sections on the go) */
    char *q, ch;
    unsigned char u, w_part, r_part; /* For hex calculation */
    size_t v;                        /* Virtual screen index */
    size_t j;                        /* Filename character index */
    size_t len;                      /* Used for printing the filename */
    /* Height of text buffer portion of the screen */
    size_t th = h > 2 ? h - 2 : 1;

    /* Text buffer */
    reverse_scan(b, th, w, centre);

    v = 0;
    q = b->a + b->d;
    /* Print before the gap */
    while (q != b->g) {
        ch = *q++;
        /*
         * When a newline character is encountered, complete the screen
         * line with spaces. The modulus will be zero at the start of
         * the next screen line. The "do" makes it work when the '\n'
         * is the first character on a screen line.
         */
        if (ch == '\n')
            do {
                *(ns + v++) = ' ';
            } while (v % w);
        else
            *(ns + v++) = isgraph((unsigned char) ch) || ch == ' ' ? ch : '?';
    }
    /* Record cursor's screen location */
    *cy = v / w;
    *cx = v % w;
    q = b->c;
    /* Print after the gap */
    do {
        ch = *q;
        if (ch == '\n')
            do {
                *(ns + v++) = ' ';
            } while (v % w);
        else
            *(ns + v++) = isgraph((unsigned char) ch) || ch == ' ' ? ch : '?';
        /* Stop if have reached the status bar, before printing there */
        if (v / w == th) break;
    } while (q++ != b->e);

    /* Fill in unused text region with spaces */
    while (v / w != th) *(ns + v++) = ' ';

    /* Stop if screen is only one row high */
    if (h == 1) return;

    /*
     * Status bar:
     * Print indicator if last command failed.
     */
    *(ns + v++) = cr ? '!' : ' ';

    /* Print hex of char under the cursor */
    if (w >= 3) {
        u = cla ? (unsigned char) *cl->c : (unsigned char) *b->c;
        w_part = u / 16; /* Whole part */
        r_part = u % 16; /* Remainder part */
        *(ns + v++) = w_part >= 10 ? w_part - 10 + 'A' : w_part + '0';
        *(ns + v++) = r_part >= 10 ? r_part - 10 + 'A' : r_part + '0';
    }

    /* Blank space */
    if (w >= 4) {
        *(ns + v++) = ' ';
    }

    /* Print filename */
    if (w > 4 && b->fn != NULL) {
        len = strlen(b->fn);
        /* Truncate filename if screen is not wide enough */
        len = len < w - 4 ? len : w - 4;
        /* Print file name in status bar */
        for (j = 0; j < len; ++j) *(ns + v++) = *(b->fn + j);
    }
    /* Complete status bar with spaces */
    while (v / w != h - 1) *(ns + v++) = ' ';

    /* Stop if screen is only two rows high */
    if (h == 2) return;

    /* Command line buffer */
    reverse_scan(cl, 1, w, 0);
    q = cl->a + cl->d;
    /* Print before the gap */
    while (q != cl->g) {
        ch = *q++;
        if (ch == '\n')
            do {
                *(ns + v++) = ' ';
            } while (v % w);
        else
            *(ns + v++) = isgraph((unsigned char) ch) || ch == ' ' ? ch : '?';
    }
    /* If the command line is active, record cursor's screen location */
    if (cla) {
        *cy = v / w;
        *cx = v % w;
    }
    q = cl->c;
    /* Print after the gap */
    while (1) {
        ch = *q;
        if (ch == '\n')
            do {
                *(ns + v++) = ' ';
            } while (v % w);
        else
            *(ns + v++) = isgraph((unsigned char) ch) || ch == ' ' ? ch : '?';
        /* Stop if off the bottom of the screen, before printing there */
        if (v / w == h) break;
        if (q == cl->e) break;
        ++q;
    }
    /* Fill in unused text region with spaces */
    while (v / w != h) *(ns + v++) = ' ';
}

void diff_draw(char *ns, char *cs, size_t sa, size_t w)
{
    /* Physically draw the screen where the virtual screens differ */
    size_t v;       /* Index */
    int in_pos = 0; /* If in position for printing (no move is required) */
    char ch;
    for (v = 0; v < sa; ++v) {
        if ((ch = *(ns + v)) != *(cs + v)) {
            if (!in_pos) {
                /* Top left corner is (1, 1) not (0, 0) so need to add one */
                move_cursor(v / w + 1, v - (v / w) * w + 1);
                in_pos = 1;
            }
            putchar(ch);
        } else {
            in_pos = 0;
        }
    }
}

int new_buffer(struct tb *z, char *fn)
{
    char *func = "new_buffer";
    struct stat st;
    size_t new_n, new_s;
    struct buffer **t;
    struct buffer *b;  /* Buffer shortcut */
    /* Grow to take more text buffers */
    if(z->u == z->n) {
        SAFEADD(new_n, z->u, SPARETB, return 1);
        SAFEMULT(new_s, new_n, sizeof(struct buffer *), return 1);
        errno = 0;
        if ((t = realloc(z->z, new_s)) == NULL) {
            LOGE("realloc failed");
            return 1;
        }
        z->z = t;
        z->n = new_n;
    }
    b = *(z->z + z->u); /* Create shortcut */
    if (fn != NULL && !stat(fn, &st)) {
        /* File exists */
        if (!((st.st_mode & S_IFMT) == S_IFREG)) {
            LOG("not a regular file");
            return 1;
        }
        if (st.st_size < 0) {
            LOG("negative file size");
            return 1;
        }
        if ((b = init_buffer((size_t) st.st_size)) == NULL) {
            LOG("init_buffer failed");
            return 1;
        }
        if (rename_buffer(b, fn)) {
            LOG("rename_buffer failed");
            free_buffer(b);
            return 1;
        }
        if (insert_file(b, fn)) {
            LOG("insert_file failed");
            free_buffer(b);
            return 1;
        }
    } else {
        /* New file */
        if ((b = init_buffer(0)) == NULL) {
            LOG("init_buffer failed");
            return 1;
        }
        if (fn != NULL && rename_buffer(b, fn)) {
            LOG("rename_buffer failed");
            free_buffer(b);
            return 1;
        }
    }
    /* Success */
    *(z->z + z->u) = b; /* Link back */
    z->a = z->u; /* Make the active text buffer the new text buffer */
    ++z->u;      /* Increase the number of used text buffers */
    return 0;
}

char *make_temp_file(char *template)
{
    char *func = "make_temp_file";
    char *t;
    size_t len = strlen(template), s;
#ifdef _WIN32
    HANDLE fh;
#else
    int fd;
#endif
    SAFEADD(s, len, 1, return NULL);
    LMALLOC(t, s, return NULL);
    memcpy(t, template, len);
    *(t + len) = '\0';

#ifdef _WIN32
    errno = 0;
    if (_mktemp_s(t, len + 1)) {
        LOGE("_mktemp_s failed");
        return NULL;
    }
    if ((fh = CreateFile(t, GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) return NULL;
    if (!CloseHandle(fh)) return NULL;
#else
    errno = 0;
    if ((fd = mkstemp(t)) == -1) {
        LOGE("mkstemp failed");
        return NULL;
    }
    errno = 0;
    if (close(fd)) {
        LOGE("close failed");
        return NULL;
    }
#endif
    return t;
}

int main(int argc, char **argv)
{
    char *func = "main";      /* Function name */
    int ret = 0;              /* Editor's return value */
    int running = 1;          /* Editor is running */
    size_t h = 0, w = 0;      /* Screen height and width (real) */
    size_t new_h, new_w;      /* New screen height and width (real) */
    size_t cy, cx;            /* Cursor's screen coordinates */
    struct tb *z = NULL;      /* The text buffers */
    struct buffer *cl = NULL; /* Command line buffer */
    int cla = 0;              /* Command line buffer is active */
    /* Operation for which the command line is being used */
    char operation = '\0', operation_copy = '\0';
    char *cl_str = NULL;      /* Command line buffer converted to a string */
    /* Bad character table for the Quick Search algorithm */
    size_t bad[UCHAR_MAX + 1];
    struct mem *se = NULL;    /* Search memory */
    struct mem *p = NULL;     /* Paste memory */
    int cr = 0;               /* Command return value */
    int centre = 0;           /* Request to centre the cursor */
    int redraw = 0;           /* Request to redraw the entire screen */
    struct buffer *cb;        /* Shortcut to the cursor's buffer */
    char *ns = NULL;          /* Next screen (virtual) */
    char *cs = NULL;          /* Current screen (virtual) */
    size_t ss = 0;            /* Screen size (virtual) */
    size_t sa = 0;            /* Terminal screen area (real) */
    /* Keyboard key (one physical key can send multiple) */
    int key;
    int digit;                /* Numerical digit */
    unsigned char hex[2];     /* Hexadecimal array */
    int term_in = 0;          /* Terminal input */
    size_t mult;              /* Command multiplier (cannot be zero) */
    char *logfn = NULL;       /* Log filename */
    char *clfn = NULL;        /* Command line filename used as sed script */
    char *infn = NULL;        /* Sed input filename */
    char *outfn = NULL;       /* Sed output filename */
    char *errfn = NULL;       /* Sed error filename */
    char sedcmd[SEDMAXCMD];      /* Sed command */
    int regex_ok = 0;         /* Last regex completed successfully */
    char *t;                  /* Temporary pointer */
    size_t i;                 /* Generic index */
#ifndef _WIN32
    struct termios term_orig, term_new;
#endif

    /* Create log file */
    if ((logfn = make_temp_file(LOGTEMPLATE)) == NULL)
        QUIT("cannot create log file");
    /* Open the log file */
    errno = 0;
    if ((logfp = fopen(logfn, "wb")) == NULL)
        QUITE("cannot open log file");

    /* Create files for sed */
    if ((clfn = make_temp_file(CLTEMPLATE)) == NULL)
        QUIT("failed to make command line file for sed script");
    if ((infn = make_temp_file(INTEMPLATE)) == NULL)
        QUIT("failed to make sed input file");
    if ((outfn = make_temp_file(OUTTEMPLATE)) == NULL)
        QUIT("failed to make sed output file");
    if ((errfn = make_temp_file(ERRTEMPLATE)) == NULL)
        QUIT("failed to make sed error file");

    /* Create sed command */
    snprintf(sedcmd, SEDMAXCMD, SEDFORMAT, SEDSTR, clfn, infn, outfn, errfn);

    /* Ignore interrupt, sent by ^C */
/*    errno = 0;
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) QUITE("cannot ignore SIGINT"); */

    /* See if input is from a terminal */
#ifdef _WIN32
    if (_isatty(_fileno(stdin))) term_in = 1;
#else
    if (isatty(STDIN_FILENO)) term_in = 1;
#endif

#ifndef _WIN32
    if (term_in) {
        /* Change terminal input to raw and no echo */
        errno = 0;
        if (tcgetattr(STDIN_FILENO, &term_orig))
            QUITE("cannot get terminal attributes");

        term_new = term_orig;
        cfmakeraw(&term_new);

        errno = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &term_new))
            QUITE("cannot set terminal attributes");
    }
#endif

    /* Process command line arguments */
    if (argc > 1) {
        if ((z = init_tb(argc - 1)) == NULL)
            QUIT("failed to init text buffers");
        /* Load files into buffers */
        for (i = 0; i < (size_t) (argc - 1); ++i)
            if (new_buffer(z, *(argv + i + 1)))
                QUIT("failed to create new buffer");
        z->a = 0; /* Go back to first buffer */
    } else {
        if ((z = init_tb(1)) == NULL) QUIT("failed to init text buffers");
        /* Start empty buffer */
        if (new_buffer(z, NULL)) QUIT("failed to create new buffer");
    }

    /* Initialise command line buffer */
    if ((cl = init_buffer(0)) == NULL)
        QUIT("failed to init command line buffer");
    /* Initialise search memory */
    if ((se = init_mem()) == NULL) QUIT("failed to init search memory");
    /* Initialise paste memory */
    if ((p = init_mem()) == NULL) QUIT("failed to init paste memory");

#ifdef _WIN32
    setup_graphics();
#endif

    /* Editor loop */
    while (running) {

    /* Top of the editor loop */
top:
        if (get_screen_size(&new_h, &new_w)) QUIT("failed to get screen size");

        /* Do graphics only if screen is big enough */
        if (new_h >= 1 && new_w >= 1) {
            /* Requested redraw or change in screen dimensions */
            if (redraw || new_h != h || new_w != w) {
                h = new_h;
                w = new_w;
                if (h > INT_MAX / w) QUIT("integer overflow");
                sa = h * w;
                /* Bigger screen */
                if (ss < sa) {
                    free(ns);
                    free(cs);
                    errno = 0;
                    if ((t = malloc(sa)) == NULL) QUITE("malloc failed");
                    ns = t;
                    errno = 0;
                    if ((t = malloc(sa)) == NULL) QUITE("malloc failed");
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

            draw_screen(*(z->z + z->a), cl, cla, cr, h, w, ns, &cy, &cx, centre);
            centre = 0; /* Clear centre request */
            diff_draw(ns, cs, sa, w);
            /* Top left corner is (1, 1) not (0, 0) so need to add one */
            move_cursor(cy + 1, cx + 1);
            /* Swap virtual screens */
            t = cs;
            cs = ns;
            ns = t;
        }

        cr = 0; /* Reset command return value */

        /* Shortcut to the cursor's buffer */
        cb = cla ? cl : *(z->z + z->a);

        mult = 1; /* Reset command multiplier */
        if ((key = GETCH()) == CMDMULT) {
            /* Read multiplier number */
            mult = 0;
            key = GETCH();
            while (isdigit(key)) {
                if (MOF(mult, 10)) {
                    LOG("command multiplier: size_t multiplication overflow");
                    cr = 1;
                    goto top;
                }
                mult *= 10;
                digit = key - '0';
                if (AOF(mult, digit)) {
                    LOG("command multiplier: size_t addition overflow");
                    cr = 1;
                    goto top;
                }
                mult += digit;
                key = GETCH();
            }
        }

        /* Remap special keyboard keys */
#ifdef _WIN32
        if (key == 0xE0) {
            key = GETCH();
            switch(key) {
            case 'H': key = UP; break;
            case 'P': key = DOWN; break;
            case 'K': key = LEFT; break;
            case 'M': key = RIGHT; break;
            case 'S': key = DEL; break;
            case 'G': key = HOME; break;
            case 'O': key = ENDLINE; break;
            }
        }
#endif

        if (key == ESC) {
            key = GETCH();
            switch (key) {
            case STARTBUF: start_of_buffer(cb); goto top;
            case ENDBUF: end_of_buffer(cb); goto top;
            case REPSEARCH: cr = search(cb, se, bad); goto top;
            case MATCHBRACE: cr = match_brace(cb); goto top;
            case COPY: cr = copy_region(cb, p, 0); goto top;
            case REDRAW: redraw = 1; goto top;
            case REGEXREG: delete_buffer(cl); cla = 1; operation = 'X'; goto top;
            case UNDOREGEX: if (regex_ok) cr = replace_region(cb, infn); goto top;
#ifndef _WIN32
            case '[':
                key = GETCH();
                switch(key) {
                case 'A': key = UP; break;
                case 'B': key = DOWN; break;
                case 'D': key = LEFT; break;
                case 'C': key = RIGHT; break;
                case '3': if ((key = GETCH()) == '~') key = DEL; break;
                case 'H': key = HOME; break;
                case 'F': key = ENDLINE; break;
                }
                break;
#endif
            }
        }

        /* Remap Backspace key */
        if (key == 8 || key == 0x7F) key = BKSPACE;

        /* Remap carriage return */
        if (key == '\r') key = '\n';

        switch (key) {
        case LEFT: cr = move_left(cb, mult); goto top;
        case RIGHT: cr = move_right(cb, mult); goto top;
        case UP: cr = up_line(cb, mult); goto top;
        case DOWN: cr = down_line(cb, mult); goto top;
        case HOME: start_of_line(cb); goto top;
        case ENDLINE: end_of_line(cb); goto top;
        case DEL: cr = delete_char(cb, mult); goto top;
        case BKSPACE: cr = backspace_char(cb, mult); goto top;
        case TRIMCLEAN: trim_clean(cb); goto top;
        case SETMARK: set_mark(cb); goto top;
        case CUT: cr = copy_region(cb, p, 1); goto top;
        case PASTE: cr = paste(cb, p, mult); goto top;
        case CENTRE: centre = 1; goto top;
        case SEARCH: delete_buffer(cl); cla = 1; operation = 'S'; goto top;
        case CUTTOEOL:
            if (!mult) cr = cut_to_sol(cb, p);
            else cr = cut_to_eol(cb, p);
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
                    LOG("insert hex: invalid digit");
                    cr = 1;
                    goto top;
                }
            }
            cr = insert_char(cb, *hex * 16 + *(hex + 1), mult);
            goto top;
        }

        if (key == Cx) {
            key = GETCH();
            switch (key) {
            case CLOSE: running = 0; break;
            case SAVE: cr = write_buffer(cb, cb->fn, 1); goto top;
            case RENAME: delete_buffer(cl); cla = 1; operation = 'R'; goto top;
            case INSERTFILE: delete_buffer(cl); cla = 1; operation = 'I'; goto top;
            case NEWBUF: delete_buffer(cl); cla = 1; operation = 'N'; goto top;
            }
            /* Left and right buffer, respectively */
#ifdef _WIN32
            if (key == 0xE0) {
                key = GETCH();
                switch(key) {
                case 'K': {z->a ? --z->a : (cr = 1); goto top;}
                case 'M': {z->a != z->u - 1 ? ++z->a : (cr = 1); goto top;}
                }
            }
#else
            if (key == 0x1B && (key = GETCH()) == '[') {
                key = GETCH();
                switch(key) {
                case 'D': {z->a ? --z->a : (cr = 1); goto top;}
                case 'C': {z->a != z->u - 1 ? ++z->a : (cr = 1); goto top;}
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
            case 'X':
                regex_ok = 0; /* Clear as may fail */
                if (write_buffer(cl, clfn, 0)) {
                    cr = 1;
                    goto top;
                }
                if (write_region(*(z->z + z->a), infn)) {
                    cr = 1;
                    goto top;
                }
                if (sys_cmd(sedcmd)) {
                    cr = 1;
                    goto top;
                }
                if (!(cr = replace_region(*(z->z + z->a), outfn)))
                    regex_ok = 1;
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
    errno = 0;
    if (term_in && tcsetattr(STDIN_FILENO, TCSANOW, &term_orig)) {
        LOGE("failed to set original terminal attributes");
        ret = 1;
    }
#endif
    /*
     * Remove files for sed, without setting ret on failure.
     * No logging is performed if file does not exist.
     */
    RM(clfn);
    RM(infn);
    RM(outfn);
    RM(errfn);
    /* Free memory */
    free_buffer(cl);
    free_tb(z);
    free_mem(se);
    free_mem(p);
    free(cl_str);
    free(ns);
    free(cs);
    free(clfn);
    free(infn);
    free(outfn);
    free(errfn);
    /* Close log file */
    errno = 0;
    if (logfp != NULL && fclose(logfp)) {
        logfp = NULL; /* So that LOGE uses stderr */
        LOGE("failed to close log file");
        ret = 1;
    }
    /* Delete log file if not used */
    if (!log_file_used) RM(logfn);
    else fprintf(stderr, "%s: check log file: %s\n", *argv, logfn);
    free(logfn);
    return ret;
}
