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

#define LARGEGAP 2
#define SMALLGAP 1

#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

#define TEXTSIZE(b) ((b)->s - (b)->g)

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

/* Rear gap buffer (used for output and tokens) */
struct rbuf {
    char *p;
    size_t s; /* Buffer size */
    size_t g; /* Gap size */
};

/* Front gap buffer (used for input) */
struct fbuf {
    char *p;
    size_t s; /* Buffer size */
    size_t g; /* Gap size */
};

/* For argument collection */
struct marg {
    struct marg *prev;
    struct mem text;
    int quote_on;
    size_t quote_depth;
    size_t act_arg;
    size_t bracket_depth; /* Only unquoted brackets are counted */
    struct rbuf arg[MAXARGS];
    struct marg *next;
};

struct fbuf *init_fbuf(size_t target_gap, size_t will_use) {
    struct fbuf *fb;
    if (AOF(target_gap, will_use)) return NULL;
    if ((fb = malloc(sizeof(struct fbuf))) == NULL) return NULL;
    if ((fb->p = malloc(target_gap + will_use)) == NULL) {
        free(fb);
        return NULL;
    }
    fb->s = target_gap + will_use;
    fb->g = target_gap + will_use;
    return fb;
}

struct rbuf *init_rbuf(size_t target_gap, size_t will_use) {
    struct rbuf *rb;
    if (AOF(target_gap, will_use)) return NULL;
    if ((rb = malloc(sizeof(struct fbuf))) == NULL) return NULL;
    if ((rb->p = malloc(target_gap + will_use)) == NULL) {
        free(rb);
        return NULL;
    }
    rb->s = target_gap + will_use;
    rb->g = target_gap + will_use;
    return rb;
}

void free_fbuf(struct fbuf *fb) {
    if (fb != NULL) {
        free(fb->p);
        free(fb);
    }
}

void free_rbuf(struct rbuf *rb) {
    if (rb != NULL) {
        free(rb->p);
        free(rb);
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

int grow_fbuf(struct fbuf *fb, size_t target_gap, size_t will_use) {
    size_t new_s;
    size_t increase;
    char *t;
    if (AOF(target_gap, will_use)) return 1;
    new_s = TEXTSIZE(fb) + target_gap + will_use;
    increase = new_s - fb->s;
    if ((t = realloc(fb->p, new_s)) == NULL) return 1;
    memmove(t + fb->g + increase, t + fb->g, TEXTSIZE(fb));
    fb->p = t;
    fb->s = new_s;
    fb->g += increase;
    return 0;
}

int grow_rbuf(struct rbuf *rb, size_t target_gap, size_t will_use) {
    size_t new_s;
    size_t increase;
    char *t;
    if (AOF(target_gap, will_use)) return 1;
    new_s = TEXTSIZE(rb) + target_gap + will_use;
    increase = new_s - rb->s;
    if ((t = realloc(rb->p, new_s)) == NULL) return 1;
    rb->p = t;
    rb->s = new_s;
    rb->g += increase;
    return 0;
}

void delete_rbuf(struct rbuf *rb) {
    rb->g = rb->s;
}

int read_token(struct fbuf *input, struct rbuf *token, int *end_of_input) {
    char ch;
    delete_rbuf(token);
    if (input->g == input->s) {
        *end_of_input = 1;
        return 1;
    }
    *end_of_input = 0;
    ch = *(input->p + input->g);
    if (!token->g) if (grow_rbuf(token, SMALLGAP, 1)) return 1;
    *(token->p + TEXTSIZE(token)) = ch;
    --token->g;
    ++input->g;
    if (isalpha(ch) || ch == '_') {
        while (input->g < input->s) {
            ch = *(input->p + input->g);
            if (!isalnum(ch) && ch != '_') break;
            if (!token->g) if (grow_rbuf(token, SMALLGAP, 1)) return 1;
            *(token->p + TEXTSIZE(token)) = ch;
            --token->g;
            ++input->g;
        }
    }
    return 0;
}

int insert_file(struct fbuf *fb, char *fn) {
    size_t fs;
    FILE *fp;
    if (filesize(fn, &fs)) return 1;
    if (!fs) return 0;
    if (fb->g < fs) if(grow_fbuf(fb, LARGEGAP, fs)) return 1;
    if ((fp = fopen(fn, "r")) == NULL) return 1;
    if (fread(fb->p + fb->g - fs, 1, fs, fp) != fs) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp)) return 1;
    fb->g -= fs;
    return 0;
}

int insert_rbuf_in_fbuf(struct fbuf *fb, struct rbuf *rb) {
    size_t ts = TEXTSIZE(rb);
    if (fb->g < ts) if (grow_fbuf(fb, LARGEGAP, ts)) return 1;
    memcpy(fb->p + fb->g - ts, rb->p, ts);
    fb->g -= ts;
    return 0;
}

int read_stdin(struct rbuf *rb) {
    int x;
    while ((x = getchar()) != EOF) {
        if (!rb->g) if (grow_rbuf(rb, LARGEGAP, 1)) return 1;
        *(rb->p + TEXTSIZE(rb)) = x;
        --rb->g;
    }
    if (ferror(stdin) || !feof(stdin)) return 1;
    return 0;
}

void print_token(struct rbuf *token) {
    /* This function is just for testing */
    size_t i = 0;
    size_t ts = TEXTSIZE(token);
    char ch;
    if (!ts) return;
    for (i = 0; i < ts; ++i) {
        ch = *(token->p + i);
        if (ch == '\n') printf("\\n");
        else if (ch == '\t') printf("\\t");
        else if (ch == ' ') printf("SPC");
        else putchar(ch);
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    size_t fs;
    size_t total_fs = 0;
    struct rbuf *tmp;
    struct fbuf *input;
    struct rbuf *token;
    int end_of_input;
    int j;

    if (argc < 1) return 1;

    if (argc == 1) {
        if ((tmp = init_rbuf(LARGEGAP, 1)) == NULL) return 1;
        if (read_stdin(tmp)) {
            free_rbuf(tmp);
            return 1;
        }
        if ((input = init_fbuf(LARGEGAP, TEXTSIZE(tmp))) == NULL) {
            free_rbuf(tmp);
            return 1;
        }
        if (insert_rbuf_in_fbuf(input, tmp)) {
            free_rbuf(tmp);
            free_fbuf(input);
            return 1;
        }
        free_rbuf(tmp);
    } else {
        for (j = 1; j < argc; ++j) {
            if (filesize(*(argv + j), &fs)) return 1;
            total_fs += fs;
        }
        if (AOF(total_fs, LARGEGAP)) return 1;
        if ((input = init_fbuf(LARGEGAP, total_fs)) == NULL) return 1;
        for (j = argc - 1; j; --j) {
            if (insert_file(input, *(argv + j))) {
                free_fbuf(input);
                return 1;
            }
        }
    }

    if ((token = init_rbuf(SMALLGAP, 1)) == NULL) {
        free_fbuf(input);
        return 1;
    }

    while (!read_token(input, token, &end_of_input)) {
        print_token(token);
    }
    if (!end_of_input) {
        free_fbuf(input);
        free_rbuf(token);
        return 1;
    }


    free_fbuf(input);
    free_rbuf(token);
    return 0;
}
