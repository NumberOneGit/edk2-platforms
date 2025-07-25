/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *  Copyright (c) 2020, Andrei Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2019-2020, Pete Batard <pete@akeo.ie>
 *  Copyright (c) 2016, Linaro Limited. All rights reserved.
 *  Copyright (c) 2011-2020, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <AsmMacroLib.h>
#include <Library/ArmLib.h>
#include <IndustryStandard/Bcm2836Mbox.h>
#include <IndustryStandard/RpiMbox.h>

    .macro  drain
    mov     x5, #RPI_MBOX_MAX_TRIES
0:  ldr     w6, [x4, #BCM2836_MBOX_STATUS_OFFSET]
    tbnz    w6, #BCM2836_MBOX_STATUS_EMPTY, 1f
    dmb     ld
    ldr     wzr, [x4, #BCM2836_MBOX_READ_OFFSET]
    subs    x5, x5, #1
    b.ne    0b
1:
    .endm

    .macro  poll, status
    mov     x5, #RPI_MBOX_MAX_TRIES
0:  ldr     w6, [x4, #BCM2836_MBOX_STATUS_OFFSET]
    tbz     w6, #\status, 1f
    dmb     ld
    subs    x5, x5, #1
    b.ne    0b
1:
    .endm

    .macro  run, command_buffer
    adr     x0, \command_buffer
    orr     x0, x0, #RPI_MBOX_VC_CHANNEL
    add     x0, x0, x1

    poll    BCM2836_MBOX_STATUS_FULL
    str     w0, [x4, #BCM2836_MBOX_WRITE_OFFSET]
    dmb     sy
    poll    BCM2836_MBOX_STATUS_EMPTY
    dmb     sy
    ldr     wzr, [x4, #BCM2836_MBOX_READ_OFFSET]
    dmb     ld
    .endm

ASM_FUNC (ArmPlatformPeiBootAction)
    mov     x1, #FixedPcdGet64 (PcdDmaDeviceOffset)
    orr     x0, x0, #RPI_MBOX_VC_CHANNEL
    // x1 holds the value of PcdDmaDeviceOffset throughout this function

    MOV64   (x4, FixedPcdGet64 (PcdFwMailboxBaseAddress))

    drain

    run     .Lmeminfo_buffer

    ldr     w0, .Lmembase
    adr     x2, mSystemMemoryBase
    str     x0, [x2]

    ldr     w0, .Lmemsize
    sub     x0, x0, #1
    adr     x2, mSystemMemoryEnd
    str     x0, [x2]

    ret

    .align  4
.Lmeminfo_buffer:
    .long   .Lmeminfo_size
    .long   0x0
    .long   RPI_MBOX_GET_ARM_MEMSIZE
    .long   8                           // buf size
    .long   0                           // input len
.Lmembase:
    .long   0                           // mem base
.Lmemsize:
    .long   0                           // mem size
    .long   0                           // end tag
    .set    .Lmeminfo_size, . - .Lmeminfo_buffer

ASM_FUNCTION_REMOVE_IF_UNREFERENCED
