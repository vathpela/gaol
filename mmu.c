/*
 * mmu.c - vm mmu setup
 * Copyright 2019 Peter Jones <pjones@redhat.com>
 *
 */

#include "gaol.h"

#include <err.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/mman.h>

#define qprintf(...)

#if 0
static size_t ntables;
static paging_table_t *tables;
static cr3_t cr3;
static pml4e_t *pml4t;

static void
fixup_pt_ptrs(pt_type pt_type, paging_table_t *table, intptr_t delta)
{
        intptr_t old, new;
        pml4e_t *pml4 = &table->entry[0].pml4;
        pdpe_t *pdp = &table->entry[0].pdp;
        pde_t *pd = &table->entry[0].pd;

        for (uint16_t i = 0; i < 512; i++) {
                switch (pt_type) {
                case CR3:
                        old = pfn40_to_ptr64(cr3.pml4_base);
                        new  = old + delta;
                        fixup_pt_ptrs(PML4E, (paging_table_t *)new, delta);
                        pml4t = (pml4e_t *)new;
                        cr3.pml4_base = pfn40(new);
                        return;
                case PML4E:
                        if (pml4[i].p != 1)
                                continue;
                        old = pfn40_to_ptr64(pml4[i].pdp_base);
                        new = old + delta;
                        fixup_pt_ptrs(PDPE, (paging_table_t *)new, delta);
                        pml4[i].pdp_base = ptr64to40(new);
                        break;
                case PDPE:
                        if (pdp[i].p != 1)
                                continue;
                        old = pfn40_to_ptr64(pdp[i].pd_base);
                        new = old + delta;
                        fixup_pt_ptrs(PDE, (paging_table_t *)new, delta);
                        pdp[i].pd_base = ptr64to40(new);
                        break;
                case PDE:
                        if (pd[i].p != 1)
                                continue;
                        old = pfn40_to_ptr64(pd[i].pt_base);
                        new = old + delta;
                        pd[i].pt_base = ptr64to40(new);
                        break;
                case PTE:
                        /* no fixup needed, since "page_base" here is our
                         * real allocation, not a paging structure. */
                        break;
                }
        }
}

static int
new_paging_table(void **addr)
{
        paging_table_t *newtables;
        intptr_t delta;
        size_t sz = (ntables+1) * PAGE_SIZE;

        newtables = reallocarray(tables, ntables+1, PAGE_SIZE);
        if (!newtables) {
                warn("Couldn't allocate paging table");
                return -1;
        }
        memset(&newtables[ntables], 0, PAGE_SIZE);

        /*
         * Life would really have been better if reallocarray() guaranteed
         * that alignment of the resulting object would be no smaller than
         * its /size/ parameter...
         */
        if ((intptr_t)newtables & (PAGE_SIZE - 1)) {
                int rc;
                void *tmp;

                rc = posix_memalign(&tmp, PAGE_SIZE, sz);
                if (rc < 0) {
                        warn("Couldn't allocate paging table");
                        return -1;
                }

                memmove(tmp, newtables, sz);
                free(newtables);
                newtables = tmp;
        }

        *addr = &newtables[ntables];
        ntables += 1;

        if (pml4t) {
                delta = (intptr_t)newtables - (intptr_t)tables;
                fixup_pt_ptrs(CR3, newtables, delta);
        }
        tables = newtables;
        pml4t = (pml4e_t *)newtables;

        return 0;
}

static int
finalize_paging(void)
{
        paging_table_t *newtables;
        int rc;
        intptr_t oldptr = (intptr_t)tables, newptr;
        size_t sz = ntables * sizeof(*newtables);

        if (((intptr_t)tables & (PAGE_SIZE - 1)) == 0)
                return 0;

        printf("finalizing paging\n");

        rc = posix_memalign((void **)&newtables, PAGE_SIZE, sz);
        if (rc < 0) {
                warn("Could not finalize page tables");
                return -1;
        }
        newptr = ptr64to40(newtables);

        memmove(newtables, tables, sz);

        fixup_pt_ptrs(CR3, newtables, newptr - oldptr);

        free(tables);
        tables = newtables;
        pml4t = (pml4e_t *)newtables;
        cr3.pml4_base = newptr;

        return 0;
}

