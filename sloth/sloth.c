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

#include <sys/types.h>
#include <sys/stat.h>


#ifdef _WIN32
/* For rand_s */
#define _CRT_RAND_S
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Set to the the directory where sloth is installed.
 * The SQL scripts and m4 script must also be in this
 * same directory.
 */
#define EX_DIR "/home/logan/bin"

#define STR_BLOCK 512

#define AOF(a, b) ((a) > SIZE_MAX - (b))
#define MOF(a, b) ((a) && (b) > SIZE_MAX / (a))

char *random_alnum_str(size_t len)
{
    char *p;
    unsigned char *t, u;
    size_t i, j;
#ifdef _WIN32
    unsigned int r;
    size_t ts = sizeof(unsigned int);
#else
    FILE *fp;
    size_t ts = 64;
#endif

    if (AOF(len, 1))
        return NULL;
    if ((p = malloc(len + 1)) == NULL)
        return NULL;

#ifndef _WIN32
    if ((t = malloc(ts)) == NULL) {
        free(p);
        return NULL;
    }
    if ((fp = fopen("/dev/urandom", "r")) == NULL) {
        free(p);
        free(t);
        return NULL;
    }
#endif

    j = 0;
    while (j != len) {
#ifdef _WIN32
        if (rand_s(&r)) {
            free(p);
            return NULL;
        }
        t = (unsigned char *) &r;
#else
        if (fread(t, 1, ts, fp) != ts) {
            free(p);
            free(t);
            fclose(fp);
            return NULL;
        }
#endif

        for (i = 0; i < ts; ++i) {
            u = *(t + i);
            if (isalnum(u)) {
                *(p + j++) = u;
                if (j == len)
                    break;
            }
        }
    }

    *(p + j) = '\0';

#ifndef _WIN32
    free(t);
    if (fclose(fp)) {
        free(p);
        return NULL;
    }
#endif

    return p;
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

int cp_file(char *from_file, char *to_file)
{
    FILE *fp_from;
    FILE *fp_to;
    size_t fs;
    char *p;

    if (filesize(from_file, &fs))
        return 1;

    if ((p = malloc(fs)) == NULL)
        return 1;

    if ((fp_from = fopen(from_file, "rb")) == NULL) {
        free(p);
        return 1;
    }

    if (fread(p, 1, fs, fp_from) != fs) {
        free(p);
        fclose(fp_from);
        return 1;
    }

    if (fclose(fp_from)) {
        free(p);
        return 1;
    }

    if ((fp_to = fopen(to_file, "wb")) == NULL) {
        free(p);
        return 1;
    }

    if (fwrite(p, 1, fs, fp_to) != fs) {
        free(p);
        return 1;
    }

    free(p);

    if (fclose(fp_to))
        return 1;

    return 0;
}

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

char *path_join(char *directory_name, char *file_base_name)
{
    size_t d_len, f_len, len;
    char dir_sep;
    char *p;

    if (directory_name == NULL || file_base_name == NULL)
        return NULL;

    d_len = strlen(directory_name);

    /* directory_name is an empty string */
    if (!d_len) {
        if ((p = strdup(file_base_name)) == NULL)
            return NULL;
        return p;
    }

    f_len = strlen(file_base_name);

#ifdef _WIN32
    dir_sep = '\\';
#else
    dir_sep = '/';
#endif

    if (AOF(d_len, f_len))
        return NULL;
    len = d_len + f_len;
    if (AOF(len, 2))
        return NULL;
    ++len;                      /* For directory separator */
    if ((p = malloc(len + 1)) == NULL)
        return NULL;

    memcpy(p, directory_name, d_len);
    *(p + d_len) = dir_sep;
    memcpy(p + d_len + 1, file_base_name, f_len);
    *(p + len) = '\0';
    return p;
}

char *directory_name(char *file_path)
{
    /*
     * Returns the directory name of a file path (must be a file).
     * Returns NULL on error, or the directory name string on success
     * (which must be freed after use).
     */
    char *q = file_path;
    char *last = NULL;
    size_t len;
    char *p;

#ifdef _WIN32
    char dir_sep = '\\';
#else
    char dir_sep = '/';
#endif

    if (file_path == NULL)
        return NULL;

    while (*q != '\0') {
        if (*q == dir_sep)
            last = q;
        ++q;
    }

    /* No directory separator found */
    if (last == NULL)
        return strdup(".");

    len = last - file_path;

    /* Overflow is impossible due to the trailing '\0' */
    if ((p = malloc(len + 1)) == NULL)
        return NULL;

    memcpy(p, file_path, len);
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

int run_sql(char *db_name, char *ex_dir, char *script_name)
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
         concat("m4 ", macro_path, " ", sql_path, " | sqlite3 ", db_name,
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

int sloth_commit(char *ex_dir, char *msg, char *time, int backup)
{
    char *cmd;

    if (backup) {
        if (cp_file("sloth.db", "sloth_copy.db"))
            return 1;
    }

    if ((cmd =
         concat("sqlite3 sloth_copy.db \"delete from sloth_tmp_text; ",
                "insert into sloth_tmp_text (x) values (\'", msg,
                "\');\"", NULL)) == NULL)
        return 1;

    if (sys_cmd(cmd)) {
        free(cmd);
        return 1;
    }
    free(cmd);

    if (sys_cmd("sqlite3 sloth_copy.db \"delete from sloth_tmp_int;\""))
        return 1;

    if (time == NULL) {
        if (sys_cmd
            ("sqlite3 sloth_copy.db \"insert into sloth_tmp_int (i) "
             "select strftime(\'%s\',\'now\');\""))
            return 1;
    } else {
        if ((cmd = concat("sqlite3 sloth_copy.db ",
                          "\"insert into sloth_tmp_int (i) values (\'",
                          time, "\');\"", NULL)) == NULL)
            return 1;

        if (sys_cmd(cmd)) {
            free(cmd);
            return 1;
        }
        free(cmd);
    }

    if (run_sql("sloth_copy.db", ex_dir, "commit.sql"))
        return 1;

    if (backup) {
        /* Atomic on POSIX */
        if (mv_file("sloth_copy.db", "sloth.db"))
            return 1;
    }

    return 0;
}

void swap_ch(char *str, char old, char new)
{
    /*
     * Replaces all old chars in a string str with new chars.
     * str must be non-static.
     */
    char *q = str;
    while (*q != '\0') {
        if (*q == old)
            *q = new;
        ++q;
    }
}

int import_git(char *ex_dir)
{
    FILE *fp;
    size_t fs;
    char *p = NULL;

    char *hash;
    char *time;
    char *msg;

    char *cmd;
    if (sys_cmd("git log --reverse --pretty=format:%H^%at^%s > .log"))
        return 1;

    if (filesize(".log", &fs))
        return 1;

    if (AOF(fs, 1))
        return 1;

    if ((p = malloc(fs + 1)) == NULL)
        return 1;
    *(p + fs) = '\0';

    if ((fp = fopen(".log", "rb")) == NULL) {
        free(p);
        return 1;
    }

    if (fread(p, 1, fs, fp) != fs) {
        free(p);
        fclose(fp);
        return 1;
    }

    if (fclose(fp)) {
        free(p);
        return 1;
    }

    /* Backup */
    if (cp_file("sloth.db", "sloth_copy.db")) {
        free(p);
        return 1;
    }

    /* Parse */
    hash = strtok(p, "^\n");
    do {
        time = strtok(NULL, "^\n");
        msg = strtok(NULL, "^\n");

        /* Clean msg */
        swap_ch(msg, '\'', ' ');

        printf("hash: %s\ntime: %s\nmsg: %s\n", hash, time, msg);

        if ((cmd = concat("git checkout ", hash, NULL)) == NULL) {
            free(p);
            return 1;
        }

        if (sys_cmd(cmd)) {
            free(p);
            return 1;
        }

        if (sys_cmd("git ls-files > .track")) {
            free(p);
            return 1;
        }

        if (sloth_commit(ex_dir, msg, time, 0)) {
            free(p);
            return 1;
        }
    } while ((hash = strtok(NULL, "^\n")) != NULL);

    free(p);
    free(cmd);

    /* Atomic on POSIX */
    if (mv_file("sloth_copy.db", "sloth.db"))
        return 1;

    return 0;
}

void print_usage(char *prgm_name)
{
    fprintf(stderr, "Usage: %s init|log|import|export|subdir|combine\n"
            "%s commit msg [time]\n", prgm_name, prgm_name);
}

int main(int argc, char **argv)
{
    int ret = 0;
    char *prgm_name;
    char *ex_dir;
    char *opt = NULL;
    char *subdir = NULL;
    char *cmd = NULL;

    if (argc < 2) {
        print_usage(*argv);
        return 1;
    }

    if ((prgm_name = strdup(*argv)) == NULL)
        return 1;

    if ((ex_dir = strdup(EX_DIR)) == NULL) {
        ret = 1;
        goto clean_up;
    }

    if ((opt = strdup(*(argv + 1))) == NULL) {
        ret = 1;
        goto clean_up;
    }

    if (!strcmp(opt, "init")) {
        if (run_sql("sloth.db", ex_dir, "ddl.sql")) {
            ret = 1;
            goto clean_up;
        }
    } else if (!strcmp(opt, "log")) {
        if (run_sql("sloth.db", ex_dir, "log.sql")) {
            ret = 1;
            goto clean_up;
        }
    } else if (!strcmp(opt, "commit")) {
        if (argc == 3) {
            if (sloth_commit(ex_dir, *(argv + 2), NULL, 1)) {
                ret = 1;
                goto clean_up;
            }
        } else if (argc == 4) {
            if (sloth_commit(ex_dir, *(argv + 2), *(argv + 3), 1)) {
                ret = 1;
                goto clean_up;
            }
        } else {
            print_usage(prgm_name);
            ret = 1;
            goto clean_up;
        }
    } else if (!strcmp(opt, "export")) {
        if (run_sql("sloth.db", ex_dir, "export.sql")) {
            ret = 1;
            goto clean_up;
        }
    } else if (!strcmp(opt, "import")) {
        if (import_git(ex_dir)) {
            ret = 1;
            goto clean_up;
        }
    } else if (!strcmp(opt, "subdir")) {
        if (argc != 3) {
            print_usage(prgm_name);
            ret = 1;
            goto clean_up;
        }

        /* Backup */
        if (cp_file("sloth.db", "sloth_copy.db")) {
            ret = 1;
            goto clean_up;
        }

        if ((subdir = strdup(*(argv + 2))) == NULL) {
            ret = 1;
            goto clean_up;
        }
        swap_ch(subdir, '\'', ' ');

        if ((cmd =
             concat("sqlite3 sloth_copy.db \"delete from sloth_tmp_text; ",
                    "insert into sloth_tmp_text (x) values (\'", subdir,
                    "\');\"", NULL)) == NULL)
            return 1;

        if (sys_cmd(cmd)) {
            ret = 1;
            goto clean_up;
        }

        if (run_sql("sloth_copy.db", ex_dir, "subdir.sql")) {
            ret = 1;
            goto clean_up;
        }

        /* Atomic on POSIX */
        if (mv_file("sloth_copy.db", "sloth.db")) {
            ret = 1;
            goto clean_up;
        }
    } else {
        print_usage(prgm_name);
        ret = 1;
        goto clean_up;
    }

  clean_up:
    free(prgm_name);
    free(ex_dir);
    free(opt);
    free(subdir);
    free(cmd);

    return ret;
}
