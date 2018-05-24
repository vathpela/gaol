/*
 * reloc.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include "gaol.h"

#include <elf.h>

#if __SIZE_WIDTH__ == 64
typedef Elf64_Rela relocation;
#define R_TYPE(x) ELF64_R_TYPE (x)
#else
typedef Elf32_Rel relocation;
#define R_TYPE(x) ELF32_R_TYPE (x)
#endif

#if defined(__aarch64__)
#define R_NONE R_AARCH64_NONE
#define R_RELATIVE R_AARCH64_RELATIVE
#elif defined(__arm__)
#define R_NONE R_ARM_NONE
#define R_RELATIVE R_ARM_RELATIVE
#elif defined(__x86_64__)
#define R_NONE R_X86_64_NONE
#define R_RELATIVE R_X86_64_RELATIVE
#elif defined(__i386__)
#define R_NONE R_386_NONE
#define R_RELATIVE R_386_RELATIVE
#else
#error If your arch was here, you could be building right now.
#endif

int _relocate (long ldbase, dynamic_section_ *dyn,
               int argc unused, char *argv[] unused)
{
	relocation *rel = 0;
	long relsz = 0, relent = 0;
	unsigned long *addr;
	int i;

	for (i = 0; dyn[i].d_tag != DT_NULL; ++i) {
		switch (dyn[i].d_tag) {
			case DT_REL:
			case DT_RELA:
				rel = (relocation*)
					((unsigned long)dyn[i].d_un.d_ptr
					 + ldbase);
				break;

			case DT_RELSZ:
			case DT_RELASZ:
				relsz = dyn[i].d_un.d_val;
				break;

			case DT_RELENT:
			case DT_RELAENT:
				relent = dyn[i].d_un.d_val;
				break;

			default:
				break;
		}
	}

	if (!rel && relent == 0)
		return 0;

	if (!rel || relent == 0)
		return -1;

	while (relsz > 0) {
		/* apply the relocs */
                switch (R_TYPE(rel->r_info)) {
			case R_NONE:
				break;

			case R_RELATIVE:
				addr = (unsigned long *)
					(ldbase + rel->r_offset);
				*addr = ldbase + rel->r_addend;
				break;

			default:
				break;
		}
		rel = (relocation*) ((char *) rel + relent);
		relsz -= relent;
	}
	return 0;
}

// vim:fenc=utf-8:tw=75:et