static int
map_pages(struct proc_map *map)
{
        void *addr;
        int rc;
        uint16_t n;

        pdpe_t *pdpt;
        pde_t *pdt;
        pte_t *pt;

        if (pml4t == NULL) {
                rc = new_paging_table(&addr);
                if (rc < 0)
                        goto err;

                cr3.pml4_base = ptr64to40(addr);
                pml4t = addr;
        }

        n = get_pml4e(map->start);
        if (pml4t[n].p != 1) {
                rc = new_paging_table(&addr);
                if (rc < 0)
                        goto err;

                pml4t[n].nx = 0;
                pml4t[n].pdp_base = ptr64to40(addr);
                pml4t[n].avl = 0;
                pml4t[n].a = 0;
                pml4t[n].pcd = 0;
                pml4t[n].pwt = 1;
                pml4t[n].us = 1;
                pml4t[n].rw = 1;
                pml4t[n].p = 1;
        }
        n = get_pml4e(map->start);
        pdpt = (pdpe_t *)pfn40_to_ptr64(pml4t[n].pdp_base);

        n = get_pdpe(map->start);
        if (pdpt[n].p != 1) {
                rc = new_paging_table(&addr);
                if (rc < 0)
                        goto err;
                n = get_pml4e(map->start);
                pdpt = (pdpe_t *)pfn40_to_ptr64(pml4t[n].pdp_base);
                n = get_pdpe(map->start);

                pdpt[n].nx = 0;
                pdpt[n].pd_base = ptr64to40(addr);
                pdpt[n].avl = 0;
                pdpt[n].zero = 0;
                pdpt[n].a = 0;
                pdpt[n].pcd = 0;
                pdpt[n].pwt = 1;
                pdpt[n].us = 1;
                pdpt[n].rw = 1;
                pdpt[n].p = 1;
        }
        pdt = (pde_t *)pfn40_to_ptr64(pdpt[n].pd_base);

        n = get_pde(map->start);
        if (pdt[n].p != 1) {
                rc = new_paging_table(&addr);
                if (rc < 0)
                        goto err;
                n = get_pml4e(map->start);
                pdpt = (pdpe_t *)pfn40_to_ptr64(pml4t[n].pdp_base);
                n = get_pdpe(map->start);
                pdt = (pde_t *)pfn40_to_ptr64(pdpt[n].pd_base);

                pdt[n].nx = 0;
                pdt[n].pt_base = ptr64to40(addr);
                pdt[n].avl = 0;
                pdt[n].zero = 0;
                pdt[n].a = 0;
                pdt[n].pcd = 0;
                pdt[n].pwt = 1;
                pdt[n].us = 1;
                pdt[n].rw = 1;
                pdt[n].p = 1;
        }
        pt = (pte_t *)pfn40_to_ptr64(pdt[n].pt_base);

        for (n = get_pte(map->start), addr = (void *)map->start;
             n <= get_pte(map->end);
             n++, addr += PAGE_SIZE) {
                if (pt[n].p == 1)
                        continue;

                pt[n].nx = !(map->mode & M_X_OK);
                pt[n].page_base = ptr64to40(addr);
                pt[n].avl = 0;
                pt[n].g = 1;
                pt[n].pat = 0; /* FIXME */
                pt[n].d = 0;
                pt[n].a = 0;
                pt[n].pcd = 0;
                pt[n].pwt = map->mode & M_W_OK;
                pt[n].us = 1;
                pt[n].rw = map->mode & M_W_OK;
                pt[n].p = 1;
        }
        return 0;
err:
        return -1;
}
#endif

typedef struct __attribute__ ((__aligned__ (4096))) {
        pml4e_t pml4e[512];
        pdpe_t pdpe[512][512];
        pde_t pde[512][512][512];
        pte_t pte[512][512][512][512];
} pgtbl_t;
#if 0
static int npml4e = 0;
static int npdpe = 0;
static int npde = 0;
static int npte = 0;
#endif

static void
dump_pte(pte_t *pte, uint16_t i, uint16_t j, uint16_t k, uint16_t l)
{
        intptr_t base;
        void *phys, *virt;
        intptr_t ptr;

        if (!pte->p)
                return;

        base = pte->page_base;
        printf("  pte[0x%03hx].ptr = 0x%lx",
                       (uint16_t)l, base);

        virt = (void *)pte_to_addr(i, j, k, l, base);
        phys = (void *)pfn51_to_ptr64(base);
        printf(" map: phys:%p virt:%p\n", phys, virt);
        ptr = i & 0x1ff;
        ptr <<= 9;
        ptr |= j & 0x1ff;
        ptr <<= 30;
        ptr |= (base & 0x3fffffff) << PAGE_SHIFT;
        printf("        ptr:0x%016lx\n", ptr);
        ptr = signex(ptr, 48);
        printf("signex(ptr):0x%016lx\n", ptr);
        printf("  i:0x%03hx j:0x%03hx k:0x%03hx l:%03hx ptr:0x%016lx\n", i, j, k, l, ptr);
}

