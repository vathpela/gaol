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

typedef struct {
                uint64_t reserved:12;
                int64_t pml4_base:40;
                uint64_t ignored0:7;
                uint64_t pcd:1;
                uint64_t pwt:1;
                uint64_t ignored1:3;
} cr3_t;

typedef struct {
        uint64_t nx:1;
        int64_t pdp_base:51;
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
        int64_t pd_base:51;
        uint64_t avl:3;
        uint64_t ignored1:1;
        uint64_t zero:1;
        uint64_t ps:1;
        uint64_t a:1;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t us:1;
        uint64_t rw:1;
        uint64_t p:1;
} pdpe_t;

typedef struct {
        uint64_t nx:1;
        uint64_t pt_base:51;
        uint64_t avl:3;
        uint64_t ignored1:1;
        uint64_t zero:1;
        uint64_t ps:1;
        uint64_t a:1;
        uint64_t pcd:1;
        uint64_t pwt:1;
        uint64_t us:1;
        uint64_t rw:1;
        uint64_t p:1;
} pde_t;

typedef struct {
        uint64_t nx:1;
        int64_t page_base:51;
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

typedef union {
        uint64_t data[512];
        pml4e_t pml4[512];
        pdpe_t pdp[512];
        pde_t pd[512];
        pte_t pt[512];
} page_table_t;

typedef struct {
        page_table_t table;
        list_t list;
} page_table_list_t;

#define ALIGN_PADDING(addr, align) (((align) - ((addr) % (align))) % (align))
#define ALIGN_DOWN(addr, align) ((addr) - ((align) - ALIGN_PADDING(addr, align)))
#define ALIGN_UP(addr, align) ((addr) + ALIGN_PADDING(addr, align))

#define signex(val, bits) ({ \
        __typeof__(val) test_ = 1ul << ((bits) - 1);                    \
        __typeof__(val) mask_ = ~((test_ << 1) - 1);                    \
        __typeof__(val) ret_ = (((val) & test_)                         \
                        ? ((val) | mask_)                               \
                        : (val));                                       \
        ret_;                                                           \
        })

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#endif

#define PT_SHIFT PAGE_SHIFT
#define PT_SIZE PAGE_SIZE

#define PD_SHIFT 21
#define PD_SIZE  (1 << PD_SHIFT)

#define PDP_SHIFT 30
#define PDP_SIZE (1 << PDP_SHIFT)

#define BYTES_TO_PAGES(bytes) ((bytes) >> PAGE_SHIFT)
#define PAGES_TO_BYTES(pages) ((pages) << PAGE_SHIFT)

#define PAGE_ALIGN_UP(bytes) ALIGN_UP(bytes, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(bytes) ALIGN_DOWN(bytes, PAGE_SIZE)

#define N_PAGES(bytes) BYTES_TO_PAGES(PAGE_ALIGN_UP(bytes))
#define STARTING_PAGE(addr) BYTES_TO_PAGES(PAGE_ALIGN_DOWN(bytes))

#define PFN30_MASK 0x000000003ffffffful
#define ptr64_to_pfn30(ptr) \
        ((((uintptr_t)ptr) >> PAGE_SHIFT) & PFN30_MASK)

#define PFN40_MASK 0x000000fffffffffful
#define ptr64_to_pfn40(ptr) \
        ((((uintptr_t)ptr) >> PAGE_SHIFT) & PFN40_MASK)

#define pgoff12(ptr) \
        (((intptr_t)ptr) & 0xffful)
#define pgoff21(ptr) \
        (((intptr_t)ptr) & 0xffffful)
#define pgoff30(ptr) \
        (((intptr_t)ptr) & 0x3ffffffful)

#define pfn51_to_ptr64(ptr) ((void *)signex((((intptr_t)(ptr)) << PAGE_SHIFT), 51))
#define pfn40_to_ptr64(ptr) ((void *)signex((((intptr_t)(ptr)) << PAGE_SHIFT), 40))
#define pfn30_to_ptr64(ptr) ((void *)signex((((intptr_t)(ptr)) << PAGE_SHIFT), 30))

#define pml4e_shift 39ul
#define pml4e_mask 0x1fful
#define pdpe_shift 30ul
#define pdpe_mask 0x1fful
#define pdpe_offset_mask ((1ul << pdpe_shift) - 1)
#define pde_shift 21ul
#define pde_mask 0x1fful
#define pde_offset_mask ((1ul << pde_shift) - 1)
#define pte_shift 12ul
#define pte_mask 0x1fful
#define pte_offset_mask ((1ul << pte_shift) - 1)

#define get_pml4e(addr) (((addr) >> pml4e_shift) & pml4e_mask)
#define get_pdpe(addr) (((addr) >> pdpe_shift) & pdpe_mask)
#define get_pde(addr) (((addr) >> pde_shift) & pde_mask)
#define get_pte(addr) (((addr) >> pte_shift) & pte_mask)
#define get_pte_offset(addr) ((addr) & pte_offset_mask)
#define get_pde_offset(addr) ((addr) & pde_offset_mask)
#define get_pdpe_offset(addr) ((addr) & pdpe_offset_mask)

