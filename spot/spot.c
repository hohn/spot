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
#include <stdint.h>
#include <string.h>


#define GAP 2

/* #ifdef _WIN32 */


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

int main (void)
{
    return 0;
}