static void
dump_pde(pde_t *pde, uint16_t i, uint16_t j, uint16_t k)
{
        intptr_t base;

        if (!pde->p)
                return;

        base = pde->pt_base;
        if (pde->ps) {
                void *phys, *virt;
                intptr_t ptr;
                pde = (pde_t *)pfn30_to_ptr64(base);
                printf("  pde[0x%03hx].ptr = 0x%lx = %p",
                       (uint16_t)k, base, pde);

                virt = (void *)pde_to_addr(i, j, k, base);
                phys = (void *)pfn30_to_ptr64(base);
                printf(" map: phys:%p virt:%p\n", phys, virt);
                ptr = i & 0x1ff;
                ptr <<= 9;
                ptr |= j & 0x1ff;
                ptr <<= 30;
                ptr |= (base & 0x3fffffff) << PAGE_SHIFT;
                printf("        ptr:0x%016lx\n", ptr);
                ptr = signex(ptr, 48);
                printf("signex(ptr):0x%016lx\n", ptr);
                printf("  i:0x%03hx j:0x%03hx k:0x%03hx ptr:0x%016lx\n", i, j, k, ptr);
                return;
        }

        pte_t *pd = pfn40_to_ptr64(base);
        for (uint16_t l = 0; l < 512; l++)
                dump_pte(&pd[k], i, j, k, l);
}

static void
dump_pdpe(pdpe_t *pdpe, uint16_t i, uint16_t j)
{
        intptr_t base;

        if (!pdpe->p)
                return;

        base = pdpe->pd_base;
        if (pdpe->ps) {
                void *phys, *virt;
                intptr_t ptr;
                pdpe = (pdpe_t *)pfn30_to_ptr64(base);
                printf("  pdpe[0x%03hx].ptr = 0x%lx = %p %c%c%c",
                       (uint16_t)j, base, pdpe,
                       pdpe->nx ? '-' : 'x',
                       pdpe->us ? 'u' : 's',
                       pdpe->rw ? 'w' : 'r');

                virt = (void *)pdpe_to_addr(i, j, base);
                phys = (void *)pfn40_to_ptr64(base);
                printf(" map: phys:%p virt:%p\n", phys, virt);
                ptr = i & 0x1ff;
                ptr <<= 9;
                ptr |= j & 0x1ff;
                ptr <<= 30;
                ptr |= (base & 0x3fffffff) << PAGE_SHIFT;
                printf("        ptr:0x%016lx\n", ptr);
                ptr = signex(ptr, 48);
                printf("signex(ptr):0x%016lx\n", ptr);
                printf("  i:0x%03hx j:0x%03hx ptr:0x%016lx\n", i, j, ptr);
                return;
        } else {
                void *phys, *virt;
                pdpe = (pdpe_t *)pfn30_to_ptr64(base);
                printf("  pdpe[0x%03hx].ptr = 0x%lx = %p %c%c%c",
                       (uint16_t)j, base, pdpe,
                       pdpe->nx ? '-' : 'x',
                       pdpe->us ? 'u' : 's',
                       pdpe->rw ? 'w' : 'r');

                virt = (void *)pdpe_to_addr(i, j, base);
                phys = (void *)pfn40_to_ptr64(base);
                printf(" map: phys:%p virt:%p %c%c%c\n", phys, virt,
                       pdpe->nx ? '-' : 'x',
                       pdpe->us ? 'u' : 's',
                       pdpe->rw ? 'w' : 'r');
        }

        pde_t *pd = pfn40_to_ptr64(base);
        for (uint16_t k = 0; k < 512; k++)
                dump_pde(&pd[k], i, j, k);
}

static void
dump_pml4e(pml4e_t *pml4e, uint16_t i)
{
        pdpe_t *pdpe;
        intptr_t base;

        if (!pml4e->p)
                return;

        base = pml4e->pdp_base;
        pdpe = (pdpe_t *)pfn40_to_ptr64(base);
        printf(" pml4[0x%03hx].ptr = 0x%lx = %p %c%c%c\n", i, base, pdpe,
               pml4e->nx ? '-' : 'x',
               pml4e->us ? 'u' : 's',
               pml4e->rw ? 'w' : 'r');

        for (uint16_t j = 0; j < 512; j ++)
                dump_pdpe(pdpe + j, i, j);
}

