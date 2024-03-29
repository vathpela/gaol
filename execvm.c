/*
 * execvm.c
 * Copyright 2018-2019 Peter Jones <pjones@redhat.com>
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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "gaol.h"
#include "ioring.h"

#include "dump.h"

#define PS_LIMIT (0x200000)
#define KERNEL_STACK_SIZE (0x4000)
#define MAX_KERNEL_SIZE (PS_LIMIT - 0x5000 - KERNEL_STACK_SIZE)
#define MEM_SIZE (PS_LIMIT * 0x2)

#define SKIP_SEV
static LIST_HEAD(contexts);

static struct context *
new_vm_ctx(void)
{
        struct context *ctx;

        ctx = calloc(1, sizeof (*ctx));
        if (!ctx)
                return NULL;

        ctx->pid = -1;
        ctx->kvm = -1;
        ctx->sev = -1;

        ctx->vm = -1;
        ctx->vm_identity.userspace_addr = (uintptr_t)MAP_FAILED;
        ctx->vm_tss.userspace_addr = (uintptr_t)MAP_FAILED;

        ctx->vcpu = -1;
        ctx->vcpu_mmap_size = -1;

        ctx->run = NULL;

        INIT_LIST_HEAD(&ctx->host_maps);
        INIT_LIST_HEAD(&ctx->guest_maps);
        INIT_LIST_HEAD(&ctx->symbols);
        INIT_LIST_HEAD(&ctx->page_tables);

        list_add(&ctx->list, &contexts);

        return ctx;
}

static unused struct context *
get_ctx(pid_t pid)
{
        struct list_head *n, *pos;

        errno = 0;
        list_for_each_safe(pos, n, &contexts) {
                struct context *ctx = list_entry(pos, struct context, list);
                printf("%s(): found pid %d\n", __func__, ctx->pid);
                if (ctx->pid == pid)
                        return ctx;
        }
        errno = ESRCH;
        return NULL;
}

static void
free_maps(struct context *ctx, struct list_head *head)
{
        struct list_head *n, *pos;
        list_for_each_safe(pos, n, head) {
                struct proc_map *map = list_entry(pos, struct proc_map, list);

                if (map->kumr.memory_size != 0) {
                        map->kumr.memory_size = 0;
                        vm_ioctl(ctx, KVM_SET_USER_MEMORY_REGION, &map->kumr);
                }

                list_del(&map->list);
                if (map->name)
                        free(map->name);
                free(map);
        }

        INIT_LIST_HEAD(head);
}

static bool
should_canonicalize(const char * const pathname)
{
        struct {
                const bool valid;
                const char * const path;
                const bool uselen;
        } nocopy[] = {
                { true, "[", true },
                { true, "anon_inode:kvm-vcpu:", true },
                { true, "\0", false },
                { false, NULL, false }
        };
        bool found = false;

        for (unsigned int i = 0; found == false && nocopy[i].valid; i++) {
                if (nocopy[i].uselen) {
                        if (!strncmp(nocopy[i].path, pathname,
                                     strlen(nocopy[i].path)))
                                found = true;
                } else {
                        if (!strcmp(nocopy[i].path, pathname))
                                found = true;
                }
        }

        return !found;
}

static int
get_host_maps(struct context *ctx)
{
        int rc;
        uint8_t *buf = NULL;
        size_t bufsize = 0;
        char *idx;
        struct proc_map *map;
        int fd;
        int ret = -1;

        fd = open("/proc/self/maps", O_RDONLY);
        if (fd < 0) {
                warn("open(\"/proc/self/maps\", O_RDONLY) failed");
                errno = 0;
                return -1;
        }

        rc = read_file(fd, &buf, &bufsize);
        close(fd);
        if (rc < 0) {
                warn("read_file(%d, %p, %p) failed", fd, &buf, &bufsize);
                return -1;
        }

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
                if (!map) {
                        warn("calloc(1, %zd) failed", sizeof(*map));
                        goto err;
                }

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

                if (should_canonicalize(pathbuf)) {
                        map->name = canonicalize_file_name(pathbuf);
                        if (!map->name) {
                                warn("canonicalizing \"%s\" failed", pathbuf);
                                goto err;
                        }
                } else {
                        map->name = strdup(pathbuf);
                        if (!map->name) {
                                warn("strdup() failed");
                                goto err;
                        }
                }

                list_add(&map->list, &ctx->host_maps);

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
        free(buf);
        return ret;
}

static int
make_guest_maps(struct context *ctx)
{
        int rc;
        struct link_map *map_head = NULL, *map = NULL;
        struct list_head *this, *n;
        list_t new_maps;

        INIT_LIST_HEAD(&new_maps);

        rc = dlinfo(ctx->phandle, RTLD_DI_LINKMAP, &map_head);
        if (rc < 0) {
                fflush(stdout);
                warnx("Could not get link map: %s", dlerror());
                goto err;
        }

#if 0
        dump_maps("guest");

#endif
        printf("Finding guest maps\n");
        list_for_each(this, &ctx->host_maps) {
                struct proc_map *host_map;
                struct proc_map *guest_map;

                host_map = list_entry(this, struct proc_map, list);
#if 0
                printf("finding maps for \"%s\" at %p-%p\n", host_map->name, (void *)host_map->start, (void *)host_map->end);
#endif

                map = map_head;
                do {
                        char *new_name = NULL;
                        uintptr_t start = map->l_addr;
                        long long offset;
                        if (map->l_name) {
                                new_name = canonicalize_file_name(map->l_name);
                                if (!new_name) {
                                        fflush(stdout);
                                        warn("canonicalize_file_name(%s) failed", map->l_name);
                                        goto err;
                                }
                        }

                        if (host_map->start != start) {
                                //printf("no\n");
                                map = map->l_next;
                                free(new_name);
                                continue;
                        }
                        if (strcmp(host_map->name, map->l_name) &&
                            strcmp(host_map->name, new_name)) {
                                //printf("no\n");
                                map = map->l_next;
                                free(new_name);
                                continue;
                        }
                        free(new_name);

                        offset = host_map->pgoff;

#if 0
                        printf(" %s && %p == %p ? ",
                               map->l_name,
                               (void *)host_map->start,
                               (void *)map->l_addr);

                        printf("yes\n");
#endif

/*
 * Unfortunately dlinfo(handle, RTLD_DI_LINKMAP, &map) is more or less
 * completely defective in two major ways:
 *
 * 1) link.h says:
 *      char *l_name; // Absolute file name object was found in.
 *    but the name there has not been canonicalized
 *
 * 2) link.h says:
 *      ElfW(Addr) l_addr; // Difference between the address in the ELF file
 *                         // and the addresses in memory.
 *    Thankfully, this actually appears to be the load address rather than
 *    the /difference/, but it's just the address the first chunk was
 *    mmap()ed at, and it doesn't give you entries for the other maps, nor
 *    a full size, so there's no way to disambiguate whether the next
 *    mappings in /proc/<pid>/maps are from this map or from some other map
 *    (possibly in the link map and possibly not) if they're the same file.
 *    Moreover, you can't use /proc/<pid>/maps' pgoff plus start addresses,
 *    because you can't tell if the file was mmap()ed twice in contiguous
 *    locations, and if for some reason there are gaps (unmapped regions),
 *    you can't tell that's not the end of the mapping.
 *
 *    It might be possible to rectify this by using dl_iterate_phdr() to
 *    find the program headers and actually trying to parse the ELF and
 *    match it up ourselves.  But that really just speaks to how defective
 *    this is to begin with.
 *
 *    Anyway, below is the known-defective pgoff/start matching technique
 *    described above, except sometimes I see this in proc:
 *
 * 0484f000-04850000 r--p 00000000 fd:00 30506907 /path/to/process
 * 04850000-04851000 r-xp 00001000 fd:00 30506907 /path/to/process
 * 04851000-04852000 r--p 00002000 fd:00 30506907 /path/to/process < this pgoff
 * 04852000-04853000 r--p 00002000 fd:00 30506907 /path/to/process < this pgoff
 * 04853000-04854000 rw-p 00003000 fd:00 30506907 /path/to/process
 *
 *    I don't know why I'm seeing that, but in any case, I'm just pgoff <=
 *    instead of ==, which once again introduces an error case with
 *    contiguously loaded maps from the same file.
 */
                        struct list_head *that = this;
                        while (that) {
                                struct proc_map *proc_map;

                                proc_map = list_entry(that, struct proc_map,
                                                      list);
                                if (strcmp(host_map->name, proc_map->name) ||
                                    start != proc_map->start ||
                                    offset < proc_map->pgoff) {
                                        map = NULL;
                                        break;
                                }

                                guest_map = calloc(1, sizeof(*guest_map));
                                if (!guest_map) {
                                        warn("Could not allocate guest map record");
                                        goto err;
                                }

                                memcpy(guest_map, proc_map, sizeof(*guest_map));
                                list_add(&guest_map->list, &new_maps);
                                guest_map->name = strdup(guest_map->name);
                                if (!guest_map->name) {
                                        warn("Could not allocate guest map name");
                                        goto err;
                                }

                                printf("  %"PRIx64"-%"PRIx64" %c%c%c%c %08llx %02llx:%02llx %llu %s\n",
                                       guest_map->start, guest_map->end,
                                       (guest_map->mode & M_R_OK) ? 'r' : '-',
                                       (guest_map->mode & M_W_OK) ? 'w' : '-',
                                       (guest_map->mode & M_X_OK) ? 'x' : '-',
                                       (guest_map->mode & M_P_OK) ? 'p' : 's',
                                       guest_map->pgoff, guest_map->major, guest_map->minor, guest_map->ino,
                                       guest_map->name);

                                start = proc_map->end;
                                offset = proc_map->end - host_map->start;
                                that = that->next;
                        }
                } while (map && map != map_head);
        }

        list_for_each_safe(this, n, &new_maps) {
                struct proc_map *guest_map;

                guest_map = list_entry(this, struct proc_map, list);
                list_del(&guest_map->list);
                list_add(&guest_map->list, &ctx->guest_maps);
        }
        return 0;
