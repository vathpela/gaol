/*
 * util.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */
#ifndef UTIL_H_
#define UTIL_H_ 1

#include <alloca.h>
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tgmath.h>
#include <unistd.h>

static inline int unused
read_file(int fd, uint8_t **buf, size_t *bufsize)
{
        uint8_t *p;
        size_t size = 4096;
        size_t filesize = 0;
        ssize_t s = 0;

        uint8_t *newbuf;
        if (!(newbuf = calloc(size, sizeof (uint8_t))))
                return -1;

        *buf = newbuf;

        do {
                p = *buf + filesize;
                /* size - filesize shouldn't exceed SSIZE_MAX because we're
                 * only allocating 4096 bytes at a time and we're checking that
                 * before doing so. */
                s = read(fd, p, size - filesize);
                if (s < 0 && errno == EAGAIN) {
                        /*
                         * if we got EAGAIN, there's a good chance we've hit
                         * the kernel rate limiter.  Doing more reads is just
                         * going to make it worse, so instead, give it a rest.
                         */
                        sched_yield();
                        continue;
                } else if (s < 0) {
                        int saved_errno = errno;
                        free(*buf);
                        *buf = NULL;
                        *bufsize = 0;
                        errno = saved_errno;
                        return -1;
                }
                filesize += s;
                /* only exit for empty reads */
                if (s == 0)
                        break;
                if (filesize >= size) {
                        /* See if we're going to overrun and return an error
                         * instead. */
                        if (size > (size_t)-1 - 4096) {
                                free(*buf);
                                *buf = NULL;
                                *bufsize = 0;
                                errno = ENOMEM;
                                return -1;
                        }
                        newbuf = realloc(*buf, size + 4096);
                        if (newbuf == NULL) {
                                int saved_errno = errno;
                                free(*buf);
                                *buf = NULL;
                                *bufsize = 0;
                                errno = saved_errno;
                                return -1;
                        }
                        *buf = newbuf;
                        memset(*buf + size, '\0', 4096);
                        size += 4096;
                }
        } while (1);

        newbuf = realloc(*buf, filesize+1);
        if (!newbuf) {
                free(*buf);
                *buf = NULL;
                return -1;
        }
        newbuf[filesize] = '\0';
        *buf = newbuf;
        *bufsize = filesize+1;
        return 0;
}

#endif /* UTIL_H_ */
// vim:fenc=utf-8:tw=75:et