#define clear_bits(val, shift, mask) \
        (((unsigned long)(val)) & ~((mask) << (shift)))
#define rshift_and_mask(val, shift, mask) \
        ((((unsigned long)(val)) >> (shift)) & (mask))
#define mask_and_lshift(val, shift, mask) \
        ((((unsigned long)(val)) & (mask)) << (shift))
#define set_bits(val, bits, shift, mask) \
        ((val) | mask_and_lshift(bits, shift, mask))
#define apply_bits(val, bits, shift, mask) \
        ((val) = (set_bits(clear_bits(val, shift, mask), bits, shift, mask)))

#define set_pml4e(addr, pml4e) set_bits(addr, pml4e, pml4e_shift, pml4e_mask)
#define set_pdpe(addr, pdpe) set_bits(addr, pdpe, pdpe_shift, pdpe_mask)
#define set_pde(addr, pde) set_bits(addr, pde, pde_shift, pde_mask)
#define set_pte(addr, pte) set_bits(addr, pte, pte_shift, pte_mask)
#define set_pte_offset(addr, offset) \
        set_bits(addr, offset, 0, pte_offset_mask)
#define set_pde_offset(addr, offset) \
        set_bits(addr, offset, 0, pde_offset_mask)
#define set_pdpe_offset(addr, offset) \
        set_bits(addr, offset, 0, pdpe_offset_mask)

#define pdpe_to_addr(pml4e, pdpe, offset)               \
        ({                                              \
                intptr_t addr_ = 0;                     \
                printf("\npdpe_to_addr(0x%03hx, 0x%03hx, 0x%016lx)\n", \
                       pml4e, pdpe, offset);                            \
                printf("  set_pml4e(0x%016lx, 0x%016x) -> 0x%016lx\n", \
                       addr_, pml4e, set_pml4e(addr_, pml4e));           \
                addr_ = set_pml4e(addr_, pml4e);                        \
                printf("  set_pdpe(0x%016lx, 0x%016x) -> 0x%016lx\n",  \
                       addr_, pdpe, set_pdpe(addr_, pdpe));             \
                addr_ = set_pdpe(addr_, pdpe);                          \
                printf("  set_pdpe_offset(0x%016lx, 0x%016lx) -> 0x%016lx\n",\
                       addr_, offset, set_pdpe_offset(addr_, offset));  \
                addr_ = set_pdpe_offset(addr_, offset);                 \
                addr_;                                                  \
        })

#define pde_to_addr(pml4e, pdpe, pde, offset)           \
        ({                                              \
                intptr_t addr_ = 0;                     \
                addr_ = set_pml4e(addr_, pml4e);        \
                addr_ = set_pdpe(addr_, pdpe);          \
                addr_ = set_pde(addr_, pde);            \
                addr_ = set_pde_offset(addr_, offset);  \
                addr_;                                  \
        })

#define pte_to_addr(pml4e, pdpe, pde, pte, offset)      \
        ({                                              \
                intptr_t addr_ = 0;                     \
                addr_ = set_pml4e(addr_, pml4e);        \
                addr_ = set_pdpe(addr_, pdpe);          \
                addr_ = set_pde(addr_, pde);            \
                addr_ = set_pte(addr_, pte);            \
                addr_ = set_pte_offset(addr_, offset);  \
                addr_;                                  \
        })

#define get_pfn(addr) ((addr) >> PAGE_SHIFT)

#define virt_to_phys(pml4_base, vaddr)                                  \
        ({                                                              \
                uint16_t pml4en_ = get_pml4e(vaddr);                    \
                uint16_t pdpen_ = get_pdpe(vaddr);                      \
                pml4e_t *pml4e = ((pml4e_t *)pml4_base) + pml4en_;      \
                struct list_head *n_, *pos_;                            \
                pdpe_t *pdpe = (

typedef enum {
        CR3,
        PML4E,
        PDPE,
        PDE,
        PTE
} pt_type;

typedef union {
        struct {
                uint16_t si:13;
                uint16_t ti:1;
                uint16_t rpl:2;
        };
        uint16_t value;
} segment_selector;

#define selector(SI, TI, RPL) \
        ((uint16_t)(segment_selector){ .si = SI, .ti = TI, .rpl = RPL }.value)

typedef enum {
        RING0 = 0,
        KERNEL = 0,
        RING1,
        RING2,
        USER = 3,
        RING3 = 3,
} ring;

typedef enum {
        ES, CS=0xb, SS=0x2, DS, FS, GS
} segreg;

#endif /* !CPU_H_ */
// vim:fenc=utf-8:tw=75:et
