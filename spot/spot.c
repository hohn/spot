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

#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/*
 * The default gap size. Must be at least 1.
 * It is good to set small while testing, but BUFSIZ is a sensible choice for
 * real use (to limit the expense of growing the gap).
 */
#define GAP 2

/*
 * End Of Buffer CHaracter. This cannot be deleted, but will not be written to
 * file.
 */
#define EOBCH '~'

/* Command keys */
#define MULT 21
#define LEFT 2
#define RIGHT 6
#define DEL 4
#define BKSPACE 8
#define SETMARK 0
#define COPY 3
#define CUT 23
#define PASTE 25
#define SEARCH 19

/* File commands */
#define FILEMENU 24
#define SAVE 19
#define RENAME 18

/* size_t integer overflow tests */
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

/* ANSI escape sequences */
#define CLEAR_SCREEN() printf("\033[2J")
#define CLEAR_LINE() printf("\033[2K")

/*
 * Sets the return value for the text editor and jumps to the clean up.
 * Only use in main().
 */
#define QUIT(r) do {ret = r; goto clean_up;} while (0)

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
    if (mult > (size_t) (b->g - b->a)) return 1;
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
    if (mult > (size_t) (b->e - b->c)) return 1;
    memmove(b->g, b->c, mult);
    b->g += mult;
    b->c += mult;
    return 0;
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
    unsigned char *q, *stop, *q_copy, *pat;
    size_t patlen;
    int found = 0;
    if (!se->u || b->e - b->c <= 1) return 1;
    size_t s = b->e - b->c - 1;
    if (se->u > s) return 1;
    if (se->u == 1) {
        /* Single character patterns */
        q = memchr(b->c + 1, *se->p, s);
        if (q == NULL) return 1;
    } else {
        /* Quick Search algorithm */
        q = b->c + 1;
        stop = b->e - se->u; /* Inclusive */
        while (q <= stop) {
            q_copy = q;
            pat = se->p;
            patlen = se->u;
            /* Compare pattern to text */
            do {if (*q_copy++ != *pat++) break;} while (--patlen);
            /* Match found */
            if (!patlen) {found = 1; break;}
            /* Jump using the bad character table */
            q += *(bad + *(q + se->u));
        }
        if (!found) return 1;
    }
    if (move_right(b, q - b->c)) return 1;
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
     */
    size_t req_increase, current_size, target_size, increase;
    char *t, *tc;
    if (AOF(req, GAP)) return 1;
    /* Only call grow_gap if req > (b->c - b->g) */
    req_increase = req + GAP - (b->c - b->g);
    current_size = b->e - b->a + 1;
    increase = current_size > req_increase ? current_size : req_increase;
    if (AOF(current_size, increase)) return 1;
    target_size = current_size + increase;
    if ((t = malloc(target_size)) == NULL) return 1;
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
    if (mult > (size_t) (b->c - b->g)) if (grow_gap(b, mult)) return 1;
    memset(b->g, ch, mult);
    b->g += mult;
    b->m_set = 0;
    return 0;
}

int delete_char(struct buffer *b, size_t mult)
{
    /* Deletes mult chars by expanding the gap rightwards */
    if (mult > (size_t) (b->e - b->c)) return 1;
    b->c += mult;
    b->m_set = 0;
    return 0;
}

