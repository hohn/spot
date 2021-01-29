/*
 * Copyright (c) 2021 Logan Ryan McLintock
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
 * sloth -- version control system.
 * C file to glue the SQL together.
 */

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STR_BLOCK 512

#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

int mv_file(char *from_file, char *to_file)
{
#ifdef _WIN32
    /* NOT atomic */
    if (!MoveFileEx
        (from_file, to_file,
         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 1;
#else
    /* Atomic */
    if (rename(from_file, to_file))
        return 1;
#endif
    return 0;
}


char *concat(char *str1, ...)
{
/*
 * Concatenate multiple strings. Last argument must be NULL.
 * Returns NULL on error or a pointer to the concatenated string on success.
 * Must free the concatenated string after use.
 */
    int err = 0;
    va_list arg_p;
    char *str;
    size_t len;
    char *p;                    /* Buffer pointer */
    char *t;                    /* Temporary buffer pointer */
    size_t u = 0;               /* Used memory */
    size_t s;                   /* Total memory size */
    size_t n;                   /* Temporary new size */

    if ((p = malloc(STR_BLOCK)) == NULL)
        return NULL;

    s = STR_BLOCK;

    va_start(arg_p, str1);
    str = str1;
    while (str != NULL) {
        len = strlen(str);

        /* Need to save space for terminating \0 char */
        if (len >= s - u) {
            /* Grow buffer */
            if (AOF(len, 1)) {
                err = 1;
                goto clean_up;
            }
            n = len + 1 - (s - u);
            if (AOF(s, n)) {
                err = 1;
                goto clean_up;
            }
            n += s;
            if (MOF(n, 2)) {
                err = 1;
                goto clean_up;
            }
            n *= 2;
            if ((t = realloc(p, n)) == NULL) {
                err = 1;
                goto clean_up;
            }
            p = t;
            s = n;
        }
        memcpy(p + u, str, len);
        u += len;

        str = va_arg(arg_p, char *);
    }

    *(p + u) = '\0';

  clean_up:
    va_end(arg_p);

    if (err) {
        free(p);
        return NULL;
    }

    return p;
}

char *path_join(char *dirpart, char *basepart)
{
    size_t dp_len, bp_len, len;
    char dir_sep;
    char *p;
    if (dirpart == NULL || basepart == NULL)
        return NULL;
    dp_len = strlen(dirpart);

    /* dirpart is an empty string */
    if (!dp_len) {
        if ((p = strdup(basepart)) == NULL)
            return NULL;
        return p;
    }

    bp_len = strlen(basepart);

#ifdef _WIN32
    dir_sep = '\\';
#else
    dir_sep = '/';
#endif

    if (AOF(dp_len, bp_len))
        return NULL;
    len = dp_len + bp_len;
    if (AOF(len, 2))
        return NULL;
    ++len;                      /* For directory separator */
    if ((p = malloc(len + 1)) == NULL)
        return NULL;

    memcpy(p, dirpart, dp_len);
    *(p + dp_len) = dir_sep;
    memcpy(p + dp_len + 1, basepart, bp_len);
    *(p + len) = '\0';
    return p;
}

char *dirpart(char *path)
{
/*
 * Gets the directory part of a file path.
 * If there is no directory separator then the empty string is returned,
 * not the "." dot directory.
 * Returns NULL on error or a pointer to the concatenated string on success.
 * Must free the concatenated string after use.
 */
    char *q = path;
    char *last = q;
    char dir_sep;
    size_t len;
    char *p;

#ifdef _WIN32
    dir_sep = '\\';
#else
    dir_sep = '/';
#endif

    while (*q != '\0') {
        if (*q == dir_sep)
            last = q;
        ++q;
    }

    len = last - path;

    /* Overflow is impossible */
    if ((p = malloc(len + 1)) == NULL)
        return NULL;

    memcpy(p, path, len);
    *(p + len) = '\0';

    return p;
}

int sys_cmd(char *cmd)
{
    /* Executes a system command */
    int r;
    if ((r = system(cmd)) == -1)
        return 1;
#ifdef _WIN32
    if (!r)
        return 0;
#else
    if (WIFEXITED(r) && !WEXITSTATUS(r))
        return 0;
#endif
    return 1;
}

int run_sql(char *ex_dir, char *script_name)
{
    int ret = 0;
    char *sql_path;
    char *macro_path;
    char *cmd;

    if ((sql_path = path_join(ex_dir, script_name)) == NULL)
        return 1;
    if ((macro_path = path_join(ex_dir, "macros.m4")) == NULL)
        return 1;
    if ((cmd =
         concat("m4 ", macro_path, " ", sql_path, " | sqlite3 sloth.db",
                NULL)) == NULL) {
        ret = 1;
        goto clean_up;
    }

    if (sys_cmd(cmd)) {
        ret = 1;
        goto clean_up;
    }

  clean_up:
    free(sql_path);
    free(macro_path);
    free(cmd);
    return ret;
}

int main(int argc, char **argv)
{
    int ret = 0;
    char *ex_dir;
    char *opt = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s help\n", *argv);
        return 1;
    }

    if ((ex_dir = dirpart(*argv)) == NULL)
        return 1;

    if ((opt = strdup(*(argv + 1))) == NULL) {
        ret = 1;
        goto clean_up;
    }

    if (!strcmp(opt, "init")) {
        if (run_sql(ex_dir, "ddl.sql")) {
            ret = 1;
            goto clean_up;
        }

    } else if (!strcmp(opt, "commit")) {
        if (run_sql(ex_dir, "commit.sql")) {
            ret = 1;
            goto clean_up;
        }
        /* Atomic on POSIX */
        if (mv_file("sloth_copy.db", "sloth.db")) {
            ret = 1;
            goto clean_up;
        }
    } else {
        fprintf(stderr, "%s: Invalid option: %s\n", *argv, opt);
        ret = 1;
        goto clean_up;
    }

  clean_up:
    free(ex_dir);
    free(opt);

    return ret;
}
