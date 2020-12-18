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

/* ASCII test for unsigned input */
#define ISASCII(u) ((u) <= 127)

#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

/* These work on both rear and front buffers */
#define TEXTSIZE(b) ((b)->s - (b)->gs)
#define DELETEBUF(b) ((b)->gs = (b)->s)

/*
 * Appends a char to the end of a rear buffer.
 * Clear ret to zero before calling this macro.
 */
#define RBAPPENDCH(rb, ch, ret) do { \
    if (!(rb)->gs && grow_rear_buf(rb, 1)) { \
        ret = 1; \
    } else { \
        *((rb)->p + TEXTSIZE(rb)) = ch; \
        (rb)->gs -= 1; \
    } \
} while (0)

/*
 * A memory struct, also called a byte string.
 * This is used instead of strings as it can handle '\0' characters.
 */
struct mem {
    char *p;                    /* Pointer to memory */
    size_t s;                   /* Size of memory */
};

/* For macro definitions. Link together to form a doubly linked list. */
struct mdef {
    struct mdef *prev;          /* Previous node (first node is NULL) */
    struct mem name;            /* Macro name */
    struct mem text;            /* Macro replacement text */
    struct mdef *next;          /* Next node (last node is NULL) */
};

/*
 * Rear gap buffer: used for outputs and tokens.
 *
 *  +-------------+---------+
 *  |    text     |   gap   |
 *  +-------------+---------+
 *  |<---- s ---->|<-- g -->|
 *  p
 */
struct rear_buf {
    char *p;                    /* Pointer to memory */
    size_t s;                   /* Total buffer size */
    size_t gs;                  /* Gap size */
};


/*
 * Front gap buffer: used for the input.
 *
 *  +---------+-------------+
 *  |   gap   |    text     |
 *  +---------+-------------+
 *  |<-- g -->|<---- s ---->|
 *  p
 */
struct front_buf {
    char *p;                    /* Pointer to memory */
    size_t s;                   /* Total buffer size */
    size_t gs;                  /* Gap size */
};

/*
 * For macro argument collection. These link together to form a doubly linked
 * list which is the stack. The stack allows for nested expansion to occur
 * during argument collection.
 */
struct margs {
    struct margs *prev;         /* Previous node (first is NULL) */
    struct mem text;            /* Macro replacement text before arguments are substituted */
    size_t bracket_depth;       /* For nested brackets: only unquoted brackets are counted */
    size_t act_arg;             /* Active argument */
    struct rear_buf *args[MAXARGS];     /* Storage of the collected arguments before substitution */
    struct margs *next;         /* Next node (last is NULL) */
};

struct rear_buf *init_rear_buf(size_t s)
{
    /* Initalises a rear buffer */
    struct rear_buf *rb;
    if ((rb = malloc(sizeof(struct rear_buf))) == NULL)
        return NULL;
    if ((rb->p = malloc(s)) == NULL) {
        free(rb);
        return NULL;
    }
    rb->s = s;
    rb->gs = s;
    return rb;
}

struct front_buf *init_front_buf(size_t s)
{
    /* Initalises a front buffer */
    struct front_buf *fb;
    if ((fb = malloc(sizeof(struct front_buf))) == NULL)
        return NULL;
    if ((fb->p = malloc(s)) == NULL) {
        free(fb);
        return NULL;
    }
    fb->s = s;
    fb->gs = s;
    return fb;
}

void free_rear_buf(struct rear_buf *rb)
{
    /* Frees a rear buffer */
    if (rb != NULL) {
        free(rb->p);
        free(rb);
    }
}

void free_front_buf(struct front_buf *fb)
{
    /* Frees a front buffer */
    if (fb != NULL) {
        free(fb->p);
        free(fb);
    }
}

struct mdef *create_mdef_node(void)
{
    /*
     * Creates a macro definition node.
     * Does not link it into the doubly linked list.
     */
    struct mdef *md;
    if ((md = malloc(sizeof(struct mdef))) == NULL)
        return NULL;
    md->prev = NULL;
    md->name.p = NULL;
    md->name.s = 0;
    md->text.p = NULL;
    md->text.s = 0;
    md->next = NULL;
    return md;
}

int stack_on_mdef(struct mdef **md)
{
    /*
     * Add a new macro definition node to the end of the macro definition
     * doubly linked list.
     */
    struct mdef *t;
    if ((t = create_mdef_node()) == NULL)
        return 1;
    if (*md != NULL) {
        t->next = *md;
        (*md)->prev = t;
    }
    *md = t;
    return 0;
}

