/*
 * compiler.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef COMPILER_H_
#define COMPILER_H_

#define unused __attribute__((__unused__))
#define hidden __attribute__((__visibility__ ("hidden")))
#define private hidden
#define public __attribute__((__visibility__ ("default")))
#define destructor __attribute__((destructor))
#define constructor __attribute__((constructor))
#define alias(x) __attribute__((weak, alias (#x)))
#define nonnull(...) __attribute__((__nonnull__(__VA_ARGS__)))
#define PRINTF(...) __attribute__((__format__(printf, __VA_ARGS__)))
#define flatten __attribute__((__flatten__))
#define packed __attribute__((__packed__))
#define aligned(x) __attribute__((__aligned__(x)))
#define version(sym, ver) __asm__(".symver " # sym "," # ver)
#define noreturn __attribute__((__noreturn__))
#define section(x) __attribute__((__section__(x)))

#define min(x, y) (((x) >= (y)) ? (y) : (x))
#define max(x, y) (((x) > (y)) ? (x) : (y))

#endif /* !COMPILER_H_ */
// vim:fenc=utf-8:tw=75:et
