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
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define GAP 2

#define CLEAR_SCREEN printf("\033[2J")
#define MOVE_CURSOR(y, x) printf("\033[" #y ";" #x "H")

struct buffer {
    char *fn;  /* Filename where the buffer will save to */
    char *a;   /* Start of buffer */
    char *g;   /* Start of gap */
    char *c;   /* Cursor (after gap) */
    char *e;   /* End of buffer */
    char *d;   /* Draw start */
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
    b->d = b->d - old_a + b->a;
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

int get_filesize(char *fn, size_t *fs)
{
    struct _stat64 st;
    if (_stat64(fn, &st)) return 1;
    if (st.st_size > SIZE_MAX || st.st_size < 0) return 1;
    *fs = (size_t) st.st_size;
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
    b->d = b->a;
    b->m = 0;
    b->m_set = 0;
    return b;
}

int insert_file(struct buffer *b, char *fn)
{
    size_t fs;
    FILE *fp;
    if (get_filesize(fn, &fs)) return 1;
    if (fs > (size_t) (b->c - b->g)) if (grow_gap(b, fs)) return 1;
    if ((fp = fopen(fn, "rb")) == NULL) return 1;
    if (fread(b->g, 1, fs, fp) != fs) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp)) return 1;
    b->g += fs;
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

int main (void)
{

    int h, w;

    struct buffer *b = init_buffer(10);
    insert_char(b, 'c', 5);

#ifdef _WIN32
    setup_graphics();
#endif

    CLEAR_SCREEN;
    MOVE_CURSOR(0, 0);

    if (get_screen_size(&h, &w)) printf("get_screen_size failed");
    printf("height: %d, width: %d", h, w);

    getchar();

    return 0;
}