err:
        return -1;
}

static void
free_symbols(struct context *ctx)
{
        struct list_head *n, *pos;
        list_for_each_safe(pos, n, &ctx->symbols) {
                struct symbol *sym = list_entry(pos, struct symbol, list);

                list_del(&sym->list);
                if (sym->name)
                        free(sym->name);
                free(sym);
        }

        INIT_LIST_HEAD(&ctx->symbols);
}

static int
add_symbol(struct context *ctx, const char * const name)
{
        struct symbol *sym;
        int ret = -1;
        void *object = NULL;

        sym = calloc(1, sizeof(*sym));
        if (!sym)
                goto err;
        list_add(&sym->list, &ctx->symbols);

        sym->name = strdup(name);
        if (!sym->name)
                goto err;

        object = dlsym(ctx->phandle, name);
        printf("dlsym(%p, \"%s\") -> %p\n", ctx->phandle, name, object);
        //if (!object)
        //        printf("dlsym(%p, \"%s\") failed: %s\n", ctx->phandle, names[i], dlerror());

        sym->addr = (uintptr_t)object;
        //printf("Adding symbol '%s' at %p\n", name, (void *)object);

        ret = 0;
err:
        if (ret < 0 && sym) {
                if (sym->name)
                        free(sym->name);
                free(sym);
        }

        return ret;
}

