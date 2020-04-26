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
 * m4 written from scratch
 */

#include <sys/stat.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXARGS 9

#define GROWTH 2
#define LARGEGAP 2
#define SMALLGAP 1

#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

#define TEXTSIZE(b) ((b)->s - (b)->g)
#define DELETEBUF(b) ((b)->g = (b)->s)

/* Byte string (can handle '\0' characters) */
struct mem {
    char *p;
    size_t s;
};

/* For macro definitions */
struct mdef {
    struct mdef *prev;
    struct mem name;
    struct mem text;
    struct mdef *next;
};

/*
 * The rear gap buffer is used for output and tokens,
 * and the front gap buffer is used for input.
 */
struct buf {
    char *p;
    size_t s; /* Buffer size */
    size_t g; /* Gap size */
    int rear; /* 1: gap is at the rear, 0: gap is at the front */
};

/* For argument collection */
struct marg {
    struct marg *prev;
    struct mem text;
    int quote_on;
    size_t quote_depth;
    size_t act_arg;
    size_t bracket_depth; /* Only unquoted brackets are counted */
    struct buf *arg[MAXARGS];
    struct marg *next;
};

struct buf *init_buf(size_t s, int rear) {
    struct buf *fb;
    if ((fb = malloc(sizeof(struct buf))) == NULL) return NULL;
    if ((fb->p = malloc(s)) == NULL) {
        free(fb);
        return NULL;
    }
    fb->s = s;
    fb->g = s;
    fb->rear = rear;
    return fb;
}

void free_buf(struct buf *b) {
    if (b != NULL) {
        free(b->p);
        free(b);
    }
}

int filesize(char *fn, size_t *fs) {
    struct stat st;
    if (stat(fn, &st)) return 1;
    if (!S_ISREG(st.st_mode)) return 1;
    if (st.st_size < 0) return 1;
    *fs = st.st_size;
    return 0;
}

int grow_buf(struct buf *b, size_t fixed_chunk) {
    size_t new_s;
    size_t increase;
    char *t;
    if (MOF(b->s, GROWTH)) return 1;
    if (AOF(b->s * GROWTH, fixed_chunk)) return 1;
    new_s = b->s * GROWTH + fixed_chunk;
    increase = new_s - b->s;
    if ((t = realloc(b->p, new_s)) == NULL) return 1;
    if (!b->rear) memmove(t + b->g + increase, t + b->g, TEXTSIZE(b));
    b->p = t;
    b->s = new_s;
    b->g += increase;
    return 0;
}

int read_token(struct buf *input, struct buf *token, int *end_of_input) {
    char ch;
    DELETEBUF(token);
    if (input->g == input->s) {
        *end_of_input = 1;
        return 1;
    }
    *end_of_input = 0;
    ch = *(input->p + input->g);
    if (!token->g) if (grow_buf(token, 0)) return 1;
    *(token->p + TEXTSIZE(token)) = ch;
    --token->g;
    ++input->g;
    if (isalpha(ch) || ch == '_') {
        while (input->g < input->s) {
            ch = *(input->p + input->g);
            if (!isalnum(ch) && ch != '_') break;
            if (!token->g) if (grow_buf(token, 0)) return 1;
            *(token->p + TEXTSIZE(token)) = ch;
            --token->g;
            ++input->g;
        }
    }
    return 0;
}

int insert_file(struct buf *fb, char *fn) {
    size_t fs;
    FILE *fp;
    if (fb->rear) return 1;
    if (filesize(fn, &fs)) return 1;
    if (!fs) return 0;
    if (fb->g < fs) if(grow_buf(fb, fs)) return 1;
    if ((fp = fopen(fn, "r")) == NULL) return 1;
    if (fread(fb->p + fb->g - fs, 1, fs, fp) != fs) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp)) return 1;
    fb->g -= fs;
    return 0;
}

int insert_rear_in_front_buf(struct buf *fb, struct buf *rb) {
    size_t ts = TEXTSIZE(rb);
    if (fb->rear) return 1;
    if (!rb->rear) return 1;
    if (fb->g < ts) if (grow_buf(fb, ts)) return 1;
    memcpy(fb->p + fb->g - ts, rb->p, ts);
    fb->g -= ts;
    return 0;
}

int read_stdin(struct buf *rb) {
    int x;
    if (!rb->rear) return 1;
    while ((x = getchar()) != EOF) {
        if (!rb->g) if (grow_buf(rb, 0)) return 1;
        *(rb->p + TEXTSIZE(rb)) = x;
        --rb->g;
    }
    if (ferror(stdin) || !feof(stdin)) return 1;
    return 0;
}

void print_token(struct buf *token) {
    /* This function is just for testing */
    size_t i = 0;
    size_t ts = TEXTSIZE(token);
    char ch;
    if (!ts) return;
    for (i = 0; i < ts; ++i) {
        ch = *(token->p + i);
        printf(isgraph(ch) ? "%c" : "%02X", ch);
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    size_t fs;
    size_t total_fs = 0;
    struct buf *tmp;
    struct buf *input;
    struct buf *token;
    int end_of_input;
    int j;

    if (argc < 1) return 1;

    if (argc == 1) {
        if ((tmp = init_buf(LARGEGAP, 1)) == NULL) return 1;
        if (read_stdin(tmp)) {
            free_buf(tmp);
            return 1;
        }
        if (AOF(LARGEGAP, TEXTSIZE(tmp))) {
            free_buf(tmp);
            return 1;
        }
        if ((input = init_buf(LARGEGAP + TEXTSIZE(tmp), 0)) == NULL) {
            free_buf(tmp);
            return 1;
        }
        if (insert_rear_in_front_buf(input, tmp)) {
            free_buf(tmp);
            free_buf(input);
            return 1;
        }
        free_buf(tmp);
    } else {
        for (j = 1; j < argc; ++j) {
            if (filesize(*(argv + j), &fs)) return 1;
            total_fs += fs;
        }
        if (AOF(LARGEGAP, total_fs)) return 1;
        if ((input = init_buf(LARGEGAP + total_fs, 0)) == NULL) return 1;
        for (j = argc - 1; j; --j) {
            if (insert_file(input, *(argv + j))) {
                free_buf(input);
                return 1;
            }
        }
    }

    if ((token = init_buf(SMALLGAP, 1)) == NULL) {
        free_buf(input);
        return 1;
    }

    while (!read_token(input, token, &end_of_input)) {
        print_token(token);
    }
    if (!end_of_input) {
        free_buf(input);
        free_buf(token);
        return 1;
    }


    free_buf(input);
    free_buf(token);
    return 0;
}
