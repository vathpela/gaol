/*
 * context.h
 * Copyright 2018-2019 Peter Jones <pjones@redhat.com>
 */

#ifndef CONTEXT_H_
#define CONTEXT_H_

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Memory modes */
#define M_R_OK 1
#define M_W_OK 2
#define M_X_OK 4
#define M_P_OK 8

struct proc_map {
        uintptr_t start;
        uintptr_t end;

        int mode;

        long long pgoff;
        long long major, minor;
        unsigned long long ino;

        char *name;

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
        unsigned long vm_identity_map;
        unsigned long vm_tss;
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

        list_t symbols;
        list_t host_maps;
        list_t guest_maps;

        /* for our global vm context list */
        list_t list;
};

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

static inline uint32_t unused
next_slot(struct context *ctx)
{
        struct list_head *this;
        struct proc_map *map;
        uint32_t slot = UINT32_MAX;

        list_for_each(this, &ctx->guest_maps) {
                map = list_entry(this, struct proc_map, list);

                if (slot == UINT32_MAX || slot <= map->kumr.slot)
                        slot = map->kumr.slot + 1;
        }
        return slot;
}

#endif /* !CONTEXT_H_ */
// vim:fenc=utf-8:tw=75:et
