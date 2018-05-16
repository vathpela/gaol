/*
 * reloc.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef RELOC_H_
#define RELOC_H_

#include <elf.h>

#if __SIZE_WIDTH__ == 64
typedef Elf64_Dyn dynamic_section_;
#else
typedef Elf32_Dyn dynamic_section_;
#endif

int hidden _relocate (long ldbase, dynamic_section_ *dyn,
                      int argc unused, char *argv[] unused);

#endif /* !RELOC_H_ */
// vim:fenc=utf-8:tw=75:et
