/*
 * cpu.h
 * Copyright 2019 Peter Jones <pjones@redhat.com>
 */

#ifndef CPU_H_
#define CPU_H_

/* CR0 bits */
#define CR0_PE 1u
#define CR0_MP (1u << 1)
#define CR0_EM (1u << 2)
#define CR0_TS (1u << 3)
#define CR0_ET (1u << 4)
#define CR0_NE (1u << 5)
#define CR0_WP (1u << 16)
#define CR0_AM (1u << 18)
#define CR0_NW (1u << 29)
#define CR0_CD (1u << 30)
#define CR0_PG (1u << 31)

/* CR4 bits */
#define CR4_VME 1u
#define CR4_PVI (1u << 1)
#define CR4_TSD (1u << 2)
#define CR4_DE (1u << 3)
#define CR4_PSE (1u << 4)
#define CR4_PAE (1u << 5)
#define CR4_MCE (1u << 6)
#define CR4_PGE (1u << 7)
#define CR4_PCE (1u << 8)
#define CR4_OSFXSR (1u << 9)
#define CR4_OSXMMEXCPT (1u << 10)
#define CR4_UMIP (1u << 11)
#define CR4_VMXE (1u << 13)
#define CR4_SMXE (1u << 14)
#define CR4_FSGSBASE (1u << 16)
#define CR4_PCIDE (1u << 17)
#define CR4_OSXSAVE (1u << 18)
#define CR4_SMEP (1u << 20)
#define CR4_SMAP (1u << 21)
#define CR4_PKE (1u << 22)

#define EFER_SCE 1
#define EFER_LME (1 << 8)
#define EFER_LMA (1 << 10)
#define EFER_NXE (1 << 11)
#define EFER_SVME (1 << 12)
#define EFER_LMSLE (1 << 13)
#define EFER_FFXSR (1 << 14)
#define EFER_TCE (1 << 15)

/* 64-bit page * entry bits */
#define PDE64_PRESENT 1
#define PDE64_RW (1 << 1)
#define PDE64_USER (1 << 2)
#define PDE64_ACCESSED (1 << 5)
#define PDE64_DIRTY (1 << 6)
#define PDE64_PS (1 << 7)
#define PDE64_G (1 << 8)

#define ALIGN_PADDING(addr, align) (((align) - ((addr) % (align))) % (align))
#define ALIGN_DOWN(addr, align) ((addr) - ((align) - ALIGN_PADDING(addr, align)))
#define ALIGN_UP(addr, align) ((addr) + ALIGN_PADDING(addr, align))

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#endif

#define PFN_MAX 0x0000fffffffff000

#define BYTES_TO_PAGES(bytes) ((bytes) >> PAGE_SHIFT)
#define PAGES_TO_BYTES(pages) ((pages) << PAGE_SHIFT)

#define PAGE_ALIGN_UP(bytes) ALIGN_UP(bytes, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(bytes) ALIGN_DOWN(bytes, PAGE_SIZE)

#define N_PAGES(bytes) BYTES_TO_PAGES(PAGE_ALIGN_UP(bytes))
#define STARTING_PAGE(addr) BYTES_TO_PAGES(PAGE_ALIGN_DOWN(bytes))

typedef struct {
        uint64_t reserved:12;
        uint64_t pml4_base:40;
        uint64_t ignored0:7;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t ignored1:3;
} cr3_t;

static inline void
set_cr3_addr(cr3_t *cr3, uintptr_t ptr)
{
        cr3->pml4_base = ptr & 0x000fffffffffffffull;
}

static inline uintptr_t
get_cr3_addr(cr3_t *cr3)
{
        uintptr_t ret = cr3->pml4_base;

        if (ret & 0x0008000000000000ull)
                ret |= 0xfff0000000000000ull;

        return ret;
}

typedef struct {
        uint64_t nx:1;
        uint64_t ignored0:11;
        uint64_t pdp_base:40;
        uint64_t avl:3;
        uint64_t reserved1:2;
        uint64_t ignored2:1;
        uint64_t a:1;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t us:1;
        uint64_t rw:1;
        uint64_t p:1;
} pml4e_t;

typedef struct {
        uint64_t nx:1;
        uint64_t ignored0:11;
        uint64_t pd_base:40;
        uint64_t avl:3;
        uint64_t ignored1:1;
        uint64_t zero:1;
        uint64_t ignored2:1;
        uint64_t a:1;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t us:1;
        uint64_t rw:1;
        uint64_t p:1;
} pdpe_t;

typedef struct {
        uint64_t nx:1;
        uint64_t ignored0:11;
        uint64_t pt_base:40;
        uint64_t avl:3;
        uint64_t ignored1:1;
        uint64_t zero:1;
        uint64_t ignored2:1;
        uint64_t a:1;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t us:1;
        uint64_t rw:1;
        uint64_t p:1;
} pde_t;

typedef struct {
        uint64_t nx:1;
        uint64_t ignored0:11;
        uint64_t page_base:40;
        uint64_t avl:3;
        uint64_t g:1;
        uint64_t pat:1;
        uint64_t d:1;
        uint64_t a:1;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t us:1;
        uint64_t rw:1;
        uint64_t p:1;
} pte_t;

#define pml4e_shift 39ull
#define pml4e_mask 0x1ffull
#define pdpe_shift 30ull
#define pdpe_mask 0x1ffull
#define pde_shift 21ull
#define pde_mask 0x1ffull
#define pte_shift 12ull
#define pte_mask 0x1ffull
#define offset_mask 0xfffull

#define get_pml4e(addr) (((addr) >> pml4e_shift) & pml4e_mask)
#define get_pdpe(addr) (((addr) >> pdpe_shift) & pdpe_mask)
#define get_pde(addr) (((addr) >> pde_shift) & pde_mask)
#define get_pte(addr) (((addr) >> pte_shift) & pte_mask)
#define get_offset(addr) ((addr) & offset_mask)

#define clear_bits(val, shift, mask) \
        (((unsigned long long)(val)) & ~((mask) << (shift)))
#define shift_and_mask(val, shift, mask) \
        ((((unsigned long long)(val)) >> (shift)) & (mask))
#define set_bits(val, bits, shift, mask) \
        ((val) | shift_and_mask(bits, shift, mask))
#define apply_bits(val, bits, shift, mask) \
        ((val) = (set_bits(clear_bits(val, shift, mask), bits, shift, mask)))

#define set_pml4e(addr, pml4e) apply_bits(addr, pml4e, pml4e_shift, pml4e_mask)
#define set_pdpe(addr, pdpe) apply_bits(addr, pdpe, pdpe_shift, pdpe_mask)
#define set_pde(addr, pde) apply_bits(addr, pde, pde_shift, pde_mask)
#define set_pte(addr, pte) apply_bits(addr, pte, pte_shift, pte_mask)
#define set_offset(addr, offset) apply_bits(addr, offset, 0, offset_mask)

#define paging_to_addr(pml4e, pdpe, pde, pte, offset)   \
        ({                                              \
                uint64_t addr_ = 0;                     \
                set_pml4e(addr_, pml4e);                \
                set_pdpe(addr_, pdpe);                  \
                set_pde(addr_, pde);                    \
                set_pte(addr_, pte);                    \
                set_offset(addr_, offset);              \
                addr_;                                  \
        })

#define get_pfn(addr) ((addr) >> PAGE_SHIFT)

extern int private init_paging(struct context *ctx);
extern int private init_segments(struct context *ctx);

#endif /* !CPU_H_ */
// vim:fenc=utf-8:tw=75:et