int mem_to_mem_cpy(struct mem *dest, struct mem *source)
{
    /*
     * Copies source mem to the destination mem, freeing the destination mem
     * if not NULL.
     */
    char *t;
    if ((t = malloc(source->s)) == NULL) {
        return 1;
    }
    free(dest->p);
    dest->p = t;
    memcpy(dest->p, source->p, source->s);
    dest->s = source->s;
    return 0;
}

int rear_buf_mem_cmp(struct rear_buf *rb, struct mem *m)
{
    /* Compares the text of a rear buffer to a byte string */
    size_t ts, i;
    /* Nothing to compare */
    if (rb == NULL || m == NULL || rb->p == NULL || m->p == NULL)
        return 1;
    ts = TEXTSIZE(rb);
    /* Empty text in rear buffer, or different sizes */
    if (!ts || ts != m->s)
        return 1;
    i = ts;
    while (i--)
        if (*(rb->p + i) != *(m->p + i))
            return 1;
    return 0;
}

int rear_buf_char_cmp(struct rear_buf *rb, char ch)
{
    /* Compares the text of a rear buffer to a char */
    size_t ts;
    if (rb == NULL || rb->p == NULL)
        return 1;
    ts = TEXTSIZE(rb);
    if (ts != 1)
        return 1;
    if (*rb->p != ch)
        return 1;
    return 0;
}

struct mem *token_search(struct mdef *md, struct rear_buf *token)
{
    /*
     * Searches for a token in the macro names of the macro definition doubly
     * linked list. If found it returns the replacement text (before the
     * arguments are substituted), else it returns NULL.
     */
    struct mdef *t = md, *next;
    while (t != NULL) {
        next = t->next;
        if (!rear_buf_mem_cmp(token, &md->name))
            return &t->text;
        t = next;
    }
    return NULL;
}

void free_mdef_node(struct mdef *md)
{
    /* Frees a macro definition node */
    if (md != NULL) {
        free(md->name.p);
        free(md->text.p);
        free(md);
    }
}

void free_mdef_linked_list(struct mdef *md)
{
    /* Frees the macro definiton doubly linked list */
    struct mdef *t = md, *next;
    while (t != NULL) {
        next = t->next;
        free_mdef_node(t);
        t = next;
    }
}

void free_margs_node(struct margs *ma)
{
    /* Free a macro arguments node (a stack node) */
    size_t i;
    if (ma != NULL) {
        for (i = 0; i < MAXARGS; ++i) {
            free_rear_buf(ma->args[i]);
        }
        free(ma);
    }
}

void free_margs_linked_list(struct margs *ma)
{
    /* Frees the macro arguments doubly linked list (the stack) */
    struct margs *t = ma, *next;
    while (t != NULL) {
        next = t->next;
        free_margs_node(t);
        t = next;
    }
}

struct margs *create_margs_node(void)
{
    /*
     * Creates a macro arguments node (a stack node).
     * Does not link it on on the stack.
     */
    size_t i;
    struct margs *ma;
    if ((ma = malloc(sizeof(struct margs))) == NULL)
        return NULL;
    ma->prev = NULL;
    ma->text.p = NULL;
    ma->text.s = 0;
    ma->bracket_depth = 0;
    ma->act_arg = 0;
    ma->next = NULL;
    for (i = 0; i < MAXARGS; ++i)
        ma->args[i] = NULL;
    for (i = 0; i < MAXARGS; ++i) {
        if ((ma->args[i] = init_rear_buf(SMALLGAP)) == NULL) {
            free_margs_node(ma);
            return NULL;
        }
    }
    return ma;
}

int stack_on_margs(struct margs **ma)
{
    /*
     * Creates a new margs node and links it on top of the stack creating
     * a new head.
     */
    struct margs *t;
    if ((t = create_margs_node()) == NULL)
        return 1;
    if (*ma != NULL) {
        t->next = *ma;
        (*ma)->prev = t;
    }
    *ma = t;
    return 0;
}

void delete_margs_stack_head(struct margs **ma)
{
    /*
     * Deletes the head margs node from the top of the macro arguments doubly
     * linked list. The new head will be the next node in the list, or NULL
     * if there are no more.
     */
    struct margs *t;
    /* Empty list, nothing to do */
    if (*ma == NULL)
        return;
    /* Store pointer to next node */
    t = (*ma)->next;
    /* Free head node */
    free_margs_node(*ma);
    /* Move the head down to the next node */
    *ma = t;
}

