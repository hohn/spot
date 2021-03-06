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

#include <sys/types.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Index 0 is the macro name, indices 1 to 9 are macro arguments */
#define MAXARGS 10
/* Number of positive diversions (0 to 9) */
#define NUM_NON_NEG_DIVS 10
/* Number of diversions (0 to 9 and -1) */
#define NUM_DIVS 11

#define GROWTH 2
#define LARGEGAP 2
#define SMALLGAP 1

/* Built-in macro identifiers (user defined macros are use 0) */
/* The define macro */
#define BI_DEFINE 1
/* The undefine macro */
#define BI_UNDEFINE 2
/* The divert macro */
#define BI_DIVERT 3
/* The undivert macro */
#define BI_UNDIVERT 4
/* The divnum macro */
#define BI_DIVNUM 5
/* The changequote macro */
#define BI_CHANGEQUOTE 6
/* The include macro */
#define BI_INCLUDE 7
/* The dnl macro */
#define BI_DNL 8
/* The esyscmd macro */
#define BI_ESYSCMD 9
/* The ifdef macro */
#define BI_IFDEF 10
/* The ifelse macro */
#define BI_IFELSE 11
/* The dumpdef macro */
#define BI_DUMPDEF 12
/* The errprint macro */
#define BI_ERRPRINT 13
/* The traceon macro */
#define BI_TRACEON 14
/* The traceoff macro */
#define BI_TRACEOFF 15

/* size_t overflow checks */
#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

/* These work on both rear and front buffers */
#define TEXTSIZE(b) ((b)->s - (b)->gs)
#define DELETEBUF(b) ((b)->gs = (b)->s)

/*
 * Appends a char to the end of a rear buffer.
 * Clear ret to zero before calling this macro.
 */
#define RB_APPEND_CH(rb, ch, ret) do { \
    if (!(rb)->gs && grow_rear_buf(rb, 1)) { \
        ret = 1; \
    } else { \
        *((rb)->p + TEXTSIZE(rb)) = ch; \
        (rb)->gs -= 1; \
    } \
} while (0)

/*
 * Test to see if the first char of a rear buffer is an alpha char
 * or an underscore. Useful for avoiding expensive functions calls
 * that require a valid macro name.
 */
#define AU(rb) ((rb)->s && (*(rb)->p == '_' || isalpha((unsigned char) *(rb)->p)))

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
    int built_in;               /* Built-in macro identifier */
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
    int built_in;               /* Built-in macro identifier */
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
    md->built_in = 0;           /* Default is 0: User defined macro */
    md->next = NULL;
    return md;
}

