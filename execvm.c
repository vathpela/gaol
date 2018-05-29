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
#include <inttypes.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/kvm.h>

#include "gaol.h"

struct proc_map {
        uintptr_t start;
        uintptr_t end;

        int mode;

        long long pgoff;
        long long major, minor;
        unsigned long long ino;

        char *name;

        struct list_head list;
};

struct context {
        int fd;
        ssize_t size;
        int vmid;
        long vcpu;
        struct kvm_run *run;
        char *name;
        int argc;
        char **argv;

        list_t maps;
};

#define M_R_OK 1
#define M_W_OK 2
#define M_X_OK 4
#define M_P_OK 8

static void
free_maps(struct context *ctx)
{
        struct list_head *n, *pos;
        list_for_each_safe(pos, n, &ctx->maps) {
                struct proc_map *map = list_entry(pos, struct proc_map, list);

                list_del(&map->list);
                if (map->name)
                        free(map->name);
                free(map);
        }
}

static int
get_maps(struct context *ctx)
{
        int rc;
        uint8_t *buf = NULL;
        size_t bufsize = 0;
        char *idx;
        struct proc_map *map;
        int fd;
        int ret = -1;

        INIT_LIST_HEAD(&ctx->maps);

        fd = open("/proc/self/maps", O_RDONLY);
        if (fd < 0) {
                errno = 0;
                return -1;
        }

        rc = read_file(fd, &buf, &bufsize);
        close(fd);
        if (rc < 0)
                return -1;

        idx = rindex((const char *)buf, '\n');
        if (!idx)
                idx = (char *)buf;
        if (!strcmp(idx, "\n"))
                idx[0] = '\0';

        while ((idx = rindex((const char *)buf, '\n'))) {
                char rwxp[5];
                int spaceoff = 0, nameoff = 0, endoff = 0;
                char pathbuf[PATH_MAX];

                map = calloc(1, sizeof(*map));
                if (!map)
                        goto err;

                rc = sscanf(idx + 1,
                            "%"PRIx64"-%"PRIx64" %[rwxps-] %llx %02llx:%02llx"
                            " %llu%n %n%*s%n",
                            &map->start, &map->end, rwxp,
                            &map->pgoff, &map->major, &map->minor,
                            &map->ino, &spaceoff, &nameoff, &endoff);

                if (!nameoff)
                        nameoff = spaceoff;
                if (!endoff)
                        endoff = nameoff;

                idx[1 + endoff] = '\0';

#if 0
                printf("read: %s\n", idx + 1);
#endif

                if (rwxp[0] == 'r')
                        map->mode |= M_R_OK;
                if (rwxp[1] == 'w')
                        map->mode |= M_W_OK;
                if (rwxp[2] == 'x')
                        map->mode |= M_X_OK;
                if (rwxp[3] == 'p')
                        map->mode |= M_P_OK;

#if 0
                printf("%"PRIx64"-%"PRIx64" %s %08llx %02llx:%02llx %llu %s\n",
                       map->start, map->end, rwxp,
                       map->pgoff, map->major, map->minor, map->ino,
                       idx + 1 + nameoff);
#endif
                char *tmp = stpncpy(pathbuf, idx + 1 + nameoff, endoff-nameoff);
                *tmp = '\0';

                if (pathbuf[0] == '\0' || pathbuf[0] == '[')
                        map->name = strdup(pathbuf);
                else
                        map->name = canonicalize_file_name(pathbuf);
                if (!map->name)
                        goto err;

                list_add(&map->list, &ctx->maps);

                idx[0] = '\0';
        }
#if 0
        struct list_head *this;
        list_for_each(this, &maps) {
                map = list_entry(this, struct proc_map, list);

                printf("new : %"PRIx64"-%"PRIx64" %c%c%c%c %08llx %02llx:%02llx %llu %s\n",
                       map->start, map->end,
                       (map->mode & M_R_OK) ? 'r' : '-',
                       (map->mode & M_W_OK) ? 'w' : '-',
                       (map->mode & M_X_OK) ? 'x' : '-',
                       (map->mode & M_P_OK) ? 'p' : 's',
                       map->pgoff, map->major, map->minor, map->ino,
                       map->name);
        }
#endif

        ret = 0;
err:
        if (ret < 0)
                free_maps(ctx);
        free(buf);
        return ret;
}

static unused struct r_debug *
find_r_debug(void)
{
        GElf_Dyn *dyn;
        struct r_debug *r_debug = NULL;

        for (dyn = _DYNAMIC; dyn->d_tag != DT_NULL; ++dyn)
                if (dyn->d_tag == DT_DEBUG) {
                        r_debug = (struct r_debug *)dyn->d_un.d_ptr;
                        break;
                }
        return r_debug;
}

static void
destroy_vm(struct context *ctx)
{
        if (ctx->fd < 0)
                return;

        if (ctx->vcpu >= 0) {
                ctx->run->immediate_exit = 1;
                ioctl(ctx->vcpu, KVM_RUN, 0);
                close(ctx->vcpu);
        }

        if (!list_empty(&ctx->maps))
            free_maps(ctx);

        if (ctx->vmid >= 0)
                close(ctx->vmid);

        if (ctx->run) {
                munmap(ctx->run, ctx->size);
                ctx->run = NULL;
        }
        ctx->size = -1;
        close(ctx->fd);
        ctx->fd = -1;
}