int filesize(char *fn, size_t * fs)
{
    /* Gets the filesize of a filename */
    struct stat st;
    if (stat(fn, &st))
        return 1;
    if (!S_ISREG(st.st_mode))
        return 1;
    if (st.st_size < 0)
        return 1;
    *fs = st.st_size;
    return 0;
}

int grow_rear_buf(struct rear_buf *rb, size_t fixed_chunk)
{
    size_t new_s;
    size_t increase;
    char *t;
    if (MOF(rb->s, GROWTH))
        return 1;
    if (AOF(rb->s * GROWTH, fixed_chunk))
        return 1;
    new_s = rb->s * GROWTH + fixed_chunk;
    increase = new_s - rb->s;
    if ((t = realloc(rb->p, new_s)) == NULL)
        return 1;
    rb->p = t;
    rb->s = new_s;
    rb->gs += increase;
    return 0;
}

int grow_front_buf(struct front_buf *fb, size_t fixed_chunk)
{
    size_t new_s;
    size_t increase;
    char *t;
    if (MOF(fb->s, GROWTH))
        return 1;
    if (AOF(fb->s * GROWTH, fixed_chunk))
        return 1;
    new_s = fb->s * GROWTH + fixed_chunk;
    increase = new_s - fb->s;
    if ((t = malloc(new_s)) == NULL)
        return 1;
    memcpy(t + fb->gs + increase, fb->p + fb->gs, TEXTSIZE(fb));
    fb->p = t;
    fb->s = new_s;
    fb->gs += increase;
    return 0;
}

int read_token(struct front_buf *input, struct rear_buf *token,
               int *end_of_input)
{
    /* Reads a token from the input */
    char ch;
    /* Clear the token */
    DELETEBUF(token);

    /* Check for end of input buffer */
    if (input->gs == input->s) {
        *end_of_input = 1;
        return 1;
    }
    *end_of_input = 0;

    /* Read first text char from the input front buffer */
    ch = *(input->p + input->gs);

    /* If the token gap is empty, make it bigger */
    if (!token->gs)
        if (grow_rear_buf(token, 0))
            return 1;

    /* Store the char at the end of the token text, reducing the rear gap */
    *(token->p + TEXTSIZE(token)) = ch;
    --token->gs;                /* Reduce token gap */
    ++input->gs;                /* Increase input gap (deletes the read char from the input) */

    if (ch == '_'
        || (ISASCII((unsigned char) ch) && isalpha((unsigned char) ch))) {
        /* Token could be a macro name */
        while (input->gs < input->s) {
            /* Read the rest of the token */
            ch = *(input->p + input->gs);

            /* Stop if the end of the token */
            if (ch != '_' && !(ISASCII((unsigned char) ch)
                               && isalnum((unsigned char) ch)))
                break;

            /* Increase token buffer gap if empty */
            if (!token->gs)
                if (grow_rear_buf(token, 0))
                    return 1;

            /* Store the char in the token, deleting it from the input */
            *(token->p + TEXTSIZE(token)) = ch;
            --token->gs;
            ++input->gs;
        }
    }
    return 0;
}

int insert_file(struct front_buf *fb, char *fn)
{
    /* Prepends a file into a front buffer */
    size_t fs;
    FILE *fp;
    if (filesize(fn, &fs))
        return 1;
    if (!fs)
        return 0;
    if (fb->gs < fs)
        if (grow_front_buf(fb, fs))
            return 1;
    if ((fp = fopen(fn, "rb")) == NULL)
        return 1;
    if (fread(fb->p + fb->gs - fs, 1, fs, fp) != fs) {
        fclose(fp);
        return 1;
    }
    if (fclose(fp))
        return 1;
    fb->gs -= fs;
    return 0;
}

int insert_rear_in_front_buf(struct front_buf *fb, struct rear_buf *rb)
{
    /*
     * Prepends a the text from a rear buffer into a front buffer.
     * Used to load stdin into a tmp rear buffer before transferring
     * to the input front buffer.
     */
    size_t ts = TEXTSIZE(rb);
    if (fb->gs < ts)
        if (grow_front_buf(fb, ts))
            return 1;
    memcpy(fb->p + fb->gs - ts, rb->p, ts);
    fb->gs -= ts;
    return 0;
}

int rear_buf_append_rear_buf(struct rear_buf *rb_dest,
                             struct rear_buf *rb_source)
{
    /* Appends source rear buffer to the end of the destination rear buffer */
    if (TEXTSIZE(rb_source) > rb_dest->gs
        && grow_rear_buf(rb_dest, TEXTSIZE(rb_source)))
        return 1;
    memcpy(rb_dest->p + TEXTSIZE(rb_dest), rb_source->p,
           TEXTSIZE(rb_source));
    rb_dest->gs -= TEXTSIZE(rb_source);
    return 0;
}