static void
dump_pgtbls(cr3_t cr3)
{
        pml4e_t *pml4;
        intptr_t base;

        printf("dumping page tables:\n");
        base = cr3.pml4_base;
        pml4 = (pml4e_t *)pfn40_to_ptr64(base);
        printf("cr3.pml4_base = 0x%lx = %p\n", base, pml4);

        for (uint16_t i = 0; i < 512; i++)
                dump_pml4e(pml4 + i, i);
}

static page_table_list_t *
new_page_table_list(struct context *ctx)
{
        page_table_list_t *ret;
        int rc;

        rc = posix_memalign((void **)&ret, PAGE_SIZE, sizeof(*ret));
        if (rc < 0) {
                warn("Could not allocate page tables");
                return NULL;
        }
        memset(ret, 0, sizeof(*ret));

        INIT_LIST_HEAD(&ret->list);
        list_add(&ret->list, &ctx->page_tables);
        ctx->ntables += 1;
        if (!ctx->pml4)
                ctx->pml4 = ret->table.pml4;

        qprintf("new paging table at %p\n", ret);
        return ret;
}

int private
init_paging(struct context *ctx)
{
        page_table_list_t *pgtbl = NULL;
        struct proc_map *page_table_map = NULL;

        page_table_map = calloc(1, sizeof(*page_table_map));
        if (!page_table_map) {
                warn("Could not allocate page table map record");
                goto err;
        }

        page_table_map->name = strdup("[pagetables]");
        if (!page_table_map) {
                warn("Could not allocate map name");
                goto err;
        }

        pgtbl = new_page_table_list(ctx);
        if (!pgtbl)
                return -1;

        INIT_LIST_HEAD(&page_table_map->list);
        list_add(&page_table_map->list, &ctx->guest_maps);
        ctx->page_table_map = page_table_map;

        return 0;
err:
        if (pgtbl)
                free(pgtbl);
        if (page_table_map)
                free(page_table_map);
        return -1;
}

int private nonnull(1)
map_pt_entry(pte_t *pt, uintptr_t va,
             bool nx, bool user, bool rw)
{
        pte_t *pte;

        printf("       Mapping 0x1000 bytes at 0x%016lx in PT[0x%03lx]\n",
               va & ~PAGE_MASK, get_pte(va));
        pte = &pt[get_pte(va)];
        if (!pte->p) {
                pte->page_base = ptr64_to_pfn40(va);
                printf("        create PT[0x%03lx] (page_base:0x%lx nx:%d us:%d rw:%d)\n", get_pte(va), (uintptr_t)pte->page_base, nx, user, rw);
                pte->nx = nx;
                pte->pwt = pte->rw = rw;
                pte->us = user;
                pte->p = 1;
        } else {
                printf("        update PT[0x%03lx] (page_base:0x%lx nx:%d->%d us:%d->%d rw:%d->%d)\n", get_pte(va), (uintptr_t)pte->page_base, pte->nx, nx, pte->us, user, pte->rw, rw);
                if (nx != pte->nx || user != pte->us || rw != pte->rw) {
                        printf("            Not changing permissions on PTE\n");
                        return -1;
                }
        }

        return 0;
}

int private nonnull(1)
map_pt_entries(pte_t *pt, uintptr_t va, size_t size,
               bool nx, bool user, bool rw)
{
        bool first = true;
        do {
                int rc;
                uint64_t base = va - (va % PT_SIZE);
                uint64_t rem = base + PT_SIZE - va;
#if 0
                printf("va:   0x%016lx\n", va);
                printf("base: 0x%016lx\n", base);
                printf("rem:  0x%016lx\n", rem);
                printf("        size:0x%016lx PT_SIZE:0x%016lx rem:0x%016lx\n", size, PT_SIZE, rem);
#endif

                fflush(stdout);
                if (!first && get_pte(va) == 0) {
                        printf("gaol: pte table overflow\n");
                        fflush(stdout);
                        fflush(stderr);
                }
                first = false;
                rc = map_pt_entry(pt, va, nx, user, rw);
                if (rc < 0)
                        return rc;

                va += min(size, rem);
                size -= min(size, rem);
        } while (size);

        return 0;
}