static int
init_kvm(struct context *ctx)
{
        if (ctx->fd >= 0)
                return ctx->fd;

        ctx->fd = open("/dev/kvm", O_RDWR);
        if (ctx->fd < 0) {
                warn("Could not get kvm fd");
                goto err;
        }

        ctx->size = ioctl(ctx->fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        if (ctx->size < 0) {
                warn("Could not get vcpu mmap size");
                goto err;
        }

        return 0;
err:
        destroy_vm(ctx);
        return -1;
}

static int
pick_and_place_dsos(struct context *ctx)
{
        int rc = -1;
        struct list_head *this;
        struct kvm_userspace_memory_region kumr;

        memset(&kumr, 0, sizeof(kumr));

        list_for_each(this, &ctx->maps) {
                struct proc_map *map;

                map = list_entry(this, struct proc_map, list);
                kumr.flags = (map->mode & M_W_OK) ? 0 : KVM_MEM_READONLY;
                kumr.guest_phys_addr = map->start;
                kumr.memory_size = map->end - map->start + 1;
                kumr.userspace_addr = map->start;

                printf("%"PRIx64"-%"PRIx64" %c%c%c%c %08llx %02llx:%02llx %llu %s\n",
                       map->start, map->end,
                       (map->mode & M_R_OK) ? 'r' : '-',
                       (map->mode & M_W_OK) ? 'w' : '-',
                       (map->mode & M_X_OK) ? 'x' : '-',
                       (map->mode & M_P_OK) ? 'p' : 's',
                       map->pgoff, map->major, map->minor, map->ino,
                       map->name);

                rc = ioctl(ctx->vmid, KVM_SET_USER_MEMORY_REGION, &kumr);
                if (rc < 0)
                        goto err;

                kumr.slot += 1;
        }

        rc = 0;
err:
        return rc;
}

static int
make_vm(struct context *ctx)
{
        unsigned long cpuid = 0;
        int rc = -1;

        ctx->fd = -1;
        ctx->size = -1;
        ctx->vmid = -1;
        ctx->vcpu = -1;
        ctx->run = NULL;

        rc = get_maps(ctx);
        if (rc < 0)
                goto err;

        if (init_kvm(ctx) < 0) {
                goto err;
        }

        ctx->vmid = ioctl(ctx->fd, KVM_CREATE_VM, 0);
        if (ctx->vmid < 0) {
                warn("Could not make vm");
                goto err;
        }

        ctx->vcpu = ioctl(ctx->vmid, KVM_CREATE_VCPU, cpuid);
        if (ctx->vcpu < 0) {
                fprintf(stderr, "Could not make vcpu: %m\n");
                goto err;
        }

        ctx->run = mmap(NULL, ctx->size, PROT_READ|PROT_WRITE,
                       MAP_PRIVATE, ctx->vcpu, 0);
        if (ctx->run == MAP_FAILED) {
                warn("Could not get vcpu kvm_run");
                goto err;
        }

        printf("vcpu: %ld\n", ctx->vcpu);

        rc = pick_and_place_dsos(ctx);
        if (rc < 0)
                goto err;

        return 0;
err:
        destroy_vm(ctx);
        return -1;
}

static void unused
dump_maps(void)
{
        char *buf;
        int fd;

        fd = open("/proc/self/maps", O_RDONLY);
        if (fd < 0) {
                errno = 0;
                return;
        }
        fflush(stdout);
        while (read(fd, &buf, 1) == 1)
                write(STDOUT_FILENO, &buf, 1);
        close(fd);
}

int hidden
execvm(const char *filename, char * const argv[])
{
        struct link_map *map_head = NULL, *map = NULL;
        void *phandle;
        Lmid_t lmid;
        int rc = -1;
        struct context ctx = {
                .fd = -1,
        };

        phandle = dlmopen(LM_ID_NEWLM, filename, RTLD_LOCAL|RTLD_NOW);
        if (!phandle) {
                warnx("dlmopen() failed: %s", dlerror());
                goto err;
        }

        rc = dlinfo(phandle, RTLD_DI_LMID, &lmid);
        if (rc < 0) {
                warnx("Could not get link map ID: %s", dlerror());
                goto err;
        }

        rc = dlinfo(phandle, RTLD_DI_LINKMAP, &map_head);
        if (rc < 0) {
                warnx("Could not get link map: %s", dlerror());
                goto err;
        }

        map = map_head;
        do {
                if (map->l_name) {
                        map->l_name = canonicalize_file_name(map->l_name);
                        if (!map->l_name)
                                goto err;
                }
                printf("%s at %p\n", map->l_name, (void *)(uintptr_t)map->l_addr);
                map = map->l_next;
        } while (map && map != map_head);

        rc = make_vm(&ctx);
        if (rc < 0) {
                warn("make_vm() failed");
                goto err;
        }

        rc = ioctl(ctx.vcpu, KVM_RUN, 0);
        printf("KVM_RUN: %d: %m\n", rc);

        for (unsigned int i = 0; argv[i]; i++)
                printf("%c%s", i == 0 ? '\0' : ' ', argv[i]);
        rc = execv(filename, argv);
err:
        free_maps(&ctx);
        return rc;
}

// vim:fenc=utf-8:tw=75:et