int sub_args(struct rear_buf *result, struct mem *text,
             struct rear_buf **args)
{
    /*
     * Substitutes the collected arguments into the macro definition.
     * Clear out the result buffer beforehand before calling this function.
     */
    int ret;
    size_t i;
    char ch;
    int dollar_encountered = 0;

    for (i = 0; i < text->s; ++i) {
        /* Read char from macro definition text */
        ch = *(text->p + i);

        if (ch == '$') {
            dollar_encountered = 1;
        } else if (dollar_encountered && ISASCII((unsigned char) ch)
                   && isdigit((unsigned char) ch)) {
            /* Insert argument */
            if (rear_buf_append_rear_buf
                (result, *(args + (unsigned char) ch)))
                return 1;
            dollar_encountered = 0;
        } else {
            if (dollar_encountered) {
                /* Insert a dollar sign to compensate */
                ret = 0;
                RBAPPENDCH(result, '$', ret);
                if (ret)
                    return 1;
                dollar_encountered = 0;
            }
            /* Insert the char */
            ret = 0;
            RBAPPENDCH(result, ch, ret);
            if (ret)
                return 1;
        }
    }
    return 0;
}

int read_stdin(struct rear_buf *rb)
{
    /* Reads from standard input and stores into a rear buffer */
    int x;
    while ((x = getchar()) != EOF) {
        if (!rb->gs)
            if (grow_rear_buf(rb, 0))
                return 1;
        *(rb->p + TEXTSIZE(rb)) = x;
        --rb->gs;
    }
    if (ferror(stdin) || !feof(stdin))
        return 1;
    return 0;
}

