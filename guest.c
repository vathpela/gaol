/*
 * guest.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/fcntl.h>

#include "compiler.h"
#include "ioring.h"

#include "dump.h"

static void unused
stall(uint64_t n)
{
        for (uint64_t x = 0; x < n; x++)
                __asm__("pause");
}

static int test_ctors = 0;

static void constructor
ctor(void)
{
        test_ctors = 1;
}

int main(void)
{
        char buf[4096];
        uint16_t size = sizeof(buf);
        int status = 0;
        int rc;

#if 0
        extern void *enarx_input_ring_ptr__;

        printf("guest!\n");
        dump_maps();
        printf("enarx_input_ring_ptr__: %p\n", enarx_input_ring_ptr__);
#else
        if (test_ctors) {
                strcpy(buf, "Ctors worked.\n");
                size = strlen(buf);
                ioring_write(buf, size);

                rc = -ENOSPC;
                while ((rc = ioring_write(buf, size)) == -ENOSPC)
                        stall(1000);
        }
        do {
                stall(10000);
                size = sizeof(buf);
                rc = ioring_read(buf, size);
                if (rc < 0) {
                        status = 1;
                        break;
                }

                if (rc == 0)
                        continue;

                size = rc;
                rc = -ENOSPC;
                while ((rc = ioring_write(buf, size)) == -ENOSPC)
                        stall(1000);
        } while (status == 0);
#endif
        strcpy(buf, "Goodbye, cruel world.\n");
        size = strlen(buf);
        ioring_write(buf, size);
        return status;
}

// vim:fenc=utf-8:tw=75:et
