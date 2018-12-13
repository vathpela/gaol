/*
 * ioring.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef IORING_H_
#define IORING_H_

#include <inttypes.h>

extern int ioring_map_rings(void);
extern int ioring_write(const char * const buf, size_t size);
extern int ioring_read(char * const buf, size_t size);

struct ioring {
        uint32_t version;
        uint16_t lock;
        uint16_t size;
        uint8_t buf[4096] aligned(4096);
} aligned(4096);

struct iorings {
        struct ioring one, two;
        struct ioring *input;
        struct ioring *output;
} aligned(4096);

#endif /* !IORING_H_ */
// vim:fenc=utf-8:tw=75:et