void print_token(struct rear_buf *token)
{
    /* This function is just for testing */
    size_t i = 0;
    size_t ts = TEXTSIZE(token);
    char ch;
    if (!ts)
        return;
    for (i = 0; i < ts; ++i) {
        ch = *(token->p + i);
        printf(isgraph(ch) ? "%c" : "%02X", ch);
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    int ret = 0;
    size_t fs;
    size_t total_fs = 0;
    struct rear_buf *tmp;
    struct front_buf *input;
    struct rear_buf *token = NULL;
    struct rear_buf *output;    /* This is a shortcut to the changing output */
    struct rear_buf *div[10];   /* The diversion output buffers 0 to 9 */
    int end_of_input = 0;       /* Indicates when the input is empty (like EOF) */
    int quote_on = 0;
    size_t quote_depth = 0;
    struct margs *ma = NULL;    /* Stack */
    struct mdef *md = NULL;     /* Macro definitions */
    struct mem *text_mem;
    /*
     * Result after arguments are substituted into macro definition
     * (before being pushed back into the input to be rescanned).
     */
    struct rear_buf *result;
    int j;

    if (argc < 1)
        return 1;

    if (argc == 1) {
        if ((tmp = init_rear_buf(LARGEGAP)) == NULL)
            return 1;
        if (read_stdin(tmp)) {
            free_rear_buf(tmp);
            return 1;
        }
        if (AOF(LARGEGAP, TEXTSIZE(tmp))) {
            free_rear_buf(tmp);
            return 1;
        }
        if ((input = init_front_buf(LARGEGAP + TEXTSIZE(tmp))) == NULL) {
            free_rear_buf(tmp);
            return 1;
        }
        if (insert_rear_in_front_buf(input, tmp)) {
            free_rear_buf(tmp);
            free_front_buf(input);
            return 1;
        }
        free_rear_buf(tmp);
    } else {
        for (j = 1; j < argc; ++j) {
            if (filesize(*(argv + j), &fs))
                return 1;
            total_fs += fs;
        }
        if (AOF(LARGEGAP, total_fs))
            return 1;
        if ((input = init_front_buf(LARGEGAP + total_fs)) == NULL)
            return 1;
        for (j = argc - 1; j; --j) {
            if (insert_file(input, *(argv + j))) {
                free_front_buf(input);
                return 1;
            }
        }
    }

    /* Setup diversion output buffers */
    for (j = 0; j < 10; ++j) {
        if ((*(div + j) = init_rear_buf(LARGEGAP)) == NULL) {
            ret = 1;
            goto clean_up;
        }
    }

    /* Set output shortcut to diversion 0 */
    output = *div;

    /* Setup token buffer */
    if ((token = init_rear_buf(SMALLGAP)) == NULL) {
        ret = 1;
        goto clean_up;
    }

    /*
     * Setup result buffer which is used to house the result of substituting
     * the arguments into the macro definition
     * (before being pushed back into the input to be rescanned).
     */
    if ((result = init_rear_buf(LARGEGAP)) == NULL) {
        ret = 1;
        goto clean_up;
    }

    /* Setup stack */
    if (stack_on_margs(&ma)) {
        ret = 1;
        goto clean_up;
    }

    /* Setup macro definition list */
    if (stack_on_mdef(&md)) {
        ret = 1;
        goto clean_up;
    }

    while (!read_token(input, token, &end_of_input)) {
        /*
         * How m4 works
         * ============
         * Nothing is interpreted when quotes are on, except that
         * the quote depth is recorded, so that it can be known
         * when to exit quote mode.
         * The quote information can be global (separate to the stack node)
         * as quotes must be off in order to enter a stack node.
         * The unquoted-backet depth must be recorded at a local stack node
         * level, to know when the collection of that macro's arguments has
         * completed, that is an unquoted right bracket has been encountered
         * creating a bracket depth of zero.
         * When the argument collection has finished the arguments are substituted
         * into the definiton and the result is then pushed back into the start
         * of the input (will be rescanned). From here the node is removed from the
         * top of the stack, and processing of the next node will resume with the
         * collection of the argument position that it was up to. This is why
         * the active argument is local information (at the stack node level).
         * Alphanumerical (plus underscore) tokens are always read from the same
         * input (which originated from reading all of stdin, or from concatenating
         * all of the command line files).
         * However, the output is complicated. If the stack is empty, then tokens
         * are written to the active diversion buffer (by default 0, but could be
         * any one of 0 to 9).
         * If the stack is being used, then the output is directed to the argument
         * being collected (could be argument 1 to 9). When an unquoted-comma is
         * encountered then the next argument will be collected.
         * Diversion buffers 1 to 9 can be undiverted into 0
         * (they are not scanned again).
         * At the end, diversion buffer 0 is printed to stdout.
         */

        if (!rear_buf_char_cmp(token, '`')) {
            /* Turn on quote mode if off */
            if (!quote_on)
                quote_on = 1;
            /* Go deeper in quote nesting (need to know when to get out again) */
            ++quote_depth;
        } else if (!rear_buf_char_cmp(token, '\'')) {
            /*
             * Turn off quote mode if exited from nested quotes
             * (the depth must be zero afterwards)
             */
            if (!--quote_depth)
                quote_on = 0;
        } else if (!quote_on) {
            /* Quotes off */

            if (ma != NULL && !rear_buf_char_cmp(token, ')')) {
                /* Todo: check backet depth */
                /* End of argument collection */
                /* Clear out result buffer */
                DELETEBUF(result);

                /* Substitute arguments into defintion */
                if (sub_args(result, &ma->text, ma->args)) {
                    ret = 1;
                    goto clean_up;
                }

                /* Push result into input */
                if (insert_rear_in_front_buf(input, result)) {
                    ret = 1;
                    goto clean_up;
                }

                /* Remove stack head */
                delete_margs_stack_head(&ma);

                /* Repoint output shortcut */
                if (ma == NULL)
                    /* Set output shortcut to diversion 0 */
                    output = *div;
                else
                    /* The active argument collection of the new stack head */
                    output = *(ma->args + ma->act_arg);

            } else if ((text_mem = token_search(md, token)) != NULL) {
                /* Look up token in macro definition doubly linked list */

                /*
                 * Create a new stack node and copy the text.
                 * The active argument will be set to zero.
                 */
                if (stack_on_margs(&ma)) {
                    ret = 1;
                    goto clean_up;
                }

                /* Copy definition (as this may change later */
                if (mem_to_mem_cpy(&ma->text, text_mem)) {
                    ret = 1;
                    goto clean_up;
                }

                /* Repoint output shortcut */
                output = *(ma->args + ma->act_arg);
            } else {
                /* Copy token to output */
                if (rear_buf_append_rear_buf(output, token)) {
                    ret = 0;
                    goto clean_up;
                }

            }
        } else {
            /* Quotes on, so just copy token to output */
            if (rear_buf_append_rear_buf(output, token)) {
                ret = 0;
                goto clean_up;
            }
        }
    }

    if (fwrite((*div)->p, 1, TEXTSIZE(*div), stdout) != TEXTSIZE(*div)) {
        ret = 1;
        goto clean_up;
    }

  clean_up:
    if (!end_of_input)
        ret = 1;
    free_front_buf(input);
    free_rear_buf(token);
    free_margs_linked_list(ma);
    return ret;
}