int backspace_char(struct buffer *b, size_t mult)
{
    /* Backspaces mult chars by expanding the gap leftwards */
    if (mult > (size_t) (b->g - b->a)) return 1;
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
    struct buffer *b;
    if ((b = malloc(sizeof(struct buffer))) == NULL) return NULL;
    if (AOF(req, GAP)) {
        free(b);
        return NULL;
    }
    if ((b->a = malloc(req + GAP)) == NULL) {
        free(b);
        return NULL;
    }
    b->fn = NULL;
    b->g = b->a;
    *(b->e = b->a + req + GAP - 1) = EOBCH;
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

struct mem *init_mem(void)
{
    struct mem *p;
    if ((p = malloc(sizeof(struct mem))) == NULL) return NULL;
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

int insert_file(struct buffer *b, char *fn)
{
    /*
     * Inserts a file into the right-hand side of the old gap, so that the
     * inserted text will appear after the new cursor position.
     */
    struct _stat64 st;
    size_t fs;
    FILE *fp;
    if (_stat64(fn, &st)) return 1;
    if (!((st.st_mode & _S_IFMT) == _S_IFREG)
        || st.st_size > SIZE_MAX || st.st_size < 0) return 1;
    if (!st.st_size) return 0;
    fs = (size_t) st.st_size;
    if (fs > (size_t) (b->c - b->g)) if (grow_gap(b, fs)) return 1;
    if ((fp = fopen(fn, "rb")) == NULL) return 1;
    if (fread(b->c - fs, 1, fs, fp) != fs) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp)) return 1;
    b->c -= fs;
    b->m_set = 0;
    return 0;
}