static void *
get_symbol(struct context *ctx, const char * const name)
{
        struct list_head *pos;
        list_for_each(pos, &ctx->symbols) {
                struct symbol *sym = list_entry(pos, struct symbol, list);

                if (!strcmp(sym->name, name))
                        return sym;
        }

        return NULL;
}

static void *
get_symbol_object(struct context *ctx, const char * const name)
{
        struct symbol *sym = get_symbol(ctx, name);
        void *object = NULL;

        if (sym == NULL && add_symbol(ctx, name) >= 0)
                sym = get_symbol(ctx, name);

        if (sym)
                object = (void *)sym->addr;

        return object;
}

static uintptr_t
get_symbol_guest_object(struct context *ctx, const char * const name)
{
        uintptr_t object;
        uintptr_t guest_object = 0;

        object = (uintptr_t)get_symbol_object(ctx, name);
        if (!object) {
                warnx("Could not find symbol \"%s\"", name);
                return 0;
        }

        struct list_head *pos;
        list_for_each(pos, &ctx->guest_maps) {
                struct proc_map *map = list_entry(pos, struct proc_map, list);

                if (map->start > object || object > map->end)
                        continue;
                if (!map->user_pages)
                        continue;

                /*
                 * right now we're keeping the guest aliased to the host
                 * userspace addresses, so this isn't strictly necessary,
                 * but it's worth having anyway in case we want to change
                 * that later.
                 */
                guest_object = object - map->kumr.userspace_addr + map->start;
                break;
        }

        return guest_object;
}

