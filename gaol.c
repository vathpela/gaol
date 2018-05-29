/*
 * gaol.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "gaol.h"

static void noreturn
usage(int status)
{
        FILE *output = status == 0 ? stdout : stderr;

        fprintf(output, "usage: gaol <cmd> [<arg0> ... <argN>]\n");
        exit(status);
}

char *
find_executable(const char *filename)
{
        char *path = getenv("PATH")
                ? /* defaults to left side arg */
                : (geteuid() == 0 ? _PATH_STDPATH : _PATH_DEFPATH);
        char *q, *p;
        char *filepath, *tmp;

        if (!filename) {
                errno = EINVAL;
                return NULL;
        }

        if (strchr(filename, '/')) {
                char *filepath;

                filepath = canonicalize_file_name(filename);
                if (!filepath)
                        return NULL;

                if (!access(filepath, X_OK))
                        return filepath;

                free(filepath);
                errno = ENOENT;
                return NULL;
        }

        filepath = alloca(strlen(path) + sizeof("/") + strlen(filename));
        if (!filepath)
                err(2, "Could not allocate memory");

        q = p = path;
        for (q = path; q && q[0]; q = p + 1) {
                p = strchrnul(q, ':');
                if (p[0] == '\0')
                        break;
                p[0] = '\0';

                if (!strcmp(q, "") || !strcmp(q, ".") || !strcmp(q, ".."))
                        continue;

                tmp = stpcpy(filepath, q);
                tmp = stpcpy(tmp, "/");
                tmp = stpcpy(tmp, filename);

                if (!access(filepath, X_OK))
                        return strdup(filepath);
        }

        errno = ENOENT;
        return NULL;
}

int
main(int argc, char *argv[])
{
        int cmd = -1;
        char *filename = NULL;
        int rc;

        for (int i = 1; i < argc; i++) {
                char *arg = argv[i];
                if (!strcmp(arg, "--help") || !strcmp(arg, "-h") ||
                    !strcmp(arg, "--usage") || !strcmp(arg, "-?"))
                        usage(0);

                if (cmd < 0) {
                        cmd = i;
                        break;
                }
        }

        if (cmd < 0)
                usage(1);

        filename = find_executable(argv[cmd]);
        if (!filename)
                err(2, "%s", argv[cmd]);

        rc = execvm(filename, &argv[cmd]);
        free(filename);
        if (rc < 0)
                err(6, "Failure is always an option");
        return rc;
}

// vim:fenc=utf-8:tw=75:et
