/*
 * ioring.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

.pushsection .data.enarx_ioring, "aw"

        .balign 4096
        .globl enarx_input_ring__
enarx_input_ring__:
        .skip 8192, 0
        .size enarx_input_ring__, 8192

        .balign 4096
        .globl enarx_output_ring__
enarx_output_ring__:
        .skip 8192, 0
        .size enarx_output_ring__, 8192

        .balign 8
        .globl enarx_input_ring_p__
enarx_input_ring_p__:
        .quad enarx_input_ring__
        .size enarx_input_ring_p__,.-enarx_input_ring_p__

        .balign 8
        .globl enarx_output_ring_p__
enarx_output_ring_p__:
        .quad enarx_output_ring__
        .size enarx_output_ring_p__,.-enarx_output_ring_p__

.popsection