#if 0
static int
add_symbols(struct context *ctx)
{
        int ret = -1;
        int i;
        const char * const names[] = {
                "enarx_input_ring_ptr__",
                "enarx_output_ring_ptr__",
                ""
        };


        for (i = 0; names[i][0] != 0; i++) {
                ret = add_symbol(ctx, names[i]);
                if (ret < 0)
                        goto err;
        }

        ret = 0;
err:
        if (ret != 0)
                free_symbols(ctx);

        return ret;
}
#endif

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
        if (!ctx)
                return;

        list_del(&ctx->list);

        if (ctx->vcpu >= 0) {
                ctx->run->immediate_exit = 1;
#if 0
                /* this hangs the task in the error cases, and the guest
                 * seems to go away correctly without it, so... */
                printf("%d doing KVM_RUN\n", __LINE__);
                vcpu_ioctl(ctx, KVM_RUN, 0);
#endif
                close(ctx->vcpu);
        }

        free_symbols(ctx);

        free((void *)ctx->stack_map->start);

        free_maps(ctx, &ctx->host_maps);
        free_maps(ctx, &ctx->guest_maps);

        struct list_head *n, *this;
        list_for_each_safe(this, n, &ctx->page_tables) {
                page_table_list_t *pti =
                        list_entry(this, page_table_list_t, list);

                list_del(&pti->list);
                free(pti);
        }

        if (ctx->vm >= 0)
                close(ctx->vm);

        if (ctx->run) {
                munmap(ctx->run, ctx->vcpu_mmap_size);
                ctx->run = NULL;
        }

        if (ctx->sev >= 0) {
                close(ctx->sev);
                ctx->sev = -1;
        }

        if (ctx->kvm >= 0) {
                close(ctx->kvm);
                ctx->kvm = -1;
        }

        ctx->vcpu_mmap_size = -1;

        if (ctx->vm_tss.userspace_addr != (uintptr_t)MAP_FAILED)
                munmap((void *)ctx->vm_tss.userspace_addr, PAGE_SIZE * 3);

        if (ctx->vm_identity.userspace_addr != (uintptr_t)MAP_FAILED)
                munmap((void *)ctx->vm_identity.userspace_addr, PAGE_SIZE);

        if (ctx->phandle)
                dlclose(ctx->phandle);

        free(ctx);
}

static int
pick_and_place_dsos(struct context *ctx)
{
        int rc = -1;
        struct list_head *this;

        printf("Adding guest maps with phys base 0x%016lx\n", ctx->vm_phys_base);
        list_for_each(this, &ctx->guest_maps) {
                struct proc_map *map;

                map = list_entry(this, struct proc_map, list);

                printf("  %"PRIx64"-%"PRIx64" %c%c%c%c %08llx %02llx:%02llx %llu %s",
                       map->start, map->end,
                       (map->mode & M_R_OK) ? 'r' : '-',
                       (map->mode & M_W_OK) ? 'w' : '-',
                       (map->mode & M_X_OK) ? 'x' : '-',
                       (map->mode & M_P_OK) ? 'p' : 's',
                       map->pgoff, map->major, map->minor, map->ino,
                       map->name);

                if (!strcmp(map->name, "[vvar]") ||
                    !strcmp(map->name, "[vdso]") ||
                    !strcmp(map->name, "[vsyscall]")) {
                        printf (" (skipping)\n");
                        continue;
                }

                printf("\n");

                map->user_pages = true;
                map->kumr.slot = ctx->kumr_slot++;
                map->kumr.flags = (map->mode & M_W_OK) ? 0 : KVM_MEM_READONLY;
                map->kumr.guest_phys_addr = ctx->vm_phys_base + map->start;
                map->kumr.memory_size = map->end - map->start;
                map->kumr.userspace_addr = map->start;

                printf("  -> as phys:%p-%p gaol:%p-%p\n",
                       (void *)map->kumr.guest_phys_addr,
                       (void *)map->kumr.guest_phys_addr+map->kumr.memory_size,
                       (void *)map->kumr.userspace_addr,
                       (void *)map->kumr.userspace_addr+map->kumr.memory_size);
                fflush(stdout);

                rc = vm_ioctl(ctx, KVM_SET_USER_MEMORY_REGION, &map->kumr);
                if (rc < 0) {
                        map->kumr.slot = -1;
                        map->kumr.flags = 0;
                        map->kumr.guest_phys_addr = 0;
                        map->kumr.memory_size = 0;
                        map->kumr.userspace_addr = 0;
                        warn("KVM_SET_USER_MEMORY_REGION failed");
                        goto err;
                }
        }

        rc = 0;
err:
        return rc;
}

static inline int
init_sev(struct context *ctx unused)
{
#ifdef SKIP_SEV
        return 0;
#else
        struct sev_user_data_status status;
        int sev_err = 0;
        int rc;

        ctx->sev = open("/dev/sev", O_RDWR);
        if (ctx->sev < 0) {
                warn("Could not get SEV fd");
                goto err;
        }

        memset(&status, 0, sizeof(status));

        rc = sev_ioctl(ctx, SEV_PLATFORM_STATUS, &status, &sev_err);
        if (rc < 0) {
                warn("Could not initialize SEV (fw_error:%d)", sev_err);
                goto err;
        }

err:
        return rc;
#endif
}

