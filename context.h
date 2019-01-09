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

        list_t symbols;
        list_t host_maps;
        list_t guest_maps;

        /* for our global vm context list */
        list_t list;
};

#endif /* !CONTEXT_H_ */
// vim:fenc=utf-8:tw=75:et
