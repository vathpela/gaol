/*
 * gaol.h
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 */

#ifndef GAOL_H_
#define GAOL_H_

#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/kvm.h>
#include <linux/psp-sev.h>

#if 0
#include <alloca.h>
#include <dirent.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <tgmath.h>
#endif

#include "compiler.h"
#include "list.h"

#include "context.h"
#include "util.h"
#include "ioring.h"
#include "dump.h"
#include "execvm.h"
#include "mmu.h"

#endif /* !GAOL_H_ */
// vim:fenc=utf-8:tw=75:et