static struct context *
set_up_vm(void)
{
        unsigned long cpuid = 0;
        struct context *ctx;
        int rc;

        /* PJFIX: just for debugging for now, and this is the earliest
         * common path to stick it on */
        setlinebuf(stdout);
        setlinebuf(stderr);

        ctx = new_vm_ctx();
        if (ctx == NULL)
                return NULL;

        ctx->kvm = open("/dev/kvm", O_RDWR);
        if (ctx->kvm < 0) {
                warn("Could not get kvm fd");
                goto err;
        }

        ctx->vm = kvm_ioctl(ctx, KVM_CREATE_VM, 0);
        if (ctx->vm < 0) {
                warn("Could not make vm");
                goto err;
        }

        /* PJFIX: check capabilities */
        ctx->vm_identity.slot = ctx->kumr_slot++;
        ctx->vm_identity.flags = 0;
        ctx->vm_identity.guest_phys_addr = 0xfffbc000;
        ctx->vm_identity.memory_size = PAGE_SIZE;
        ctx->vm_identity.userspace_addr =
                (uintptr_t)mmap((void *)0xfffbc000ul, PAGE_SIZE, PROT_READ|PROT_WRITE,
                                MAP_FIXED|MAP_SHARED|MAP_ANONYMOUS, -1, 0);
        printf("vm_identity.guest_phys_addr: 0x%016llx\n", ctx->vm_identity.guest_phys_addr);
        printf("vm_identity.userspace_addr: 0x%016llx\n", ctx->vm_identity.userspace_addr);
        if (!ctx->vm_identity.userspace_addr) {
                warn("Couldn't get identity map");
                goto err;
        }

        rc = vm_ioctl(ctx, KVM_SET_IDENTITY_MAP_ADDR, &ctx->vm_identity.userspace_addr);
        if (rc < 0) {
                warn("Could not set vm identity map address");
                goto err;
        }

        /* PJFIX: check capabilities */
        ctx->vm_tss.slot = ctx->kumr_slot++;
        ctx->vm_tss.flags = 0;
        ctx->vm_tss.guest_phys_addr = 0xfffbd000;
        ctx->vm_tss.memory_size = PAGE_SIZE * 3;
        ctx->vm_tss.userspace_addr =
                (uintptr_t)mmap((void *)0xfffbd000ul, PAGE_SIZE * 3, PROT_READ|PROT_WRITE,
                                MAP_FIXED|MAP_SHARED|MAP_ANONYMOUS, -1, 0);
        printf("vm_tss.guest_phys_addr: 0x%016llx\n", ctx->vm_tss.guest_phys_addr);
        printf("vm_tss.userspace_addr: 0x%016llx\n", ctx->vm_tss.userspace_addr);
        if (!ctx->vm_tss.userspace_addr) {
                warn("Couldn't get identity map");
                goto err;
        }

        rc = vm_ioctl(ctx, KVM_SET_TSS_ADDR, ctx->vm_tss.userspace_addr);
        if (rc < 0) {
                warn("Could not set vm tss address");
                goto err;
        }

        ctx->vm_phys_base = 0xfffc0000;

        ctx->vcpu = vm_ioctl(ctx, KVM_CREATE_VCPU, cpuid);
        if (ctx->vcpu < 0) {
                warn("Could not make vcpu");
                goto err;
        }

        ctx->vcpu_mmap_size = kvm_ioctl(ctx, KVM_GET_VCPU_MMAP_SIZE, 0);
        if (ctx->vcpu_mmap_size < 0) {
                warn("Could not get vcpu mmap size");
                goto err;
        }

        ctx->run = mmap(NULL, ctx->vcpu_mmap_size, PROT_READ|PROT_WRITE,
                        MAP_PRIVATE, ctx->vcpu, 0);
        if (ctx->run == MAP_FAILED) {
                warn("Could not map vcpu runtime controls");
                goto err;
        }

        ctx->vcpu_tsc_khz = vcpu_ioctl(ctx, KVM_GET_TSC_KHZ, 0);
        if (ctx->vcpu_tsc_khz < 0)
                warn("KVM_GET_TSC_KHZ failed?");

        printf("vcpu: %ld at %dkHZ\n", ctx->vcpu, ctx->vcpu_tsc_khz);

        rc = vm_ioctl(ctx, KVM_GET_CLOCK, &ctx->vm_clock);
        if (rc < 0)
                warn("Couldn't get vm clock");

        printf("Current tsc: %llu\n", ctx->vm_clock.clock);

        return ctx;
err:
        destroy_vm(ctx);
        return NULL;
}