int private nonnull(1, 2)
map_pd_entry(struct context *ctx, pde_t *pd,
             uintptr_t va, size_t size,
             bool nx, bool user, bool rw)
{
        pde_t *pde;
        pte_t *pt;

        printf("     Mapping 0x%lx bytes at 0x%016lx in PD[0x%03lx]\n",
               size, va & ~PT_MASK, get_pde(va));
        pde = &pd[get_pde(va)];
        if (!pde->p) {
                page_table_list_t *ptl = new_page_table_list(ctx);

                if (!ptl) {
                        warn("Couldn't allocate page table list");
                        return -1;
                }

                pt = &ptl->table.pt[0];
                pde->pt_base = ptr64_to_pfn40(&ptl->table.pt[0]);
                printf("      create PD[0x%03lx] (pt_base:0x%lx nx:%d us:%d rw:%d)\n", get_pde(va), (uintptr_t)pde->pt_base, nx, user, rw);
                pde->nx = nx;
                pde->pwt = pde->rw = rw;
                pde->us = user;
                pde->ps = size == PD_SIZE;
                pde->p = 1;
        } else {
                if (!nx && pde->nx)
                        pde->nx = 0;
                if (user && !pde->us) {
                        user = true;
                        pde->us = 1;
                }
                if (rw && !pde->rw) {
                        rw = true;
                        pde->rw = 1;
                }
                printf("      update PD[0x%03lx] (pt_base:0x%lx nx:%d->%d us:%d->%d rw:%d->%d)\n", get_pde(va), (uintptr_t)pde->pt_base, pde->nx, nx, pde->us, user, pde->rw, rw);
                pt = pfn40_to_ptr64(pde->pt_base);
        }

        if (pde->ps)
                return 0;

        return 0;
        return map_pt_entries(pt, va, size, nx, user, rw);
}

int private nonnull(1, 2)
map_pd_entries(struct context *ctx, pde_t *pd,
               uintptr_t va, size_t size,
               bool nx, bool user, bool rw)
{
        do {
                int rc;
                uint64_t base = va & ~(PD_SIZE-1);
                uint64_t rem = base + PD_SIZE - va;
#if 0
                printf("va:   0x%016lx\n", va);
                printf("base: 0x%016lx\n", base);
                printf("rem:  0x%016lx\n", rem);
                printf("      size:0x%016lx PD_SIZE:0x%016lx rem:0x%016lx\n", size, PD_SIZE, rem);
#endif

                rc = map_pd_entry(ctx, pd,
                                  va, min(size, rem),
                                  nx, user, rw);
                if (rc < 0)
                        return rc;

                va += min(size, rem);
                size -= min(size, rem);
        } while (size);

        return 0;
}

int private nonnull(1, 2)
map_pdp_entry(struct context *ctx, pdpe_t *pdp,
              uintptr_t va, size_t size,
              bool nx, bool user, bool rw)
{
        pdpe_t *pdpe;
        pde_t *pd;

        printf("   Mapping 0x%lx bytes at 0x%016lx in PDP[0x%03lx]\n",
               size, va & ~PD_MASK, get_pdpe(va));
        pdpe = &pdp[get_pdpe(va)];
        if (!pdpe->p) {
                page_table_list_t *ptl = new_page_table_list(ctx);

                if (!ptl) {
                        warn("Couldn't allocate page table list");
                        return -1;
                }

                pd = &ptl->table.pd[0];
                pdpe->pd_base = ptr64_to_pfn40(pd);
                printf("    create PDP[0x%03lx] (pd_base:0x%lx nx:%d us:%d rw:%d)\n", get_pdpe(va), (uintptr_t)pdpe->pd_base, nx, user, rw);
                pdpe->nx = nx;
                pdpe->pwt = pdpe->rw = rw;
                pdpe->us = user;
                pdpe->ps = size == PDP_SIZE;
                pdpe->p = 1;
        } else {
                if (!nx && pdpe->nx)
                        pdpe->nx = 0;
                if (user && !pdpe->us) {
                        user = true;
                        pdpe->us = 1;
                }
                if (rw && !pdpe->rw) {
                        rw = true;
                        pdpe->rw = 1;
                }
                printf("    update PDP[0x%lx] (pd_base:0x%lx nx:%d->%d us:%d->%d rw:%d->%d)\n", get_pdpe(va), (uintptr_t)pdpe->pd_base, pdpe->nx, nx, pdpe->us, user, pdpe->rw, rw);

                pd = pfn40_to_ptr64(pdpe->pd_base);
        }

        if (pdpe->ps)
                return 0;

        return 0;
        return map_pd_entries(ctx, pd, va, size, nx, user, rw);
}

