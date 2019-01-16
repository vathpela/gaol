/*
 * context.h
 * Copyright 2018-2019 Peter Jones <pjones@redhat.com>
 */

#ifndef CONTEXT_H_
#define CONTEXT_H_

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Memory modes */
#define M_R_OK 1 /* read */
#define M_W_OK 2 /* write */
#define M_X_OK 4 /* execute */
#define M_P_OK 8 /* private */

struct proc_map {
        uintptr_t start;
        uintptr_t end;

        int mode;

        long long pgoff;
        long long major, minor;
        unsigned long long ino;

        char *name;

        bool user_pages;
        struct kvm_userspace_memory_region kumr;
        struct list_head list;
};

struct symbol {
        char *name;
        uintptr_t addr;

        struct list_head list;
};

struct context {
        pid_t pid;

        int kvm;

        int sev;

        int vm;
        int kumr_slot;
        struct kvm_userspace_memory_region vm_identity;
        struct kvm_userspace_memory_region vm_tss;
        unsigned long vm_phys_base;
        struct kvm_clock_data vm_clock;

        long vcpu;
        ssize_t vcpu_mmap_size;
        int vcpu_tsc_khz;

        struct kvm_run *run;

        char *name;
        int argc;
        char **argv;
        void *phandle;

        struct proc_map *stack_map;
        struct proc_map *page_table_map;

        list_t symbols;
        list_t host_maps;
        list_t guest_maps;

        int ntables;
        list_t page_tables;
        pml4e_t *pml4;

        /* for our global vm context list */
        list_t list;
};

static int unused
cmp_maps(const void *mp0, const void *mp1)
{
        struct proc_map *map0 = (struct proc_map *)mp0;
        struct proc_map *map1 = (struct proc_map *)mp1;

        return map0->start - map1->start;
}

static inline void unused
sort_maps(struct proc_map *maps, int nmemb)
{
        qsort(maps, nmemb, sizeof(*maps), cmp_maps);
}

static inline ssize_t unused
get_map_array(list_t *map_list, list_t *mapsp)
{
        struct proc_map *maparray = NULL;
        struct list_head *pos;
        ssize_t i = 0;
        list_t maps;

        INIT_LIST_HEAD(&maps);

        list_for_each(pos, map_list) {
                struct proc_map *omap, *map, *new;
                int j = i + 1;

                new = reallocarray(maparray, j, sizeof(struct proc_map));
                if (!new)
                        goto err;
                maparray = new;

                omap = list_entry(pos, struct proc_map, list);
                map = &new[i];
                memmove(map, omap, sizeof(*map));
                i = j;
        }

        printf("maparray:\n");
        for (int j = 0; j < i; j++) {
                struct proc_map *map;
                map = &maparray[j];

                printf("map[%d] at %p: %s\n", j, map, map->name);

                INIT_LIST_HEAD(&map->list);
                list_add(&map->list, &maps);
        }

        *mapsp = maps;
        return i;
err:
        if (maparray)
                free(maparray);
        maparray = NULL;
        return -1;
}

static inline uintptr_t unused
lowest_guest_addr(struct context *ctx)
{
        struct list_head *pos;
        uintptr_t addr = UINTPTR_MAX;

        list_for_each(pos, &ctx->guest_maps) {
                struct proc_map *map = list_entry(pos, struct proc_map, list);

                if (map->start < addr)
                        addr = map->start;
        }
        errno = 0;
        if (addr == UINTPTR_MAX)
                errno = ENOENT;
        return addr;
}

static inline uintptr_t unused
highest_guest_addr(struct context *ctx)
{
        struct list_head *pos;
        uintptr_t addr = 0;

        list_for_each(pos, &ctx->guest_maps) {
                struct proc_map *map = list_entry(pos, struct proc_map, list);

                if (map->end > addr)
                        addr = map->end;
        }
        errno = 0;
        if (addr == 0)
                errno = ENOENT;
        return addr;
}

extern int private init_paging(struct context *ctx);
extern int private finalize_paging(struct context *ctx);
extern int private init_segments(struct context *ctx);

#endif /* !CONTEXT_H_ */
// vim:fenc=utf-8:tw=75:et