int stack_on_mdef(struct mdef **md)
{
    /*
     * Add a new macro definition node to top of the macro definition
     * doubly linked list (becomes the new head).
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
    /* Compares the text of a rear buffer to a mem byte string */
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

struct mem *token_search(struct mdef *md, struct rear_buf *token, int *bi)
{
    /*
     * Searches for a token in the macro names of the macro definition doubly
     * linked list. If found it returns the replacement text (before the
     * arguments are substituted), else it returns NULL. It also sets bi
     * if a match is found, which is the built-in identifier (user defined
     * macros have a built-in identifier of zero).
     */
    struct mdef *t = md, *next;
    while (t != NULL) {
        next = t->next;
        if (!rear_buf_mem_cmp(token, &t->name)) {
            *bi = t->built_in;
            return &t->text;
        }
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
        /* Arg 0 is not allocated, so no need to free */
        for (i = 1; i < MAXARGS; ++i) {
            free_rear_buf(*(ma->args + i));
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

void print_token(struct rear_buf *token)
{
    /* Prints a rear buffer to stderr, such as a token (used by traceon) */
    size_t i = 0;
    size_t ts = TEXTSIZE(token);
    char ch;
    if (!ts)
        return;
    for (i = 0; i < ts; ++i) {
        ch = *(token->p + i);
        if (isgraph((unsigned char) ch))
            putc(ch, stderr);
        else
            fprintf(stderr, "%02X", (unsigned char) ch);
    }
    putc('\n', stderr);
}

int dump_stack(struct margs *ma)
{
    /* Prints the macro arguments linked list to stderr (used in traceon) */
    struct margs *t = ma, *next;
    size_t node = 0, i, s;

    fprintf(stderr, "*** Dump stack ***\n");

    while (t != NULL) {
        next = t->next;

        fprintf(stderr, "NODE: %lu\n", (unsigned long) node);
        fprintf(stderr, "Macro text: ");
        if (t->text.p == NULL)
            fprintf(stderr, "NULL");
        else if (fwrite(t->text.p, 1, t->text.s, stderr) != t->text.s)
            return 1;
        fprintf(stderr, "\nMacro text size: %lu\n",
                (unsigned long) t->text.s);
        fprintf(stderr, "Bracket depth: %lu\n",
                (unsigned long) t->bracket_depth);
        fprintf(stderr, "Active argument: %lu\n",
                (unsigned long) t->act_arg);

        /* Arg 0 is not allocated */
        for (i = 1; i < MAXARGS; ++i) {
            s = TEXTSIZE(*(t->args + i));
            if (s) {
                fprintf(stderr, "Argument %lu: ", (unsigned long) i);
                if (fwrite((*(t->args + i))->p, 1, s, stderr)
                    != s)
                    return 1;
                putc('\n', stderr);
            }
        }

        fprintf(stderr, "Built-in macro identifier: %d\n", t->built_in);

        ++node;
        t = next;
    }
    return 0;
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
    ma->act_arg = 1;            /* Ignore macro name for now, fix this later ... 0; */
    ma->next = NULL;
    for (i = 0; i < MAXARGS; ++i)
        *(ma->args + i) = NULL;
    /* Do not need to allocate arg 0 as it is not used */
    for (i = 1; i < MAXARGS; ++i) {
        if ((*(ma->args + i) = init_rear_buf(SMALLGAP)) == NULL) {
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
    /* Store pointer to next node. Will be NULL if there are no more */
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
    if (!((st.st_mode & S_IFMT) == S_IFREG))
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

    if (ch == '_' || isalpha((unsigned char) ch)) {
        /* Token could be a macro name */
        while (input->gs < input->s) {
            /* Read the rest of the token */
            ch = *(input->p + input->gs);

            /* Stop if the end of the token */
            if (ch != '_' && !isalnum((unsigned char) ch))
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

int insert_ch_in_front_buf(struct front_buf *fb, char ch)
{
    /*
     * Prepends a character into a front buffer.
     * Used to push a char into the input.
     */
    if (!fb->gs)
        if (grow_front_buf(fb, 1))
            return 1;
    *(fb->p + fb->gs - 1) = ch;
    --fb->gs;
    return 0;
}

int rear_buf_append_rear_buf(struct rear_buf *rb_dest,
                             struct rear_buf *rb_source)
{
    /* Appends source rear buffer to the end of the destination rear buffer */
    /* No storage */
    if (rb_dest == NULL)
        return 1;
    /* Nothing to copy */
    if (rb_source == NULL)
        return 0;
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
        } else if (dollar_encountered && isdigit((unsigned char) ch)
                   && ch != '0') {
            /* Insert argument (arg 0 is not allocated) */
            if (rear_buf_append_rear_buf
                (result, *(args + (unsigned char) ch - '0')))
                return 1;
            dollar_encountered = 0;
        } else {
            if (dollar_encountered) {
                /* Insert a dollar sign to compensate */
                ret = 0;
                RB_APPEND_CH(result, '$', ret);
                if (ret)
                    return 1;
                dollar_encountered = 0;
            }
            /* Insert the char */
            ret = 0;
            RB_APPEND_CH(result, ch, ret);
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

int add_built_in_macro(struct mdef **md, char *name_str, int bi)
{
    /*
     * Adds a built_in macro definition to the macro definition linked list.
     * There is no need to check that the macro already exists as this is only
     * called internally.
     */
    size_t s = strlen(name_str);
    /*
     * Add a new macro definition linked list head node.
     * This could be the first node, in which case the macro definition list
     * will commence.
     */
    if (stack_on_mdef(md))
        return 1;

    if (((*md)->name.p = malloc(s)) == NULL)
        return 1;
    memcpy((*md)->name.p, name_str, s);
    (*md)->name.s = s;
    (*md)->built_in = bi;
    return 0;
}

void undefine_macro(struct mdef **md, struct rear_buf *macro_name)
{
    /*
     * Searches for macro_name in the macro definition linked list,
     * and if found, removes it.
     */
    struct mdef *t = *md, *next;
    /* Case 1: Match occurs at head node */
    if (!rear_buf_mem_cmp(macro_name, &t->name)) {
        /* Repoint head */
        *md = t->next;
        /* NULL the previous of the new head */
        (*md)->prev = NULL;
        /* Free old head */
        free_mdef_node(t);
        return;
    }
    /*
     * Case 2: Match occurs not at the head. In this case there is no need to
     * reset *md as this is the head node already.
     */
    /* Search for the match */
    t = t->next;
    while (t != NULL) {
        next = t->next;
        if (!rear_buf_mem_cmp(macro_name, &t->name)) {
            /* Match */
            /* Bypass (link around t) */
            t->prev->next = t->next;
            if (t->next != NULL)
                t->next->prev = t->prev;
            /* Free cutout node */
            free_mdef_node(t);
            return;
        }
        t = next;
    }
}

int undivert(struct rear_buf *dest, struct rear_buf *source)
{
    /*
     * Appends the source diversion buffer onto the end of the destination
     * diversion buffer, and empties the source diversion buffer.
     * Any diversion buffer (0 to 9 and -1) can be undiverted into any
     * diversion buffer. It does not change the active diversion index.
     */
    if (rear_buf_append_rear_buf(dest, source))
        return 1;
    DELETEBUF(source);
    return 0;
}

int divnum_index(struct rear_buf *rb, size_t * index)
{
    /* Converts a divnum text to a divnum index */
    size_t ts = TEXTSIZE(rb);
    /* Empty divnum does not default to 0, it is invalid  */
    if (!ts)
        return 1;
    /* Non-negative divnum can only be one digit long (0 to 9) */
    if (ts == 1 && isdigit((unsigned char) *rb->p)) {
        *index = *rb->p - '0';
        return 0;
    }
    /* The only negative divnum allowed is -1 which maps to index 10 */
    if (ts == 2 && *rb->p == '-' && *(rb->p + 1) == '1') {
        *index = 10;            /* Used for divnum -1 */
        return 0;
    }
    return 1;
}

char *rear_buf_to_str(struct rear_buf *rb)
{
    /*
     * Converts a rear buffer to a string by stripping out \0 characters.
     * The string is dynamically allocated and needs to be freed later.
     */
    char *t, *q, ch;
    size_t s, j;
    s = TEXTSIZE(rb);
    if (AOF(s, 1))
        return NULL;
    if ((t = malloc(s + 1)) == NULL)
        return NULL;
    q = t;
    /* Strip out \0 characters */
    for (j = 0; j < s; ++j) {
        ch = *(rb->p + j);
        if (ch != '\0')
            *q++ = ch;
    }
    *q = '\0';
    return t;
}

int sh_cmd_to_rear_buf(struct rear_buf *result, char *cmd_str)
{
    /*
     * Executes a shell command and appends the result to the end of the
     * rear buffer.
     */
    FILE *fp;
    int x, status;

#ifdef _WIN32
    if ((fp = _popen(cmd_str, "rb")) == NULL)
        return 1;
#else
    if ((fp = popen(cmd_str, "r")) == NULL)
        return 1;
#endif

    /* Read the command output into a rear buffer */
    while ((x = getc(fp)) != EOF) {
        if (!result->gs && grow_rear_buf(result, 1))
            return 1;
        *(result->p + TEXTSIZE(result)) = x;
        --result->gs;
    }

    /* Close the process */
#ifdef _WIN32
    if ((status = _pclose(fp)) == -1)
        return 1;
#else
    if ((status = pclose(fp)) == -1)
        return 1;
#endif

    /* Check if the shell command was successful */
#ifdef _WIN32
    if (!status)
        return 0;
#else
    if (WIFEXITED(status) && !WEXITSTATUS(status))
        return 0;
#endif
    return 1;
}

int dumpdef_all(struct mdef *md)
{
    /*
     * Prints a macro name and the corresponding replacement text to stderr.
     * Does this for either one macro or all macros.
     */
    struct mdef *t = md, *next;
    size_t node = 0;

    while (t != NULL) {
        next = t->next;

        if (t->name.p == NULL)
            fprintf(stderr, "NULL");
        else if (fwrite(t->name.p, 1, t->name.s, stderr) != t->name.s)
            return 1;
        fprintf(stderr, ": ");

        if (t->built_in)
            fprintf(stderr, "Built-in");
        else if (t->text.p == NULL)
            fprintf(stderr, "NULL");
        else if (fwrite(t->text.p, 1, t->text.s, stderr) != t->text.s)
            return 1;
        putc('\n', stderr);

        ++node;
        t = next;
    }
    return 0;
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
    /* Diversion output buffers 0 to 9 and -1. Diversion -1 maps to index 10 */
    struct rear_buf *div[NUM_DIVS];
    size_t act_div = 0;         /* Active diversion */
    size_t tmp_index;           /* Temporary divnum index */
    int end_of_input = 0;       /* Indicates when the input is empty (like EOF) */
    int quote_on = 0;
    size_t quote_depth = 0;
    struct margs *ma = NULL;    /* Stack */
    struct mdef *md = NULL;     /* Macro definitions */
    struct mem *text_mem;
    int bi;                     /* Built-in identifier of matched macro */
    /*
     * Result after arguments are substituted into macro definition
     * (before being pushed back into the input to be rescanned).
     */
    struct rear_buf *result;
    size_t s;                   /* Temp size variable */
    char *tmp_str;              /* Temporary string */
    int last_match = 0;         /* Last token read was a macro match */
    int eat_whitespace = 0;     /* Eat input whitespace */
    char left_quote = '`';      /* Left quote: default is backtick */
    char right_quote = '\'';    /* Right quote: default is single quote */
    int trace = 0;              /* Indicates if trace is on */
    int i;
    size_t j;
    char *q;

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
        for (i = 1; i < argc; ++i) {
            if (filesize(*(argv + i), &fs))
                return 1;
            total_fs += fs;
        }
        if (AOF(LARGEGAP, total_fs))
            return 1;
        if ((input = init_front_buf(LARGEGAP + total_fs)) == NULL)
            return 1;
        for (i = argc - 1; i; --i) {
            if (insert_file(input, *(argv + i))) {
                free_front_buf(input);
                return 1;
            }
        }
    }

    /* Setup diversion output buffers */
    for (j = 0; j < NUM_DIVS; ++j) {
        if ((*(div + j) = init_rear_buf(LARGEGAP)) == NULL) {
            ret = 1;
            goto clean_up;
        }
    }

    /* Set output shortcut to active diversion  */
    output = *(div + act_div);

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

    /* Do not need to setup the stack, it is created on demand */

    /*
     * Add the define built-in macro.
     * This will commmence the macro definition linked list.
     */
    if (add_built_in_macro(&md, "define", BI_DEFINE)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the undefine nuilt-in macro */
    if (add_built_in_macro(&md, "undefine", BI_UNDEFINE)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the divert built-in macro */
    if (add_built_in_macro(&md, "divert", BI_DIVERT)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the undivert built-in macro */
    if (add_built_in_macro(&md, "undivert", BI_UNDIVERT)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the divnum built-in macro */
    if (add_built_in_macro(&md, "divnum", BI_DIVNUM)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the changequote built-in macro */
    if (add_built_in_macro(&md, "changequote", BI_CHANGEQUOTE)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the include built-in macro */
    if (add_built_in_macro(&md, "include", BI_INCLUDE)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the dnl built-in macro */
    if (add_built_in_macro(&md, "dnl", BI_DNL)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the esyscmd built-in macro */
    if (add_built_in_macro(&md, "esyscmd", BI_ESYSCMD)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the ifdef built-in macro */
    if (add_built_in_macro(&md, "ifdef", BI_IFDEF)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the ifelse built-in macro */
    if (add_built_in_macro(&md, "ifelse", BI_IFELSE)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the dumpdef built-in macro */
    if (add_built_in_macro(&md, "dumpdef", BI_DUMPDEF)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the errprint built-in macro */
    if (add_built_in_macro(&md, "errprint", BI_ERRPRINT)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the traceon built-in macro */
    if (add_built_in_macro(&md, "traceon", BI_TRACEON)) {
        ret = 1;
        goto clean_up;
    }

    /* Add the traceoff built-in macro */
    if (add_built_in_macro(&md, "traceoff", BI_TRACEOFF)) {
        ret = 1;
        goto clean_up;
    }


    /* The m4 loop */
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
         * any one of 0 to 9 or -1).
         * If the stack is being used, then the output is directed to the argument
         * being collected (could be argument 1 to 9). When an unquoted-comma is
         * encountered then the next argument will be collected.
         * Any diversion buffer (0 to 9 or -1) can be undiverted into the active
         * diversion except for the active diversion itself (there is no rescanning).
         * At the end, diversion buffers 0 to 9 are printed to stdout in order.
         * Diversion -1 is not written to stdout automatically (this is the only
         * thing that makes diversion -1 different), however, it can be undiverted
         * into another diversion before the program finishes.
         */

        if (trace) {
            fprintf(stderr, "Token: ");
            print_token(token);
        }

        if (!rear_buf_char_cmp(token, left_quote)) {
            /* TURN ON QUOTE mode if off */
            if (!quote_on)
                quote_on = 1;
            /* Pass through to output if a nested quote */
            if (quote_depth && rear_buf_append_rear_buf(output, token)) {
                ret = 0;
                goto clean_up;
            }
            /* Go deeper in quote nesting (need to know when to get out again) */
            ++quote_depth;
            eat_whitespace = 0;
            last_match = 0;
        } else if (!rear_buf_char_cmp(token, right_quote)) {
            /*
             * TURN OFF QUOTE mode if exited from nested quotes
             * (the depth must be zero afterwards)
             */
            /* Pass through to output if not about to exit quoting mode */
            if (quote_depth > 1 && rear_buf_append_rear_buf(output, token)) {
                ret = 0;
                goto clean_up;
            }
            if (!--quote_depth)
                quote_on = 0;
            last_match = 0;
        } else if (!quote_on) {
            /* QUOTES OFF */

            if (ma != NULL && ma->bracket_depth == 1
                && !rear_buf_char_cmp(token, ')')) {
                /* END OF ARGUMENT collection */

                /* Decrement unquoted backet depth to zero */
                --ma->bracket_depth;

                /* If trace is on, then dump the stack */
                if (trace)
                    if (dump_stack(ma)) {
                        ret = 1;
                        goto clean_up;
                    }

                /* Check for BUILT-IN MACROS */

                /*
                 * THE define MACRO. To define a macro it must start
                 * with a letter or an underscore.
                 */
                if (ma->built_in == BI_DEFINE && AU(*(ma->args + 1))) {
                    /* Undefine the macro if it is already defined */
                    undefine_macro(&md, *(ma->args + 1));

                    /* Make a new mdef head */
                    if (stack_on_mdef(&md)) {
                        ret = 1;
                        goto clean_up;
                    }
                    /* Copy the user defined macro name */
                    s = TEXTSIZE(*(ma->args + 1));
                    if ((md->name.p = malloc(s)) == NULL) {
                        ret = 1;
                        goto clean_up;
                    }
                    memcpy(md->name.p, (*(ma->args + 1))->p, s);
                    md->name.s = s;
                    md->built_in = 0;   /* User defined */
                    /* Copy the user defined macro replacement text */
                    s = TEXTSIZE(*(ma->args + 2));
                    if ((md->text.p = malloc(s)) == NULL) {
                        ret = 1;
                        goto clean_up;
                    }
                    memcpy(md->text.p, (*(ma->args + 2))->p, s);
                    md->text.s = s;

                } else if (ma->built_in == BI_UNDEFINE) {
                    /* THE undefine MACRO */
                    for (j = 0; j < MAXARGS; ++j) {
                        if (AU(*(ma->args + j))) {
                            /* Undefine the macro if it is already defined */
                            undefine_macro(&md, *(ma->args + j));
                        }
                    }
                } else if (ma->built_in == BI_DIVERT) {
                    /* THE divert MACRO */
                    if (divnum_index(*(ma->args + 1), &act_div)) {
                        fprintf(stderr,
                                "%s: divert: Invalid divnum supplied\n",
                                *argv);
                        ret = 1;
                        goto clean_up;
                    }
                    /* No need to refresh the output shortcut as this will happen later */
                } else if (ma->built_in == BI_UNDIVERT) {
                    for (j = 1; j < MAXARGS; ++j) {
                        /* THE undivert MACRO */
                        /* Skip empty arguments */
                        if (TEXTSIZE(*(ma->args + j))) {
                            if (divnum_index(*(ma->args + j), &tmp_index)) {
                                fprintf(stderr,
                                        "%s: undivert: Invalid divnum supplied at argument: %lu\n",
                                        *argv, (unsigned long) j);
                                ret = 1;
                                goto clean_up;
                            }
                            /* Cannot undivert the active diversion into itself */
                            if (tmp_index == act_div) {
                                fprintf(stderr,
                                        "%s: undivert: Cannot undivert the active diversion (%d) into itself\n",
                                        *argv,
                                        act_div !=
                                        10 ? (int) act_div : -1);
                                ret = 1;
                                goto clean_up;
                            }

                            /* Undivert into the active diversion (does not change the active diversion index) */
                            if (undivert
                                (*(div + act_div), *(div + tmp_index))) {
                                ret = 1;
                                goto clean_up;
                            }
                        }
                    }
                } else if (ma->built_in == BI_CHANGEQUOTE) {
                    /* THE changequote MACRO */
                    /* The quotes must be different, single, graph characters */
                    if (TEXTSIZE(*(ma->args + 1)) != 1
                        || TEXTSIZE(*(ma->args + 2)) != 1
                        || !isgraph(*(*(ma->args + 1))->p)
                        || !isgraph(*(*(ma->args + 2))->p)
                        || *(*(ma->args + 1))->p == *(*(ma->args + 2))->p) {
                        fprintf(stderr,
                                "%s: changequote: Invalid arguments\n",
                                *argv);
                        ret = 1;
                        goto clean_up;
                    }
                    left_quote = *(*(ma->args + 1))->p;
                    right_quote = *(*(ma->args + 2))->p;

                } else if (ma->built_in == BI_INCLUDE) {
                    /* THE include MACRO */
                    /* Convert arg 1 into the filename */
                    if ((tmp_str =
                         rear_buf_to_str(*(ma->args + 1))) == NULL) {
                        ret = 1;
                        goto clean_up;
                    }
                    if (insert_file(input, tmp_str)) {
                        fprintf(stderr,
                                "%s: include: failed to insert file: %s\n",
                                *argv, tmp_str);
                        free(tmp_str);
                        ret = 1;
                        goto clean_up;
                    }
                    free(tmp_str);
                } else if (ma->built_in == BI_ESYSCMD) {
                    /* THE esyscmd MACRO */
                    /* Convert arg 1 into the shell command string */
                    if ((tmp_str =
                         rear_buf_to_str(*(ma->args + 1))) == NULL) {
                        ret = 1;
                        goto clean_up;
                    }
                    DELETEBUF(result);
                    if (sh_cmd_to_rear_buf(result, tmp_str)) {
                        fprintf(stderr,
                                "%s: esyscmd: shell command failed\n",
                                *argv);
                        free(tmp_str);
                        ret = 1;
                        goto clean_up;
                    }
                    free(tmp_str);
                    /* Push result into input */
                    if (insert_rear_in_front_buf(input, result)) {
                        ret = 1;
                        goto clean_up;
                    }

                } else if (ma->built_in == BI_IFDEF) {
                    /* THE ifdef MACRO */
                    if (!TEXTSIZE(*(ma->args + 1))) {
                        fprintf(stderr,
                                "%s: ifdef: first argument cannot be empty\n",
                                *argv);
                        ret = 1;
                        goto clean_up;
                    }
                    if (AU(*(ma->args + 1))
                        && token_search(md, *(ma->args + 1), &bi) != NULL) {
                        /* It is defined */
                        /* Push arg 2 into input */
                        if (TEXTSIZE(*(ma->args + 2))
                            && insert_rear_in_front_buf(input,
                                                        *(ma->args + 2))) {
                            ret = 1;
                            goto clean_up;
                        }
                    } else {
                        /* Is not defined */
                        /* Push arg 3 into input */
                        if (TEXTSIZE(*(ma->args + 3))
                            && insert_rear_in_front_buf(input,
                                                        *(ma->args + 3))) {
                            ret = 1;
                            goto clean_up;
                        }
                    }
                } else if (ma->built_in == BI_IFELSE) {
                    /* THE ifelse MACRO */
                    /* Empty arguments compare equal */
                    if ((*(ma->args + 1))->s == (*(ma->args + 2))->s
                        && !memcmp((*(ma->args + 1))->p,
                                   (*(ma->args + 2))->p,
                                   (*(ma->args + 1))->s)) {
                        /* Arg 1 and 2 are equal */
                        /* Push arg 3 into input */
                        if (TEXTSIZE(*(ma->args + 3))
                            && insert_rear_in_front_buf(input,
                                                        *(ma->args + 3))) {
                            ret = 1;
                            goto clean_up;
                        }
                    } else {
                        /* Push arg 4 into input */
                        if (TEXTSIZE(*(ma->args + 4))
                            && insert_rear_in_front_buf(input,
                                                        *(ma->args + 4))) {
                            ret = 1;
                            goto clean_up;
                        }
                    }

                } else if (ma->built_in == BI_DUMPDEF) {
                    /* THE dumpdef MACRO */
                    for (j = 1; j < MAXARGS; ++j) {
                        if (TEXTSIZE(*(ma->args + j))) {
                            /* Write ONE macro name to stderr */
                            if (fwrite
                                ((*(ma->args + j))->p, 1,
                                 TEXTSIZE(*(ma->args + j)),
                                 stderr) != TEXTSIZE(*(ma->args + j))) {
                                ret = 1;
                                goto clean_up;
                            }
                            fprintf(stderr, ": ");

                            /* Look up the macro name */
                            if (AU(*(ma->args + j))
                                && (text_mem =
                                    token_search(md, *(ma->args + j),
                                                 &bi)) != NULL) {
                                /* Match */
                                /* If user defined macro */
                                if (!bi) {
                                    /* Write macro replacement text */
                                    if (fwrite
                                        (text_mem->p, 1, text_mem->s,
                                         stderr) != text_mem->s) {
                                        ret = 1;
                                        goto clean_up;
                                    }
                                    putc('\n', stderr);
                                } else {
                                    /* Built-in */
                                    fprintf(stderr, "Built-in\n");
                                }
                            } else {
                                /* No match */
                                fprintf(stderr, "Undefined\n");
                            }
                        }
                    }
                } else if (ma->built_in == BI_ERRPRINT) {
                    /* THE errprint MACRO */
                    /* Write arguments to stderr */
                    for (j = 1; j < MAXARGS; ++j) {
                        s = TEXTSIZE(*(ma->args + j));
                        if (s) {
                            if (fwrite((*(ma->args + j))->p, 1, s,
                                       stderr) != s) {
                                ret = 1;
                                goto clean_up;
                            }
                            putc('\n', stderr);
                        }
                    }
                } else {
                    /* USER DEFINED MACROS */
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
                }
                /* Remove stack head */
                delete_margs_stack_head(&ma);

                /* Repoint output shortcut */
                if (ma == NULL) {
                    /* Set output shortcut to active diversion  */
                    output = *(div + act_div);
                } else {
                    /* The active argument collection of the new stack head */
                    output = *(ma->args + ma->act_arg);
                }
                last_match = 0;

            } else if (ma != NULL && ma->bracket_depth > 1
                       && !rear_buf_char_cmp(token, ')')) {
                /* NESTED UNQUOTED CLOSE BRACKET */
                /* Pass through to output */
                if (rear_buf_append_rear_buf(output, token)) {
                    ret = 0;
                    goto clean_up;
                }
                --ma->bracket_depth;
                eat_whitespace = 1;

            } else if (last_match && ma != NULL
                       && !rear_buf_char_cmp(token, '(')) {
                /* EAT THE OPEN BRACKET after a macro name */
                /* Increment bracket depth */
                ++ma->bracket_depth;
                eat_whitespace = 1;
                last_match = 0;

            } else if (!last_match && ma != NULL
                       && !rear_buf_char_cmp(token, '(')) {
                /* NESTED UNQUOTED OPEN BRACKET */
                /* Pass through to output */
                if (rear_buf_append_rear_buf(output, token)) {
                    ret = 0;
                    goto clean_up;
                }
                ++ma->bracket_depth;
                eat_whitespace = 1;

            } else if (last_match && ma != NULL
                       && rear_buf_char_cmp(token, '(')) {
                /* Macro called with NO BRACKETS (no arguments) */
                /* Process built-in macro that take no arguments first */
                if (ma->built_in == BI_DIVNUM) {
                    /* THE divnum MACRO */
                    /* Put the token back on the input */
                    if (insert_rear_in_front_buf(input, token)) {
                        ret = 1;
                        goto clean_up;
                    }
                    if (act_div != 10) {
                        /* Push back into input to be rescanned later */
                        if (insert_ch_in_front_buf(input, '0' + act_div)) {
                            ret = 1;
                            goto clean_up;
                        }
                    } else {
                        /* divnum -1 is index 10 */
                        /* Push back -1 into input to be rescanned later */
                        if (insert_ch_in_front_buf(input, '1')) {
                            ret = 1;
                            goto clean_up;
                        }
                        if (insert_ch_in_front_buf(input, '-')) {
                            ret = 1;
                            goto clean_up;
                        }
                    }
                } else if (ma->built_in == BI_DNL) {
                    /* THE dnl MACRO */
                    /* Put the token back on the input */
                    if (insert_rear_in_front_buf(input, token)) {
                        ret = 1;
                        goto clean_up;
                    }
                    /* Search for newline character */
                    if ((q =
                         memchr(input->p + input->gs, '\n',
                                input->s - input->gs)) != NULL) {
                        /* Delete up to and including the newline character */
                        input->gs = q - input->p + 1;
                    }

                } else if (ma->built_in == BI_DIVERT) {
                    /* THE divert MACRO  -- No brackets (arguments) */
                    /* Defaults to diversion 0 */
                    act_div = 0;
                    /* No need to refresh the output shortcut as this will happen later */
                } else if (ma->built_in == BI_UNDIVERT) {
                    /* THE undivert MACRO -- No brackets (arguments) */
                    /* Default is to undivert all diversions (except for diversion -1 and the active diversion) into the active diversion */
                    for (j = 0; j < NUM_NON_NEG_DIVS; ++j) {
                        /* Undivert into the active diversion (does not change the active diversion index) */
                        if (j != act_div && undivert
                            (*(div + act_div), *(div + tmp_index))) {
                            ret = 1;
                            goto clean_up;
                        }
                    }
                } else if (ma->built_in == BI_CHANGEQUOTE) {
                    /* THE changequote MACRO -- No brackets (arguments) */
                    /* Restore default quotes */
                    left_quote = '`';
                    right_quote = '\'';
                } else if (ma->built_in == BI_DUMPDEF) {
                    /* THE dumpdef MACRO -- No brackets (arguments) */
                    /* Write ALL macros to stderr */
                    if (dumpdef_all(md)) {
                        ret = 1;
                        goto clean_up;
                    }
                } else if (ma->built_in == BI_TRACEON) {
                    /* THE traceon MACRO -- No brackets (arguments) */
                    trace = 1;
                } else if (ma->built_in == BI_TRACEOFF) {
                    /* THE traceoff MACRO -- No brackets (arguments) */
                    trace = 0;
                } else {
                    /* USER DEFINED MACRO WITH NO ARGUMENT BRACKETS */
                    /* Put the token back on the input */
                    if (insert_rear_in_front_buf(input, token)) {
                        ret = 1;
                        goto clean_up;
                    }

                    /* Clear out result buffer */
                    DELETEBUF(result);

                    /* If trace is on, then dump the stack */
                    if (trace)
                        if (dump_stack(ma)) {
                            ret = 1;
                            goto clean_up;
                        }

                    /*
                     * Substitute arguments into definition.
                     * No arguments have been collected, but this is still
                     * useful as it removes the argument substitution placeholders
                     * ($1 $2... etc) from the definition text.
                     */
                    if (sub_args(result, &ma->text, ma->args)) {
                        ret = 1;
                        goto clean_up;
                    }

                    /* Push result into input */
                    if (insert_rear_in_front_buf(input, result)) {
                        ret = 1;
                        goto clean_up;
                    }
                }

                /* Remove stack head */
                delete_margs_stack_head(&ma);

                /* Repoint output shortcut */
                if (ma == NULL) {
                    /* Set output shortcut to active diversion  */
                    output = *(div + act_div);
                } else {
                    /* The active argument collection of the new stack head */
                    output = *(ma->args + ma->act_arg);
                }
                eat_whitespace = 1;
                last_match = 0;

            } else if (ma != NULL && ma->bracket_depth == 1
                       && !rear_buf_char_cmp(token, ',')) {
                /* COMMA, so advance to next argument */
                if (ma->act_arg == MAXARGS - 1) {
                    fprintf(stderr,
                            "Macro call has more than %d arguments\n",
                            MAXARGS - 1);
                    ret = 1;
                    goto clean_up;
                } else {
                    ++ma->act_arg;
                }
                /* Refresh output shortcut */
                output = *(ma->args + ma->act_arg);
                eat_whitespace = 1;
                last_match = 0;
            } else if (AU(token)
                       && (text_mem =
                           token_search(md, token, &bi)) != NULL) {
                /*
                 * MATCHED TOKEN in macro definition list.
                 * All macro names must start with a letter or underscore,
                 * so no point in searching otherwise.
                 */

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

                /* Copy the built-in identifier */
                ma->built_in = bi;

                /* Repoint output shortcut */
                output = *(ma->args + ma->act_arg);

                last_match = 1;

            } else {
                /* EAT WHITESPACE */
                if (eat_whitespace && (!rear_buf_char_cmp(token, ' ')
                                       || !rear_buf_char_cmp(token, '\t')
                                       || !rear_buf_char_cmp(token, '\n')
                                       || !rear_buf_char_cmp(token, '\r'))) {
                    /* Do nothing */
                } else {
                    eat_whitespace = 0;
                    /* Copy TOKEN TO OUTPUT */
                    if (rear_buf_append_rear_buf(output, token)) {
                        ret = 0;
                        goto clean_up;
                    }
                }
                last_match = 0;
            }
        } else {
            /* Quotes on, so just copy TOKEN TO OUTPUT */
            if (rear_buf_append_rear_buf(output, token)) {
                ret = 0;
                goto clean_up;
            }
        }
    }

    /* Write diversions to stdout in ascending order (excluding diversion -1) */
    for (j = 0; j < NUM_NON_NEG_DIVS; ++j) {
        s = TEXTSIZE(*(div + j));
        if (s && fwrite((*(div + j))->p, 1, s, stdout) != s) {
            ret = 1;
            goto clean_up;
        }
    }

  clean_up:
    if (!end_of_input)
        ret = 1;
    free_front_buf(input);
    free_rear_buf(token);
    free_margs_linked_list(ma);
    return ret;
}
