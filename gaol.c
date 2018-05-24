/*
 * gaol.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gaol.h"

static void noreturn
usage(int status)
{
        FILE *output = status == 0 ? stdout : stderr;

        fprintf(output, "usage: gaol <cmd>\n");
        exit(status);
}

int
main(int argc, char *argv[])
{
        const char *cmd = NULL;

        for (int i = 0; i < argc; i++) {
                char *arg = argv[i];
                if (!strcmp(arg, "--help") || !strcmp(arg, "-h") ||
                    !strcmp(arg, "--usage") || !strcmp(arg, "-?"))
                        usage(0);

                if (!cmd) {
                        cmd = arg;
                        continue;
                }

                usage(1);
        }

        if (!cmd)
                usage(1);
}

// vim:fenc=utf-8:tw=75:et
