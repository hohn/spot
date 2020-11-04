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


#ifndef LBUF_H
#define LBUF_H

/*
 * Default gap size. Must be at least 1.
 * It is good to set small while testing, say 2, but BUFSIZ is a sensible
 * choice for real use (to limit the expense of growing the gap).
 */
#define GAP BUFSIZ

/*
 * End Of Buffer CHaracter. This cannot be deleted, but will not be written to
 * file.
 */
#define EOBCH '~'

/* One character operations with no out of bounds checking */
/* Move left */
#define LCH() (*--b->c = *--b->g)
/* Move right */
#define RCH() (*b->g++ = *b->c++)
/* Delete */
#define DCH() (b->c++)

#ifndef ST_OVERFLOW_TESTS
#define ST_OVERFLOW_TESTS
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))
#endif

/* ASCII test for unsigned input that could be larger than UCHAR_MAX */
#define ISASCII(u) ((u) <= 127)

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

/* Memory structure: used for copy and paste, and search */
struct mem {
    char *p;                    /* Pointer to memory */
    size_t u;                   /* Used memory amount (<= s) */
    size_t s;                   /* Memory size */
};

int move_left(struct buffer *b, size_t mult);
int move_right(struct buffer *b, size_t mult);
void start_of_buffer(struct buffer *b);
void end_of_buffer(struct buffer *b);
void start_of_line(struct buffer *b);
void end_of_line(struct buffer *b);
size_t col_index(struct buffer *b);
int up_line(struct buffer *b, size_t mult);
int down_line(struct buffer *b, size_t mult);
int match_brace(struct buffer *b);
void delete_buffer(struct buffer *b);
void trim_clean(struct buffer *b);
void set_bad(size_t * bad, struct mem *se);
int search(struct buffer *b, struct mem *se, size_t * bad);
int insert_char(struct buffer *b, char ch, size_t mult);
int delete_char(struct buffer *b, size_t mult);
int backspace_char(struct buffer *b, size_t mult);
struct buffer *init_buffer(size_t req);
void free_buffer(struct buffer *b);
struct mem *init_mem(void);
void free_mem(struct mem *p);
int get_file_size(char *fn, size_t * fs);
int insert_file(struct buffer *b, char *fn);
int write_buffer(struct buffer *b, char *fn, int backup_req);
int rename_buffer(struct buffer *b, char *new_name);
int buffer_to_str(struct buffer *b, char **p_to_str);
int buffer_to_mem(struct buffer *b, struct mem *m);
void set_mark(struct buffer *b);
int copy_region(struct buffer *b, struct mem *p, int del);
int paste(struct buffer *b, struct mem *p, size_t mult);
int cut_to_eol(struct buffer *b, struct mem *p);
int cut_to_sol(struct buffer *b, struct mem *p);

#endif