int private nonnull(1, 2)
map_pdp_entries(struct context *ctx, pdpe_t *pdp,
                uintptr_t va, size_t size,
                bool nx, bool user, bool rw)
{
        do {
                int rc;
                uint64_t base = va & ~(PDP_SIZE-1);
                uint64_t rem = base + PDP_SIZE - va;

                rc = map_pdp_entry(ctx, pdp,
                                   va, min(size, rem),
                                   nx, user, rw);
                if (rc < 0)
                        return rc;

                va += min(size, rem);
                size -= min(size, rem);
        } while (size);

        return 0;
}

int private nonnull(1, 2)
map_pml4_entry(struct context *ctx, pml4e_t *pml4,
               uintptr_t va, size_t size,
               bool nx, bool user, bool rw)
{
        pml4e_t *pml4e;
        pdpe_t *pdp;

        pml4e = &pml4[get_pml4e(va)];
        qprintf("pml4:%p pml4[0x%lx] is at %p\n", pml4, get_pml4e(va), pml4e);
        printf(" Mapping 0x%lx bytes at 0x%016lx in PML4[0x%03lx]\n",
               size, va & ~PDP_MASK, get_pml4e(va));
        if (!pml4e->p) {
                page_table_list_t *ptl = new_page_table_list(ctx);

                if (!ptl) {
                        warn("Couldn't allocate page table list");
                        return -1;
                }

                pdp = &ptl->table.pdp[0];
                pml4e->pdp_base = ptr64_to_pfn40(pdp);
                printf("  create PML4[0x%03lx] (pdp_base:0x%lx nx:%d us:%d rw:%d)\n", get_pml4e(va), (uintptr_t)pml4e->pdp_base, nx, user, rw);
                pml4e->nx = nx;
                pml4e->pwt = pml4e->rw = rw;
                pml4e->us = user;
                pml4e->p = 1;
        } else {
                pdp = pfn40_to_ptr64(pml4e->pdp_base);
                bool update = false;
                if (!nx && pml4e->nx) {
                        pml4e->nx = 0;
                        update = true;
                }
                if (user && !pml4e->us) {
                        user = true;
                        pml4e->us = 1;
                        update = true;
                }
                if (rw && !pml4e->rw) {
                        rw = true;
                        pml4e->rw = 1;
                        update = true;
                }

                if (update)
                        printf("  update PML4[0x%03lx] (pdp_base:0x%lx nx:%d->%d us:%d->%d rw:%d->%d)\n", get_pml4e(va), (uintptr_t)pml4e->pdp_base, pml4e->nx, nx, pml4e->us, user, pml4e->rw, rw);
        }

        return 0;
        return map_pdp_entries(ctx, pdp, va, size, nx, user, rw);
}

int private nonnull(1, 2)
map_pml4_entries(struct context *ctx, pml4e_t *pml4,
                 uintptr_t va, size_t size,
                 bool nx, bool user, bool rw)
{
        do {
                int rc;
                uint64_t base = va & ~(PML4_SIZE-1);
                uint64_t rem = base + PML4_SIZE - va;

                printf("Mapping pml4 entries from 0x%016lx to 0x%016lx\n",
                       va, va + size);

#if 0
                printf("va:   0x%016lx & 0x%016lx = 0x%016lx\n", va, ~(PML4_SIZE-1), va & ~(PML4_SIZE-1));
                printf("base: 0x%016lx\n", base);
                printf("rem:  0x%016lx\n", rem);
#endif

                rc = map_pml4_entry(ctx, pml4,
                                    va, min(size, rem),
                                    nx, user, rw);
                if (rc < 0)
                        return rc;

                va += min(size, rem);
                size -= min(size, rem);
        } while (size);

        return 0;
}