/*
 * We know none of the addresses *outside* of the link map we've
 * mirrored are in use in the vm, and ASLR has already happened for
 * our host binary, so find our stack, allocate the same amount, and
 * assign it to the guest at the same address.
 */
static int
init_stack(struct context *ctx)
{
        struct list_head *this;
        struct proc_map *guest_map, *host_map = NULL;
        uintptr_t addr;
        void *stack;
        size_t size;

        addr = (uintptr_t)&addr;

        guest_map = calloc(1, sizeof(*guest_map));
        if (!guest_map) {
                warn("Could not allocate guest stack map entry");
                return -1;
        }

        list_for_each(this, &ctx->host_maps) {
                host_map = list_entry(this, struct proc_map, list);
                if (host_map->start <= addr && host_map->end >= addr)
                        break;
                host_map = NULL;
        }

        if (host_map == NULL) {
                warnx("Could not find host stack map?!?!");
                free(guest_map);
                return -1;
        }

        size = ALIGN_UP(host_map->end - host_map->start, PAGE_SIZE);
        stack = calloc(1, size);
        if (!stack) {
                warn("Could not allocate guest stack");
                return -1;
        }

        memmove(guest_map, host_map, sizeof(*guest_map));
        INIT_LIST_HEAD(&guest_map->list);
        guest_map->start = (uintptr_t)stack;
        guest_map->end = guest_map->start + size;

        guest_map->name = strdup(guest_map->name);
        if (!guest_map->name) {
                free(guest_map);
                free(stack);
                return -1;
        }

        guest_map->user_pages = true;
        guest_map->kumr.slot = ctx->kumr_slot++;
        guest_map->kumr.flags = 0;
        guest_map->kumr.guest_phys_addr = ctx->vm_phys_base + guest_map->start;
        guest_map->kumr.memory_size = size;
        guest_map->kumr.userspace_addr = (uintptr_t)stack;

        printf("  -> [stack] as phys:%p-%p gaol:%p-%p\n",
               (void *)guest_map->kumr.guest_phys_addr,
               (void *)guest_map->kumr.guest_phys_addr
                     + guest_map->kumr.memory_size,
               (void *)guest_map->kumr.userspace_addr,
               (void *)guest_map->kumr.userspace_addr
                     + guest_map->kumr.memory_size);
        fflush(stdout);

        list_add(&guest_map->list, &ctx->guest_maps);
        ctx->stack_map = guest_map;
        return 0;
}

typedef union {
        struct {
                /*
                 * phys size = 1024 * PAGE_SIZE
                 * codemap + 0x000 * PAGE_SIZE = instructions
                 * codemap + 0x001 * PAGE_SIZE = pml4
                 * codemap + 0x002 * PAGE_SIZE = pdp
                 * codemap + 0x003 * PAGE_SIZE = pd
                 * codemap + 0x004 * PAGE_SIZE = stack ends
                 * codemap + 0x006 * PAGE_SIZE = stack begins
                 * codemap + 0x007 * PAGE_SIZE = arena begins
                 * codemap + 0x3ff * PAGE_SIZE = last in arena
                 */
                uint8_t code[PAGE_SIZE];
                pml4e_t pml4[512];
                pdpe_t pdp[512];
                pde_t pd[512];
                uint8_t stack[PAGE_SIZE * 3];
                uint8_t memory[PAGE_SIZE * (1024 - 7)];
        };
        uint8_t arena[PAGE_SIZE * 1024];
} arena_t;

