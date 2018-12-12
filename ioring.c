/*
 * ioring.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include <string.h>
#include <errno.h>

#include "compiler.h"
#include "ioring.h"

struct ioring {
        uint16_t lock;
        uint16_t size;
        uint8_t buf[4096] aligned(4096);
} packed aligned(4096);

typedef struct ioring ioring;

ioring enarx_input_ring__;
ioring enarx_output_ring__;
ioring *enarx_input_ring_ptr__ = &enarx_input_ring__;
ioring *enarx_output_ring_ptr__ = &enarx_output_ring__;

static ioring *input, *output;

void
ioring_swap_rings(void)
{
        ioring *tmp = output;

        output = input;
        input = tmp;
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

        if (!input || !output) {
                input = enarx_input_ring_ptr__;
                output = enarx_output_ring_ptr__;
        };

        /*
         * yes yes, this spinlock isn't real. just need something that
         * mostly works and is static, for now
         */
        while (output->lock)
                stall(1000);
        output->lock = 1;

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

        if (!input || !output) {
                input = enarx_input_ring_ptr__;
                output = enarx_output_ring_ptr__;
        };

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