int private nonnull(1)
map_pages(struct context *ctx,
          uintptr_t va, size_t size,
          bool nx, bool user, bool rw)
{
        page_table_list_t *pt;
        pml4e_t *pml4 = NULL;
        int rc;

        pt = list_entry(ctx->page_tables.prev, page_table_list_t, list);
        pml4 = &pt->table.pml4[0];
        if (!pml4)
                return -1;

        printf("Mapping pages for 0x%016lx to 0x%016lx nx:%d us:%d rw:%d\n", va, va + size, nx, user, rw);

        int16_t prev_pml4e = -1;
        int16_t prev_pdpe = -1;
        int16_t prev_pde = -1;
        int16_t prev_pte = -1;
        for (uintptr_t pgva = va; pgva < va+size; pgva += PAGE_SIZE) {
                int16_t pml4e, pdpe, pde, pte;

                pdpe_t *pdp = NULL;
                pde_t *pd = NULL;
                pte_t *pt = NULL;

                pml4e = get_pml4e(pgva);
                if (pml4e != prev_pml4e) {
                        //printf("next pml4[0x%03hx]\n", pml4e);
                        rc = map_pml4_entry(ctx, pml4, pgva, size, nx, user, rw);
                        if (rc < 0)
                                return rc;
                }

                pdpe = get_pdpe(pgva);
                pdp = (pdpe_t *)pfn40_to_ptr64(pml4[pml4e].pdp_base);
                if (pdpe != prev_pdpe) {
                        //printf("  next pdp[0x%03hx]\n", pdpe);
                        rc = map_pdp_entry(ctx, pdp, pgva, size, nx, user, rw);
                        if (rc < 0)
                                return rc;
                        if (size >= PDP_SIZE) {
                                pgva += PDP_SIZE - PAGE_SIZE;
                                size -= PDP_SIZE;
                                continue;
                        }
                }

                pde = get_pde(pgva);
                pd = (pde_t *)pfn40_to_ptr64(pdp[pdpe].pd_base);
                if (pde != prev_pde) {
                        //printf("    next pd[0x%03hx]\n", pde);
                        rc = map_pd_entry(ctx, pd, pgva, size, nx, user, rw);
                        if (rc < 0)
                                return rc;
                        if (size >= PD_SIZE) {
                                pgva += PD_SIZE - PAGE_SIZE;
                                size -= PD_SIZE;
                                continue;
                        }
                }

                pte = get_pte(pgva);
                pt = (pte_t *)pfn40_to_ptr64(pd[pde].pt_base);
                if (pte != prev_pte) {
                        //printf("        next pt[0x%03hx]\n", pte);
                        rc = map_pt_entry(pt, pgva, nx, user, rw);
                        if (rc < 0)
                                return rc;
                        size -= PAGE_SIZE;
                }

                prev_pml4e = pml4e;
                prev_pdpe = pdpe;
                prev_pde = pde;
                prev_pte = pte;
        }

        return 0;
        return map_pml4_entries(ctx, pml4, va, size, nx, user, rw);
}