int write_buffer(struct buffer *b, char *fn)
{
    /*
     * Writes a buffer to file. If the file already exists, then it will be
     * renamed to have a '~' suffix (to serve as a backup).
     */
    struct _stat64 st;
    int backup_ok = 0;
    size_t len, num;
    char *backup_fn;
    FILE *fp;
    if (fn == NULL) return 1;
    if (!_stat64(fn, &st) && st.st_mode & _S_IFREG) {
        len = strlen(fn);
        if (AOF(len, 2)) return 1;
        if ((backup_fn = malloc(len + 2)) == NULL) return 1;
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

    if ((fp = fopen(fn, "wb")) == NULL) return 1;
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
    if (fclose(fp)) return 1;

#ifndef _WIN32
    if (backup_ok && chmod(fn, st.st_mode & 0777)) return 1;
#endif

    return 0;
}

int rename_buffer(struct buffer *b, char *new_name)
{
    /* Sets or changes the filename associated with a buffer */
    size_t len;
    char *t;
    if (new_name == NULL) return 1;
    len = strlen(new_name);
    if (!len) return 1;
    if (AOF(len, 1)) return 1;
    if ((t = malloc(len + 1)) == NULL) return 1;
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
    if ((t = malloc(s)) == NULL) return 1;
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
    size_t s_bg = b->g - b->a; /* Size before gap */
    size_t s_ag = b->e - b->c; /* Size after gap */
    size_t s = s_bg + s_ag;    /* Text size of buffer */
    char *t;                   /* Temporary pointer */
    if (s > m->s) {
        if ((t = malloc(s)) == NULL) return 1;
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
    size_t ci = b->g - b->a; /* Cursor index */
    size_t s;                /* Size of region */
    char *t;                 /* Temporary pointer */
    if (!b->m_set) return 1;
    if (b->m == ci) return 0;
    if (b->m < ci) {
        s = ci - b->m;
        if (s > p->s) {
            if ((t = malloc(s)) == NULL) return 1;
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
            if ((t = malloc(s)) == NULL) return 1;
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
    size_t s;
    if (MOF(mult, p->u)) return 1;
    s = p->u * mult;
    if (s > (size_t) (b->c - b->g)) if (grow_gap(b, s)) return 1;
    while (mult--) {memcpy(b->g, p->p, p->u); b->g += p->u;}
    b->m_set = 0;
    return 0;
}

#ifdef _WIN32
int setup_graphics(void)
{
    HANDLE out;
    DWORD mode;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE) return 1;
    if (!GetConsoleMode(out, &mode)) return 1;
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return 1;
    return 0;
}
#endif

#ifdef _WIN32
int get_screen_size(int *height, int *width)
{
    /* Gets the screen size in Windows */
    HANDLE out;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE) return 1;
    if(!GetConsoleScreenBufferInfo(out, &info)) return 1;
    *height = info.srWindow.Bottom - info.srWindow.Top + 1;
    *width = info.srWindow.Right - info.srWindow.Left + 1;
    return 0;
}
#endif

void reverse_scan(struct buffer *b, int th, int w, int centre)
{
    /*
     * This is the most complicated function in the text editor.
     * It does a reverse scan to see if the cursor would be on the screen.
     * If the cursor would be off the screen, or if the user requested to
     * centre, then draw start will be set so that the cursor will end up
     * on the middle line.
     */

    size_t ci = b->g - b->a;       /* Cursor index */
    int ta = th * w;               /* Text screen area */
    int hth = th > 2 ? th / 2 : 1; /* Half the text screen height */
    int hta = hth * w;             /* Half the text screen area */
    int target_h;                  /* The height to stop scanning */
    int height_count = 0;          /* Keeping track of the height while scanning */
    /*
     * Keeping track of the width while scanning.
     * Start from one to count the cursor.
     */
    int width_count = 1;
    char *q;                       /* Pointer used for the reverse scan */
    char *q_stop;                  /* Pointer where the reverse scan is to stop (inclusive) */
    char *q_d = b->a + b->d;       /* Current draw start pointer */
    /*
     * Pointer to the draw start postion that results in the cursor being centred.
     * Only used when centreing is not planned.
     */
    char *q_centre;

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

int draw_screen(struct buffer *b, struct buffer *cl, int cla, int cr, int h,
    int w, char *ns, int sa, int *cy, int *cx)
{
    /* Virtually draw screen (and clear unused sections on the go) */
    char *q, ch;
    unsigned char u, w_part, r_part; /* For hex calculation */
    size_t v;                        /* Virtual screen index */
    size_t j;                        /* Filename character index */
    size_t len;                      /* Used for printing the filename */
    /* Height of text buffer portion of the screen */
    int th = h > 2 ? h - 2 : 1;

    /* Text buffer */
    reverse_scan(b, th, w, 0);
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
        if (ch == '\n') do {*(ns + v++) = ' ';} while (v % w);
        else *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
    }
    /* Record cursor's screen location */
    *cy = v / w;
    *cx = v % w;
    q = b->c;
    /* Print after the gap */
    while (1) {
        ch = *q;
        if (ch == '\n') do {*(ns + v++) = ' ';} while (v % w);
        else *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
        /* Stop if have reached the status bar, before printing there */
        if (v / w == th) break;
        /* To avoid incrementing pointer outside of memory allocation */
        if (q == b->e) break;
        ++q;
    }
    /* Fill in unused text region with spaces */
    while (v / w != th) *(ns + v++) = ' ';

    /* Stop if screen is only one row high */
    if (h == 1) return 0;

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
    if (h == 2) return 0;

    /* Command line buffer */
    reverse_scan(cl, 1, w, 0);
    q = cl->a + cl->d;
    /* Print before the gap */
    while (q != cl->g) {
        ch = *q++;
        if (ch == '\n') do {*(ns + v++) = ' ';} while (v % w);
        else *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
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
        if (ch == '\n') do {*(ns + v++) = ' ';} while (v % w);
        else *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
        /* Stop if off the bottom of the screen, before printing there */
        if (v / w == h) break;
        if (q == cl->e) break;
        ++q;
    }
    /* Fill in unused text region with spaces */
    while (v / w != h) *(ns + v++) = ' ';

    return 0;
}

void diff_draw(char *ns, char *cs, int sa, int w)
{
    /* Physically draw the screen where the virtual screens differ */
    int v;          /* Index */
    int in_pos = 0; /* If in position for printing (no move is required) */
    char ch;
    for (v = 0; v < sa; ++v) {
        if ((ch = *(ns + v)) != *(cs + v)) {
            if (!in_pos) {
                /* Move cursor: top left corner is (1, 1) not (0, 0) so need to add one */
                printf("\033[%d;%dH", v / w + 1, v - (v / w) * w + 1);
                in_pos = 1;
            }
            putchar(ch);
        } else {
            in_pos = 0;
        }
    }
}

void test_print_buffer(struct buffer *b)
{
    /*
     * Prints a buffer to the terminal in a non-graphical way.
     * Only used for testing the gap buffer memory.
     */
    char *q = b->a, ch;
    printf("gi = %zu, ci = %zu, ei = %zu\n", (size_t) (b->g - b->a),
        (size_t) (b->c - b->a), (size_t) (b->e - b->a));
    while (q != b->g) {
        ch = *q++;
        putchar(isgraph(ch) || ch == ' ' || ch == '\n' ? ch : '?');
    }
    while (q != b->c) {
        ch = *q++;
        putchar('X');
    }
    while (q <= b->e) {
        ch = *q++;
        putchar(isgraph(ch) || ch == ' ' || ch == '\n' ? ch : '?');
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    int ret = 0;         /* Editor's return value */
    int running = 1;     /* Editor is running */
    int h, w;            /* Screen height and width */
    int cy, cx;          /* Cursor's screen coordinates */
    struct buffer **z;   /* The text buffers */
    size_t zs;           /* Number of text buffers */
    size_t za = 0;       /* The index of the active text buffer */
    struct buffer *cl;   /* Command line buffer */
    int cla = 0;         /* Command line buffer is active */
    /* Operation for which the command line is being used */
    char operation = '\0';
    char *cl_str = NULL; /* Command line buffer converted to a string */
    /* Bad character table for the Quick Search algorithm */
    size_t bad[UCHAR_MAX + 1];
    struct mem *se;      /* Search memory */
    struct mem *p;       /* Paste memory */
    int cr = 0;          /* Command return value */
    struct buffer *cb;   /* Shortcut to the cursor's buffer */
    char *ns = NULL;     /* Next screen (virtual) */
    char *cs = NULL;     /* Current screen (virtual) */
    size_t ss = 0;       /* Screen size (virtual) */
    size_t sa = 0;       /* Terminal screen area (real) */
    size_t c_sa;         /* Current terminal screen area (real) */
    /* Keyboard keys (one physical key can send multiple) */
    int key1, key2;
    int digit;           /* Numerical digit */
    size_t mult;         /* Command multiplier */
    char *t;             /* Temporary pointer */
    size_t i;            /* Generic index */
    struct _stat64 st;   /* For stat calls */

    /* Ignore interrupt, sent by ^C */
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) return 1;

    /* Process command line arguments */
    if (argc > SIZE_MAX) return 1;
    if (argc > 1) {
        zs = argc - 1;
        if ((z = malloc(zs * sizeof(struct buffer *))) == NULL) return 1;
        for (i = 0; i < zs; ++i) {
            if (!_stat64(*(argv + i + 1), &st)) {
                if (!((st.st_mode & _S_IFMT) == _S_IFREG)
                    || st.st_size > SIZE_MAX || st.st_size < 0) QUIT(1);
                if ((*(z + i) = init_buffer((size_t) st.st_size)) == NULL) QUIT(1);
                if (rename_buffer(*(z + i), *(argv + i + 1))) QUIT(1);
                if (insert_file(*(z + i), (*(z + i))->fn)) QUIT(1);
            } else {
                if ((*(z + i) = init_buffer(0)) == NULL) QUIT(1);
                if (rename_buffer(*(z + i), *(argv + i + 1))) QUIT(1);
            }
        }
    } else {
        zs = 1;
        if ((z = malloc(sizeof(struct buffer *))) == NULL) return 1;
        if ((*z = init_buffer(0)) == NULL) QUIT(1);
    }
    /* Initialise command line buffer */
    if ((cl = init_buffer(0)) == NULL) QUIT(1);
    /* Initialise search memory */
    if ((se = init_mem()) == NULL) QUIT(1);
    /* Initialise paste memory */
    if ((p = init_mem()) == NULL) QUIT(1);

#ifdef _WIN32
    setup_graphics();
#endif

    /* Editor loop */
    while (running) {

top_of_editor_loop:

        if (get_screen_size(&h, &w)) QUIT(1);

        /* Do graphics only if screen is big enough */
        if (h >= 1 && w >= 1) {
            if (h > INT_MAX / w) QUIT(1);
            c_sa = h * w;
            /* Change in screen size */
            if (c_sa != sa) {
                sa = c_sa;
                /* Bigger screen */
                if (ss < sa) {
                    free(ns);
                    free(cs);
                    if ((t = malloc(sa)) == NULL) QUIT(1);
                    ns = t;
                    if ((t = malloc(sa)) == NULL) QUIT(1);
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

            draw_screen(*(z + za), cl, cla, cr, h, w, ns, sa, &cy, &cx);
            diff_draw(ns, cs, sa, w);
            printf("\033[%d;%dH", cy + 1, cx + 1);
            /* Swap virtual screens */
            t = cs;
            cs = ns;
            ns = t;
        }

        /* Shortcut to the cursor's buffer */
        cb = cla ? cl : *(z + za);

        key1 = _getch();

        mult = 1; /* Default is to perform a command once */
        /* Read multiplier number */
        if (key1 == MULT) {
            mult = 0;
            while (isdigit(key1 = _getch())) {
                if (MOF(mult, 10)) {cr = 1; goto top_of_editor_loop;}
                mult *= 10;
                digit = key1 - '0';
                if (AOF(mult, digit)) {cr = 1; goto top_of_editor_loop;}
                mult += digit;
            }
        }

        /* Remap special keyboard keys */
        if (key1 == 0xE0) {
            key2 = _getch();
            switch(key2) {
            case 'K': key1 = LEFT; break;
            case 'M': key1 = RIGHT; break;
            case 'S': key1 = DEL; break;
            }
        }

        /* Remap carriage return to line feed */
        if (key1 == '\r') key1 = '\n';

        /* Commands related to files */
        if (key1 == FILEMENU) {
            key2 = _getch();
            switch (key2) {
            case SAVE: cr = write_buffer(cb, cb->fn); break;
            case RENAME: cla = 1; operation = 'R'; break;
            }
        } else {
            /* Non-file related commands */
            switch(key1) {
            case LEFT: cr = move_left(cb, mult); break;
            case RIGHT: cr = move_right(cb, mult); break;
            case DEL: cr = delete_char(cb, mult); break;
            case BKSPACE: cr = backspace_char(cb, mult); break;
            case SETMARK: set_mark(cb); break;
            case COPY: cr = copy_region(cb, p, 0); break;
            case CUT: cr = copy_region(cb, p, 1); break;
            case PASTE: cr = paste(cb, p, mult); break;
            case SEARCH: cla = 1; operation = 'S'; break;
            case '\n':
                if (cla) {
                    cla = 0;
                    if (operation == 'R') {
                        if (buffer_to_str(cl, &cl_str)) {cr = 1; break;}
                        cr = rename_buffer(*(z + za), cl_str); break;
                    } else if (operation == 'S') {
                        if (buffer_to_mem(cl, se)) {cr = 1; break;}
                        if (se->u > 1) {
                            set_bad(bad, se);
                        }
                        cr = search(*(z + za), se, bad); break;
                    }
                    operation = '\0';
                } else {
                    cr = insert_char(cb, key1, mult);
                }
                break;
            default: cr = insert_char(cb, key1, mult); break;
            }
        }

        if (key1 == 'q') running = 0;
    }

clean_up:
    CLEAR_SCREEN();
    free_buffer(cl);
    for (i = 0; i < zs; ++i) free_buffer(*(z + i));
    free_mem(se);
    free_mem(p);
    free(ns);
    free(cs);

    return ret;
}
