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

#include <string.h>

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


int main (void)
{
    return 0;
}
