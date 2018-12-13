/*
 * dump.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef DUMP_H_
#define DUMP_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "compiler.h"

static void unused
dump_maps(const char * const name)
{
        char *buf;
        int fd;

        fd = open("/proc/self/maps", O_RDONLY);
        if (fd < 0) {
                errno = 0;
                return;
        }
        printf("%s maps:\n", name);
        fflush(stdout);
        while (read(fd, &buf, 1) == 1)
                write(STDOUT_FILENO, &buf, 1);
        fsync(STDOUT_FILENO);
        fdatasync(STDOUT_FILENO);
        close(fd);
}



#endif /* !DUMP_H_ */
// vim:fenc=utf-8:tw=75:et
