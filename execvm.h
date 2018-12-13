/*
 * execvm.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef EXECVM_H_
#define EXECVM_H_

#include <sys/types.h>
#include <unistd.h>

typedef int vmid_t;
extern vmid_t forkvm(const char * filename, char * const argv[]) hidden;

#endif /* !EXECVM_H_ */
// vim:fenc=utf-8:tw=75:et
