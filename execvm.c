/*
 * execvm.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include <dlfcn.h>
#include <elf.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gaol.h"

void noreturn hidden
execvm(const char *filename, char * const argv[])
{
        struct link_map *map_head = NULL, *map = NULL;
        void *phandle;
        Lmid_t lmid;
        int rc;

        phandle = dlmopen(LM_ID_NEWLM, filename, RTLD_LOCAL|RTLD_NOW);
        if (!phandle)
                errx(3, "dlmopen() failed: %s", dlerror());

        rc = dlinfo(phandle, RTLD_DI_LMID, &lmid);
        if (rc < 0)
                errx(4, "Could not get link map ID: %s", dlerror());

        rc = dlinfo(phandle, RTLD_DI_LINKMAP, &map_head);
        if (rc < 0)
                errx(5, "Could not get link map: %s", dlerror());

        map = map_head;
        do {
                printf("%s at %p\n", map->l_name, (void *)(uintptr_t)map->l_addr);
                map = map->l_next;
        } while (map && map != map_head);

        execv(filename, argv);
        for (unsigned int i = 0; argv[i]; i++)
                printf("%c%s", i == 0 ? '\0' : ' ', argv[i]);
        printf("\n");
        err(4, "Failure is always an option.");
}

// vim:fenc=utf-8:tw=75:et
