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

#define GAP 2

#define CLEAR_SCREEN() printf("\033[2J")
#define CLEAR_LINE() printf("\033[2K")

#define QUIT(r) do {ret = r; goto clean_up;} while (0)

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

int move_left(struct buffer *b, size_t mult)
{
    if (mult > (size_t) (b->g - b->a)) return 1;
    memmove(b->c - mult, b->g - mult, mult);
    b->g -= mult;
    b->c -= mult;
    return 0;
}

int move_right(struct buffer *b, size_t mult)
{
    if (mult > (size_t) (b->e - b->c)) return 1;
    memmove(b->g, b->c, mult);
    b->g += mult;
    b->c += mult;
    return 0;
}

int grow_gap(struct buffer *b, size_t req)
{
    size_t target_gap, current_gap, increase, current_size, target_size;
    char *old_a = b->a, *t;
    if (req > SIZE_MAX - GAP) return 1;
    target_gap = req + GAP;
    current_gap = b->c - b->g;
    increase = target_gap - current_gap;
    current_size = b->e - b->a + 1;
    target_size = current_size + increase;
    if ((t = realloc(b->a, target_size)) == NULL) return 1;
    b->a = t;
    b->g = b->g - old_a + b->a;
    b->c = b->c - old_a + b->a;
    b->e = b->e - old_a + b->a;
    memmove(b->c + increase, b->c, b->e - b->c + 1);
    b->c += increase;
    b->e += increase;
    return 0;
}

int insert_char(struct buffer *b, char ch, size_t mult)
{
    if (mult > (size_t) (b->c - b->g)) if (grow_gap(b, mult)) return 1;
    memset(b->g, ch, mult);
    b->g += mult;
    return 0;
}

int delete_char(struct buffer *b, size_t mult)
{
    if (mult > (size_t) (b->e - b->c)) return 1;
    b->c += mult;
    return 0;
}

int backspace_char(struct buffer *b, size_t mult)
{
    if (mult > (size_t) (b->g - b->a)) return 1;
    b->g -= mult;
    return 0;
}

struct buffer *init_buffer(size_t req)
{
    struct buffer *b;
    if ((b = malloc(sizeof(struct buffer))) == NULL) return NULL;
    if (req > SIZE_MAX - GAP) {
        free(b);
        return NULL;
    }
    if ((b->a = malloc(req + GAP)) == NULL) {
        free(b);
        return NULL;
    }
    b->fn = NULL;
    b->g = b->a;
    *(b->e = b->a + req + GAP - 1) = '~';
    b->c = b->e;
    b->d = 0;
    b->m = 0;
    b->m_set = 0;
    return b;
}

int insert_file(struct buffer *b, char *fn)
{
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
    return 0;
}