int private
finalize_paging(struct context *ctx)
{
        ssize_t nmaps = 0;
        list_t maps;
        struct list_head *this;
        page_table_t *tables = NULL;
        pml4e_t *pml4;
        unsigned int n;

        struct kvm_sregs sregs;
        int rc;

        printf("Building page tables\n");

        fflush(stdout);
        nmaps = get_map_array(&ctx->guest_maps, &maps);
        if (nmaps < 0) {
                warn("Could not get memory map list");
                return -1;
        }
        printf("got %zd maps:\n", nmaps);
        for (unsigned int i = 0; i < nmaps; i++) {
                struct proc_map *map;
                map = list_entry(maps.prev, struct proc_map, list);
                printf("map[%d] at %p: va:0x%016llx %s\n", i, &map[i], map[i].kumr.userspace_addr, map[i].name);
                fflush(stdout);
        }
        sort_maps(list_entry(maps.prev, struct proc_map, list), nmaps);

        list_for_each(this, &ctx->guest_maps) {
                struct proc_map *map;

                map = list_entry(this, struct proc_map, list);
                if (!map->user_pages)
                        continue;
                rc = map_pages(ctx, map->start, map->end - map->start,
                               map->mode & M_X_OK, map->mode & M_R_OK,
                               map->mode & M_W_OK);
        }
        struct proc_map *map = list_entry(maps.prev, struct proc_map, list);
        free(map);

        rc = posix_memalign((void **)&tables, PAGE_SIZE,
                            PAGE_SIZE * ctx->ntables);
        if (rc < 0) {
                warn("Could not allocate page tables");
                goto err;
        }
        memset(tables, 0, PAGE_SIZE * ctx->ntables);
        ctx->page_table_map->kumr.userspace_addr = (uintptr_t)tables;

        n = 0;
        pml4 = tables[n++].pml4;
        for (uint16_t i = 0; i < 512; i++) {
                pdpe_t *opdp, *pdp;
                pml4e_t *opml4e = &ctx->pml4[i];

                if (!opml4e->p)
                        continue;

                memcpy(&pml4[i], opml4e, sizeof(*opml4e));
                opdp = pfn40_to_ptr64(opml4e->pdp_base);
                pdp = tables[n++].pdp;
                pml4[i].pdp_base = ptr64_to_pfn40(pdp);

                for (uint16_t j = 0; j < 512; j++) {
                        pde_t *opd, *pd;
                        pdpe_t *opdpe = &opdp[j];

                        if (!opdpe->p)
                                continue;

                        memcpy(&pdp[j], opdpe, sizeof(*opdpe));
                        opd = pfn40_to_ptr64(opdpe->pd_base);
                        pd = tables[n++].pd;
                        pdp[j].pd_base = ptr64_to_pfn40(pdp);

                        if (pdp[j].ps)
                                continue;

                        for (uint16_t k = 0; k < 512; k++) {
                                pte_t *opt, *pt;
                                pde_t *opde = &opd[k];

                                if (!opde->p)
                                        continue;

                                memcpy(&pd[k], opde, sizeof(*opde));
                                opt = pfn40_to_ptr64(opde->pt_base);
                                pt = tables[n++].pt;
                                pd[k].pt_base = ptr64_to_pfn40(pt);

                                memcpy(pt, opt, sizeof(*opt));
                        }
                }
        }

        rc = vcpu_ioctl(ctx, KVM_GET_SREGS, &sregs);
        if (rc < 0) {
                warn("Could not get VCPU SREGS");
                goto err;
        }

        cr3_t *cr3 = (cr3_t *)&sregs.cr3;
        cr3->pml4_base = ptr64_to_pfn40(ctx->pml4);
        cr3->pwt = 1;

        dump_pgtbls(*cr3);

        cr4_t *cr4 = (cr4_t *)&sregs.cr4;
        cr4->cr4 = 0;
        cr4->pae = 1;
        /* enable SSE instruction */
        cr4->osfxsr = 1;
        cr4->osxmmexcpt = 1;

        cr0_t *cr0 = (cr0_t *)&sregs.cr0;
        cr0->cr0 = 0;
        cr0->pe = 1;
        cr0->mp = 1;
        cr0->et = 1;
        cr0->ne = 1;
        cr0->wp = 1;
        cr0->am = 1;
        cr0->pg = 1;

        efer_t *efer = (efer_t *)&sregs.efer;
        efer->efer = 0;
        efer->lme = 1;
        efer->lma = 1;
        /* enable syscall instruction */
        /* efer->sce = 1; */

        rc = vcpu_ioctl(ctx, KVM_SET_SREGS, &sregs);
        if (rc < 0) {
                warn("Could not set VCPU SREGS");
                goto err;
        }

        return 0;
err:
        return -1;
}

static void unused
dump_sreg(const char * const name, struct kvm_segment sreg)
{
        printf("%s:", name);
        printf("base=0x%016llx lim=0x%08x sel=0x%04hx type=0x%02hhx ",
               sreg.base, sreg.limit, sreg.selector, sreg.type);
        printf("pres=0x%02hhx dpl=0x%02hhx db=0x%02hhx s=0x%02hhx ",
               sreg.present, sreg.dpl, sreg.db, sreg.s);
        printf("l=0x%02hhx g=0x%02hhx avl=0x%02hhx\n",
               sreg.l, sreg.g, sreg.avl);
}

int private
init_segments(struct context *ctx) {
        struct kvm_sregs sregs;
        int rc;

        rc = vcpu_ioctl(ctx, KVM_GET_SREGS, &sregs);
        if (rc < 0) {
                warn("Could not get VCPU SREGS");
                goto err;
        }

        sregs.cs.base = sregs.cs.selector = 0;

        dump_sreg("cs", sregs.cs);
#if 0
        dump_sreg("ds", sregs.ds);
        dump_sreg("es", sregs.es);
        dump_sreg("fs", sregs.fs);
        dump_sreg("gs", sregs.gs);
        dump_sreg("ss", sregs.ss);
        dump_sreg("ds", sregs.ds);
#endif

        rc = vcpu_ioctl(ctx, KVM_SET_SREGS, &sregs);
        if (rc < 0) {
                warn("Could not set VCPU SREGS");
                goto err;
        }

        return 0;
err:
        return -1;
}

// vim:fenc=utf-8:tw=75:et
