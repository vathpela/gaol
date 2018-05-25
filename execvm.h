/*
 * execvm.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef EXECVM_H_
#define EXECVM_H_

extern void noreturn execvm(const char * filename, char * const argv[]) hidden;

#endif /* !EXECVM_H_ */
// vim:fenc=utf-8:tw=75:et