int write_buffer(struct buffer *b, char *fn)
{
    struct _stat64 st;
    int backup_ok = 0;
    size_t len, num;
    char *backup_fn;
    FILE *fp;
    if (!_stat64(fn, &st) && st.st_mode & _S_IFREG) {
        len = strlen(fn);
        if (len > SIZE_MAX - 2) return 1;
        if ((backup_fn = malloc(len + 2)) == NULL) return 1;
        memcpy(backup_fn, fn, len);
        *(backup_fn + len) = '~';
        *(backup_fn + len + 1) = '\0';
        if (rename(fn, backup_fn)) {
            free(backup_fn);
            return 1;
        }
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
    size_t len;
    char *t;
    if (new_name == NULL) return 1;
    len = strlen(new_name);
    if (!len) return 1;
    if (len == SIZE_MAX) return 1;
    if ((t = malloc(len + 1)) == NULL) return 1;
    memcpy(t, new_name, len);
    *(t + len) = '\0';
    free(b->fn);
    b->fn = t;
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
    HANDLE out;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if ((out = GetStdHandle(STD_OUTPUT_HANDLE)) == INVALID_HANDLE_VALUE) return 1;
    if(!GetConsoleScreenBufferInfo(out, &info)) return 1;
    *height = info.srWindow.Bottom - info.srWindow.Top + 1;
    *width = info.srWindow.Right - info.srWindow.Left + 1;
    return 0;
}
#endif

void move_cursor(int y, int x)
{
    /* Top left corner is (1, 1) not (0, 0) so need to add one */
    printf("\033[%d;%dH", y + 1, x + 1);
}

void centre_cursor(struct buffer *b, int h, int w)
{
    char *q, ch;
    int up = h / 2 + 1;
    size_t horiz = 1; /* Cursor counted */
    if (b->g == b->a) {
        b->d = 0;
        return;
    }
    q = b->g - 1;
    while (up && q >= b->a) {
        ch = *q++;
        ++horiz;
        if (ch == '\n' || horiz == w) {
            horiz = 0;
            --up;
        }
    }
    if (q != b->a) ++q;
    b->d = q - b->a;
}

int draw_screen(struct buffer *b, struct buffer *cl, int cla, int h, int w,
    char *ns, int sa, int *cy, int *cx)
{
    int y, x;
    size_t ci = b->g - b->a; /* Cursor index */
    char *q, ch;
    size_t v;                /* Virtual screen index */
    size_t len;
    if (h < 3 || w < 1) return 1;
    if (ci < b->d) centre_cursor(b, h - 2, w);
draw_text:
    v = 0;
    y = 0;
    x = 0;
    q = b->a + b->d;
    while (q != b->g) {
        ch = *q++;
        if (ch == '\n' || x == w - 1) {
            if (y == h - 3) {
                centre_cursor(b, h - 2, w);
                memset(ns, ' ', sa);
                goto draw_text;
            }
            if (ch == '\n') v = (v / w + 1) * w;
            else *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
            ++y;
            x = 0;
        } else {
            *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
            ++x;
        }
    }
    *cy = y;
    *cx = x;
    q = b->c;
    while (q <= b->e) {
        ch = *q++;
        if (ch == '\n' || x == w - 1) {
            if (y == h - 3) break;
            if (ch == '\n') v = (v / w + 1) * w;
            else *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
            ++y;
            x = 0;
        } else {
            *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
            ++x;
        }
    }

draw_cl:
    v = sa - w;
    y = h - 1;
    x = 0;
    q = cl->a + cl->d;
    while (q != cl->g) {
        ch = *q++;
        if (ch == '\n' || x == w - 1) {
                centre_cursor(cl, 1, w);
                memset(ns + sa - w, ' ', w);
                goto draw_cl;
        } else {
            *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
            ++x;
        }
    }
    if (cla) {
        *cy = y;
        *cx = x;
    }
    q = cl->c;
    while (q <= cl->e) {
        ch = *q++;
        if (ch == '\n' || x == w - 1) {
            break;
        } else {
            *(ns + v++) = isgraph(ch) || ch == ' ' ? ch : '?';
            ++x;
        }
    }

    v = sa - w * 2;
    if (b->fn != NULL) {
        len = strlen(b->fn);
        memcpy(ns + v, b->fn, len < w ? len : w);
    } else {
        memcpy(ns + v, "NULL", 4);
    }

    return 0;
}

int diff_draw(char *ns, char *cs, int sa, int w)
{
    size_t v;
    char ch;
    for (v = 0; v < sa; ++v) {
        if ((ch = *(ns + v)) != *(cs + v)) {
            move_cursor(v / w, v - (v / w) * w);
            putchar(ch);
        }
    }
}

void test_print_buffer(struct buffer *b)
{
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
    int ret = 0, running = 1, x, h, w, cy, cx;
    struct buffer **z; /* The text buffers */
    size_t zs;         /* Number of text buffers */
    size_t za = 0;     /* The index of the active text buffer */
    struct buffer *cl; /* Command line buffer */
    int cla = 0;       /* Command line buffer is active */
    char *ns = NULL;   /* Next screen (virtual) */
    char *cs = NULL;   /* Current screen (virtual) */
    size_t ss = 0;     /* Screen size (virtual) */
    size_t sa;         /* Terminal screen area (real) */
    char *t;
    size_t i;
    struct _stat64 st;
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
    if ((cl = init_buffer(0)) == NULL) QUIT(1);

#ifdef _WIN32
    setup_graphics();
#endif



    while (running) {
        if (get_screen_size(&h, &w)) QUIT(1);
        if (h < 1 || w < 1) QUIT(1);
        if (h > INT_MAX / w) QUIT(1);
        sa = h * w;
        if (ss < sa) {
            if ((t = realloc(ns, sa)) == NULL) QUIT(1);
            ns = t;
            if ((t = realloc(cs, sa)) == NULL) QUIT(1);
            cs = t;
            ss = sa;
            memset(ns, ' ', ss);
            memset(cs, ' ', ss);
            CLEAR_SCREEN();
        } else {
            memset(ns, ' ', sa);
        }

        draw_screen(*(z + za), cl, cla, h, w, ns, sa, &cy, &cx);
        diff_draw(ns, cs, sa, w);
        move_cursor(cy, cx);
        t = cs;
        cs = ns;
        ns = t;

        x = _getch();
        insert_char(*(z + za), x, 1);
        if (x == 'q') running = 0;
    }

clean_up:
    CLEAR_SCREEN();

    if (ret) printf("FAIL\n");

    return ret;
}
