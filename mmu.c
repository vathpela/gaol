/*
 * mmu.c - vm mmu setup
 * Copyright 2019 Peter Jones <pjones@redhat.com>
 *
 */

#include "gaol.h"

#include <err.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/mman.h>

int private
init_segments(struct context *ctx) {
	struct kvm_sregs sregs;
	struct kvm_segment seg = {
		.base = 0,
		.limit = 0xffffffff,
		.selector = 1 << 3,
		.present = 1,
		.type = 0xb, /* Code segment */
		.dpl = 0, /* Kernel: level 0 */
		.db = 0,
		.s = 1,
		.l = 1, /* long mode */
		.g = 1
	};
        int rc;

        rc = vcpu_ioctl(ctx, KVM_GET_SREGS, &sregs);
        if (rc < 0) {
                warn("Could not get VCPU SREGS");
                goto err;
        }

	sregs.cs = seg;
	seg.type = 0x3; /* Data segment */
	seg.selector = 2 << 3;
	sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = seg;

        rc = vcpu_ioctl(ctx, KVM_SET_SREGS, &sregs);
        if (rc < 0) {
                warn("Could not set VCPU SREGS");
                goto err;
        }

        return 0;
err:
        return -1;
}

// vim:fenc=utf-8:tw=75:et
