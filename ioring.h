/*
 * ioring.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef IORING_H_
#define IORING_H_

#include <unistd.h>
#include <inttypes.h>

extern int ioring_write(const char * const buf, size_t size);
extern int ioring_read(char * const buf, size_t size);
extern void ioring_swap_rings(void);

#endif /* !IORING_H_ */
// vim:fenc=utf-8:tw=75:et