vmid_t hidden
forkvm(const char *filename, char * const argv[] unused)
{
        int rc = -1;
        struct context *ctx;
        Lmid_t lmid;

        ctx = set_up_vm();
        if (ctx == NULL) {
                warnx("Could not set up VM");
                return -1;
        }

#if 1 && 0
          /*
           * .code16
           * mov al, 0x61
           * mov dx, 0x217
           * out dx, al
           * mov al, 10
           * out dx, al
           * hlt
           */
        uint8_t code[] = "\xB0\x61\xBA\x17\x02\xEE\xB0\n\xEE\xF4";

        arena_t *arena;
        arena = mmap(NULL, sizeof(*arena),
                     PROT_READ|PROT_WRITE|PROT_EXEC,
                     MAP_SHARED|MAP_ANONYMOUS, -1, 0);
        if (arena == MAP_FAILED)
                err(1, "mmap failed");

        memset(arena, 0, sizeof(*arena));
        memcpy(&arena->code, code, sizeof(code));

        struct proc_map *map = calloc(1, sizeof(*map));
        if (!map)
                err(2, "calloc failed");
        map->mode = M_R_OK|M_W_OK|M_X_OK|M_P_OK;
        map->name = strdup("Code");
        map->start = (uintptr_t)arena;
        map->end = (uintptr_t)arena + sizeof(*arena);
        map->pgoff = 0;

        map->user_pages = true;
        map->kumr.slot = 0;
        map->kumr.flags = 0;
        map->kumr.guest_phys_addr = map->start;
        map->kumr.memory_size = sizeof(*arena);
        map->kumr.userspace_addr = map->start;

        list_add(&map->list, &ctx->guest_maps);

        rc = vm_ioctl(ctx, KVM_SET_USER_MEMORY_REGION, &map->kumr);
        if (rc < 0)
                err(3, "KSUMR failed");

#else
        ctx->phandle = dlmopen(LM_ID_NEWLM, filename, RTLD_LOCAL|RTLD_NOW);
        if (!ctx->phandle) {
                warnx("dlmopen() failed: %s", dlerror());
                goto err;
        }
        printf("dlmopen(LM_ID_NEWLM, \"%s\", RTLD_LOCAL|RTLD_NOW) -> %p\n",
               filename, ctx->phandle);

        rc = dlinfo(ctx->phandle, RTLD_DI_LMID, &lmid);
        if (rc < 0) {
                warnx("Could not get link map ID: %s", dlerror());
                goto err;
        }
        printf("link map id: %lu\n", lmid);

#if 0
        rc = add_symbols(ctx);
        if (rc < 0) {
                warnx("add_symbols failed");
                goto err;
        }

        extern struct iorings *iorings__;
        struct iorings **iorings = get_symbol_object(ctx, "iorings__");

        printf("old child &iorings__: %p iorings__: %p\n", iorings, *iorings);
        *iorings = iorings__;
        msync(*iorings, sizeof(*iorings), MS_ASYNC|MS_INVALIDATE);
        printf("new child &iorings__: %p iorings__: %p\n", iorings, *iorings);

        //dump_maps("child");

        printf("child before: input: %p output: %p\n", (*iorings)->input, (*iorings)->output);
        (*iorings)->input = iorings__->output;
        (*iorings)->output = iorings__->input;
        msync(iorings, sizeof((*iorings)), MS_ASYNC|MS_INVALIDATE);
        printf("child after: input: %p output: %p\n", (*iorings)->input, (*iorings)->output);
#endif

        rc = get_host_maps(ctx);
        if (rc < 0) {
                warnx("get_host_maps() failed");
                goto err;
        }

        rc = make_guest_maps(ctx);
        if (rc < 0) {
                warnx("make_guest_maps() failed");
                goto err;
        }

        rc = pick_and_place_dsos(ctx);
        if (rc < 0) {
                warnx("pick_and_place_dsos() failed");
                goto err;
        }

        rc = init_stack(ctx);
        if (rc < 0) {
                warnx("init_stack() failed");
                goto err;
        }

        rc = init_paging(ctx);
        if (rc < 0) {
                warnx("init_paging() failed");
                goto err;
        }
#endif

        rc = init_segments(ctx);
        if (rc < 0) {
                warnx("init_segments() failed");
                goto err;
        }

#if 0
        rc = init_sev(ctx);
        if (rc < 0) {
                warnx("Could not initialize SEV");
                goto err;
        }
#endif

        struct kvm_regs regs;

        rc = vcpu_ioctl(ctx, KVM_GET_REGS, &regs);
        if (rc < 0) {
                warn("Could not get vcpu regs");
                goto err;
        }

#if 1 && 0
        regs.rip = (uintptr_t)&arena->code;
        regs.rsp = (uintptr_t)&arena->stack;
        regs.rdi = ((uintptr_t)&arena->memory) - 16;
        regs.rsi = sizeof(arena->memory);
        regs.rflags = 0x2;
#else
        uint64_t offset = 1ul << 32;
        regs.rip = get_symbol_guest_object(ctx, "main") + offset;
        if (regs.rip == 0) {
                warn("Could not find main");
                goto err;
        }

        regs.rsp = ctx->stack_map->kumr.guest_phys_addr + offset;
        regs.rax = 0;
        regs.rbx = 0;
        regs.rflags = 0x2;
#endif
        printf("setting rip=0x%016llx rsp=0x%016llx\n", regs.rip, regs.rsp);

        rc = vcpu_ioctl(ctx, KVM_SET_REGS, &regs);
        if (rc < 0) {
                warn("Could not set vcpu regs");
                goto err;
        }

#if 1 && 0
        struct kvm_sregs sregs;
        rc = vcpu_ioctl(ctx, KVM_GET_SREGS, &sregs);
        if (rc < 0)
                err(4, "KVM_GET_SREGS failed");

        ctx->pml4 = &arena->pml4[0];

        cr3_t *cr3 = (cr3_t *)&sregs.cr3;
        printf("cr3: 0x%016lx -> ", cr3->cr3);
        cr3->cr3 = 0;
        cr3->pml4_base = ptr64_to_pfn40(ctx->pml4);
        printf("cr3: 0x%016lx\n", cr3->cr3);

        cr4_t *cr4 = (cr4_t *)&sregs.cr4;
        printf("cr4: 0x%016lx -> ", cr4->cr4);
        cr4->cr4 = 0;
        cr4->pae = 1;
        cr4->osfxsr = 1;
        cr4->osxmmexcpt = 1;
        printf("cr4: 0x%016lx\n", cr4->cr4);

        cr0_t *cr0 = (cr0_t *)&sregs.cr0;
        printf("cr0: 0x%016lx -> ", cr0->cr0);
        cr0->cr0 = 0;
        cr0->pe = 1;
        cr0->mp = 1;
        cr0->et = 1;
        cr0->ne = 1;
        cr0->wp = 1;
        cr0->am = 1;
        cr0->pg = 1;
        printf("cr0: 0x%016lx\n", cr0->cr0);

        efer_t *efer = (efer_t *)&sregs.efer;
        printf("efer: 0x%016lx -> ", efer->efer);
        efer->efer = 0;
        efer->lme = 1;
        efer->lma = 1;
        efer->sce = 1;
        printf("efer: 0x%016lx\n", efer->efer);

        pml4e_t *pml4 = ctx->pml4;
        pml4[0].us = 1;
        pml4[0].rw = 1;
        pml4[0].p = 1;
        pml4[0].pdp_base = ptr64_to_pfn51(&arena->pdp);

        pdpe_t *pdp = &arena->pdp[0];
        pdp[0].us = 1;
        pdp[0].rw = 1;
        pdp[0].p = 1;
        pdp[0].pd_base = ptr64_to_pfn51(&arena->pd);

#else
        finalize_paging(ctx);
#endif

        bool go = true;
        while (go) {
                struct timeval tv0 = { 0, 0 }, tv1 = { 0, 0 };
                struct timespec req = { 0, 0 }, rem = { 0, 1 };
                int64_t timediff;

                gettimeofday(&tv0, NULL);
                printf("Doing KVM_RUN\n");
                rc = vcpu_ioctl(ctx, KVM_RUN, 0);
                gettimeofday(&tv1, NULL);
                if (rc < 0) {
                        warn("KVM_RUN failed");
                        break;
                }
                printf("KVM_RUN took %ld.%06ld seconds\n",
                       tv1.tv_sec - tv0.tv_sec,
                       (tv1.tv_usec - tv0.tv_usec));
                switch (ctx->run->exit_reason) {
                case KVM_EXIT_HLT:
                        printf("exited with KVM_EXIT_HLT\n");
                        go = false;
                        break;
                case KVM_EXIT_IO:
                        printf("exited with KVM_EXIT_IO\n");
                        go = false;
                        break;
                case KVM_EXIT_FAIL_ENTRY:
                        printf("exited with KVM_EXIT_FAIL_ENTRY\n");
                        printf("failure reason: 0x%0llx\n", ctx->run->fail_entry.hardware_entry_failure_reason);
                        go = false;
                        break;
                case KVM_EXIT_INTERNAL_ERROR:
                        printf("exited with KVM_EXIT_INTERNAL_ERROR\n");
                        go = false;
                        break;
                case KVM_EXIT_SHUTDOWN:
                        printf("exited with KVM_EXIT_SHUTDOWN\n");
                        go = false;
                        break;
                default:
                        printf("exited with %d\n", ctx->run->exit_reason);
                        go = false;
                        break;
                }

                timediff = (tv1.tv_sec - tv0.tv_sec) * 1000000 +
                           tv1.tv_usec - tv0.tv_usec;
                if (timediff > 1000000)
                        continue;
                if (timediff < 0)
                        timediff = 0;

                rem.tv_nsec = (1000000 - timediff) * 1000;
                rem.tv_sec = 0;
                rc = 0;
                do {
                        memmove(&req, &rem, sizeof(req));
                        rem.tv_sec = rem.tv_nsec = 0;
                        rc = nanosleep(&req, &rem);
                        if (rc < 0 && errno == EINTR)
                                rc = 0;
                } while (rc >= 0 && rem.tv_sec >= 0 && rem.tv_nsec > 0);
        }

err:
        destroy_vm(ctx);

        return rc;
}

// vim:fenc=utf-8:tw=75:et
