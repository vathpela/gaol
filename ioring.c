/*
 * ioring.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include <err.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

#include "gaol.h"

typedef struct ioring ioring;

struct iorings *iorings__ = NULL;

int
ioring_map_rings(void)
{
        if (iorings__)
                return 0;

        iorings__ = mmap(NULL, sizeof(*iorings__), PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (iorings__ == MAP_FAILED) {
                warn("mmap(NULL, %zd, PROT_READ|PROT_WRITE, MAP_SHARED_VALIDATE|MAP_ANONYMOUS, -1, 0) = MAP_FAILED",
                     sizeof (*iorings__));
                return -1;
        }

        printf("parent &iorings__: %p iorings__: %p\n", &iorings__, iorings__);
        //dump_maps("parent");

        iorings__->input = &iorings__->one;
        iorings__->output = &iorings__->two;

        msync(iorings__, sizeof(*iorings__), MS_ASYNC|MS_INVALIDATE);

        printf("parent: input: %p output: %p\n", iorings__->input, iorings__->output);

        return 0;
}

static void
stall(uint64_t n)
{
        for (uint64_t x = 0; x < n; x++)
                __asm__("pause");
}

int
ioring_write(const char * const buf, size_t size)
{
        uint16_t start;
        ioring *output;

        if (!iorings__ || !iorings__->output) {
                errno = EINVAL;
                return -1;
        }

        output = iorings__->output;
        /*
         * yes yes, this spinlock isn't real. just need something that
         * mostly works and is static, for now
         */
        while (output->lock)
                stall(1000);
        output->lock = 1;
        msync(output, sizeof(*output), MS_ASYNC|MS_INVALIDATE);

        start = output->size;
        if (sizeof(output->buf) - start > size)
                return -ENOSPC;
        memcpy(output->buf + output->size, buf, size);
        output->size += size;
        output->lock = 0;

        return size;
}

int
ioring_read(char * const buf, size_t size)
{
        uint16_t bytes;
        ioring *input;

        if (!iorings__ || !iorings__->input) {
                errno = EINVAL;
                return -1;
        }

        input = iorings__->input;
        /*
         * yes yes, this spinlock isn't real. just need something that
         * mostly works and is static, for now
         */
        while (input->lock)
                stall(1000);
        input->lock = 1;

        bytes = min(input->size, size);
        memcpy(buf, input->buf, bytes);
        memmove(input->buf, input->buf + bytes, sizeof(input->buf) - bytes);
        input->size -= bytes;
        input->lock = 0;

        return bytes;
}

// vim:fenc=utf-8:tw=75:et
