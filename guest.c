/*
 * guest.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ioring.h"

static void
stall(uint64_t n)
{
        for (uint64_t x = 0; x < n; x++)
                __asm__("pause");
}

int main(void)
{
        char buf[4096];
        uint16_t size = sizeof(buf);
        int rc;
        int status = 0;

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

        strcpy(buf, "Goodbye, cruel world.\n");
        size = strlen(buf);
        ioring_write(buf, size);
        return status;
}

// vim:fenc=utf-8:tw=75:et
