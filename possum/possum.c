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
 * possum -- Organises photos and videos based on the creation date stored
 * inside the file.
 * To my loving esposinha with her gorgeous possum eyes.
 */

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set paths to the dependencies */
#ifdef _WIN32
#define EFN "C:\\Users\\logan\\bin\\exiftool-12.07\\exiftool.exe"
#define JFN "C:\\Users\\logan\\bin\\jdupes-1.18.2-win64\\jdupes.exe"
#else
#define EFN "/usr/local/bin/exiftool"
#define JFN "/home/logan/bin/jdupes"
#endif

/* Log without errno */
#define LOG(m) fprintf(stderr, "%s: %s: %d: %s\n", __FILE__, func, __LINE__, m)
/* Log with errno */
#define LOGE(m) fprintf(stderr, "%s: %s: %d: %s: %s\n", \
    __FILE__, func, __LINE__, m, strerror(errno))

#define AOF(a, b) ((a) > SIZE_MAX - (b))

#ifdef _WIN32
int run_program(char *fn, char **av)
{
    char *func = "runprgm";
    int ret = 0;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    char *cl;
    size_t i, j, len, s;
    int rv;

    if (fn == NULL) {
        LOG("Filename cannot be NULL");
        return 1;
    }
    if (av == NULL) {
        LOG("Argument vector cannot be NULL");
        return 1;
    }
    if (*av == NULL) {
        LOG("First element of argument vector cannot be NULL");
        return 1;
    }

    i = 0;
    s = 0;
    memset(&si, '\0', sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, '\0', sizeof(PROCESS_INFORMATION));

    while (*(av + i) != NULL) {
        len = strlen(*(av + i));
        if (memchr(*(av + i), '"', len) != NULL) {
            LOG("Argument cannot contain a double quote");
            return 1;
        }
        if (AOF(s, len)) {
            LOG("Addition size_t overflow");
            return 1;
        }
        s += len;
        if (AOF(s, 3)) {
            LOG("Addition size_t overflow");
            return 1;
        }
        s += 3;
        ++i;
    }
    i = 0;
    j = 0;
    if ((cl = malloc(s)) == NULL) return 1;
    while (*(av + i) != NULL) {
        len = strlen(*(av + i));
        *(cl + j++) = '"';
        memcpy(cl + j, *(av + i), len);
        j += len;
        *(cl + j++) = '"';
        *(cl + j++) = ' ';
        ++s;
        ++i;
    }
    *(cl + j) = '\0';

    if(!CreateProcessA(fn, cl, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        LOG("CreateProcess failed");
        return 1;
    }
    if(WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED) {
        LOG("WaitForSingleObject failed");
        ret = 1;
        goto clean_up;
    }
    if (!GetExitCodeProcess(pi.hProcess, &rv)) {
        LOG("GetExitCodeProcess failed");
        ret = 1;
        goto clean_up;
    }
    if (rv) {
        LOG("Child process returned nonzero");
        ret = 1;
        goto clean_up;
    }

clean_up:
    if (!CloseHandle(pi.hThread)) {
        LOG("CloseHandle failed");
        ret = 1;
    }
    if (!CloseHandle(pi.hProcess)) {
        LOG("CloseHandle failed");
        ret = 1;
    }
    return ret;
}
#else
int run_program(char *fn, char **av)
{
    char *func = "runprgm";
    pid_t pid;
    char *en[] = { "LC_ALL=C", NULL };
    int status;

    if (fn == NULL) {
        LOG("Filename cannot be NULL");
        return 1;
    }
    if (av == NULL) {
        LOG("Argument vector cannot be NULL");
        return 1;
    }
    if (*av == NULL) {
        LOG("First element of argument vector cannot be NULL");
        return 1;
    }

    errno = 0;
    if ((pid = fork()) == -1) {
        LOGE("fork failed");
        return 1;
    }
    if (!pid) {
        errno = 0;
        if (execve(fn, av, en) == -1) {
            LOGE("execve failed");
            return 1;
        }
    }
    errno = 0;
    if (wait(&status) == -1) {
        LOGE("wait failed");
        return 1;
    }
    if (WIFEXITED(status) && !WEXITSTATUS(status)) return 0;
    LOG("Child process did not exit successfully");
    return 1;
}
#endif

char *join_str(char *a, char *b)
{
    char *func = "join_str";
    char *p;
    size_t lena, lenb, s;
    if (a == NULL || b == NULL) {
        LOG("Cannot have NULL input string");
        return NULL;
    }
    lena = strlen(a);
    lenb = strlen(b);

    if (AOF(lena, lenb)) {
        LOG("size_t addition overflow");
        return NULL;
    }
    s = lena + lenb;
    if (AOF(s, 1)) {
        LOG("size_t addition overflow");
        return NULL;
    }
    ++s;
    errno = 0;
    if ((p = malloc(s)) == NULL) {
        LOGE("malloc failed");
        return NULL;
    }
    memcpy(p, a, lena);
    memcpy(p + lena, b, lenb);
    *(p + lena + lenb) = '\0';
    return p;
}

int main(int argc, char **argv)
{
    char *func = "main";

    char *eav[] = { "exiftool", "-r", "-FileName<CreateDate", "-d", NULL,
        "-ext", "heic", "-ext", "jpg", "-ext", "jpeg",
        "-ext", "mov", "-ext", "mp4",
        NULL, NULL };
    char *df, *f = "/%Y/%m/%Y_%m_%d_%H_%M_%S%%-c.%%ue";

    char *eav2[] = { "exiftool", "-r", "-FileName<FileModifyDate", "-d", NULL,
        "-ext", "heic", "-ext", "jpg", "-ext", "jpeg",
        "-ext", "mov", "-ext", "mp4",
        NULL, NULL };
    char *df2, *f2 = "/noexifdate/%Y_%m_%d_%H_%M_%S%%-c.%%ue";

    char *jav[] = { "jdupes", "--recurse", "--delete", "--noprompt", NULL,
        NULL };

    char *search_dir, *store_dir;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s search_dir store_dir\n", *argv);
        return 1;
    }

    search_dir = *(argv + 1);
    store_dir = *(argv + 2);

    if ((df = join_str(store_dir, f)) == NULL) {
        LOG("join_str failed");
        return 1;
    }
    *(eav + 4) = df;
    *(eav + 15) = search_dir;

    if (run_program(EFN, eav)) {
        LOG("exiftool failed to move media with exif dates");
        free(df);
        return 1;
    }

    free(df);

    if ((df2 = join_str(store_dir, f2)) == NULL) {
        LOG("join_str failed");
        return 1;
    }
    *(eav2 + 4) = df2;
    *(eav2 + 15) = search_dir;

    if (run_program(EFN, eav2)) {
        LOG("exiftool failed to move media with no exif dates");
        free(df2);
        return 1;
    }

    free(df2);

    *(jav + 4) = store_dir;
    if (run_program(JFN, jav)) {
        LOG("jdupes failed");
        return 1;
    }

    return 0;
}
