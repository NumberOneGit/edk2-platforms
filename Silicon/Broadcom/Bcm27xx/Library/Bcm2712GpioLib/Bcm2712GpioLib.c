/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/Bcm2712.h>
#include <Library/Bcm2712GpioLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>

#define BCM2712_GIO_DATA_REG                0x4
#define BCM2712_GIO_IODIR_REG               0x8

#define BCM2712_GIO_BANK_SIZE               (8 * sizeof (UINT32))
#define BCM2712_GIO_MAX_PINS_PER_BANK       32

#define BCM2712_GIO_BANK_OFFSET(Pin)        ((Pin / BCM2712_GIO_MAX_PINS_PER_BANK) * BCM2712_GIO_BANK_SIZE)
#define BCM2712_GIO_REG_BIT(Pin)            (1 << Pin)

#define BCM2712_PINCTRL_FSEL_MASK           (BIT3 | BIT2 | BIT1 | BIT0)
#define BCM2712_PINCTRL_PULL_MASK           (BIT1 | BIT0)

#define PINCTRL_REG_UNUSED                  MAX_UINT16

//
// Raw PCD value for the BCM2712 C1 stepping. Kept as a local literal so this
// silicon-package lib does not include the platform-level
// BoardRevisionHelperLib header. Must stay in sync with BCM2712_STEPPING_C1.
//
#define BCM2712_STEPPING_C1_VALUE           1

typedef struct {
  UINT16   MuxReg;
  UINT8    MuxBit;
  UINT16   CtlReg;
  UINT8    CtlBit;
} BCM2712_PINCTRL_REGISTERS;

typedef struct {
  EFI_PHYSICAL_ADDRESS         GioBase;
  EFI_PHYSICAL_ADDRESS         PinctrlBase;
  BCM2712_PINCTRL_REGISTERS    *PinctrlRegisters;
  UINT32                       PinCount;
} BCM2712_GPIO_CONTROLLER;

//
// D0 stepping: main GIO bank.
// Applies to Pi 5 Model B rev 1.1+, CM5, CM5 Lite, Pi 500.
//
STATIC BCM2712_PINCTRL_REGISTERS Bcm2712PinctrlGioRegistersD0[] = {
  [0] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [1] = { .MuxReg = 0x0, .MuxBit = 0, .CtlReg = 0x10, .CtlBit = 10 },
  [2] = { .MuxReg = 0x0, .MuxBit = 4, .CtlReg = 0x10, .CtlBit = 12 },
  [3] = { .MuxReg = 0x0, .MuxBit = 8, .CtlReg = 0x10, .CtlBit = 14 },
  [4] = { .MuxReg = 0x0, .MuxBit = 12, .CtlReg = 0x10, .CtlBit = 16 },
  [5] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [6] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [7] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [8] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [9] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [10] = { .MuxReg = 0x0, .MuxBit = 16, .CtlReg = 0x10, .CtlBit = 18 },
  [11] = { .MuxReg = 0x0, .MuxBit = 20, .CtlReg = 0x10, .CtlBit = 20 },
  [12] = { .MuxReg = 0x0, .MuxBit = 24, .CtlReg = 0x10, .CtlBit = 22 },
  [13] = { .MuxReg = 0x0, .MuxBit = 28, .CtlReg = 0x10, .CtlBit = 24 },
  [14] = { .MuxReg = 0x4, .MuxBit = 0, .CtlReg = 0x10, .CtlBit = 26 },
  [15] = { .MuxReg = 0x4, .MuxBit = 4, .CtlReg = 0x10, .CtlBit = 28 },
  [16] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [17] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [18] = { .MuxReg = 0x4, .MuxBit = 8, .CtlReg = 0x14, .CtlBit = 0 },
  [19] = { .MuxReg = 0x4, .MuxBit = 12, .CtlReg = 0x14, .CtlBit = 2 },
  [20] = { .MuxReg = 0x4, .MuxBit = 16, .CtlReg = 0x14, .CtlBit = 4 },
  [21] = { .MuxReg = 0x4, .MuxBit = 20, .CtlReg = 0x14, .CtlBit = 6 },
  [22] = { .MuxReg = 0x4, .MuxBit = 24, .CtlReg = 0x14, .CtlBit = 8 },
  [23] = { .MuxReg = 0x4, .MuxBit = 28, .CtlReg = 0x14, .CtlBit = 10 },
  [24] = { .MuxReg = 0x8, .MuxBit = 0, .CtlReg = 0x14, .CtlBit = 12 },
  [25] = { .MuxReg = 0x8, .MuxBit = 4, .CtlReg = 0x14, .CtlBit = 14 },
  [26] = { .MuxReg = 0x8, .MuxBit = 8, .CtlReg = 0x14, .CtlBit = 16 },
  [27] = { .MuxReg = 0x8, .MuxBit = 12, .CtlReg = 0x14, .CtlBit = 18 },
  [28] = { .MuxReg = 0x8, .MuxBit = 16, .CtlReg = 0x14, .CtlBit = 20 },
  [29] = { .MuxReg = 0x8, .MuxBit = 20, .CtlReg = 0x14, .CtlBit = 22 },
  [30] = { .MuxReg = 0x8, .MuxBit = 24, .CtlReg = 0x14, .CtlBit = 24 },
  [31] = { .MuxReg = 0x8, .MuxBit = 28, .CtlReg = 0x14, .CtlBit = 26 },
  [32] = { .MuxReg = 0xC, .MuxBit = 0, .CtlReg = 0x14, .CtlBit = 28 },
  [33] = { .MuxReg = 0xC, .MuxBit = 4, .CtlReg = 0x18, .CtlBit = 0 },
  [34] = { .MuxReg = 0xC, .MuxBit = 8, .CtlReg = 0x18, .CtlBit = 2 },
  [35] = { .MuxReg = 0xC, .MuxBit = 12, .CtlReg = 0x18, .CtlBit = 4 },
  [36] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 6 },
  [37] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 8 },
  [38] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 10 },
  [39] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 12 },
  [40] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 14 },
  [41] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 16 },
  [42] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 18 },
  [43] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 20 },
  [44] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 22 },
  [45] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 24 },
  [46] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 26 },
};

//
// D0 stepping: AON GIO bank.
//
STATIC BCM2712_PINCTRL_REGISTERS Bcm2712PinctrlGioAonRegistersD0[] = {
  [0] = { .MuxReg = 0xC, .MuxBit = 0, .CtlReg = 0x14, .CtlBit = 18 },
  [1] = { .MuxReg = 0xC, .MuxBit = 4, .CtlReg = 0x14, .CtlBit = 20 },
  [2] = { .MuxReg = 0xC, .MuxBit = 8, .CtlReg = 0x14, .CtlBit = 22 },
  [3] = { .MuxReg = 0xC, .MuxBit = 12, .CtlReg = 0x14, .CtlBit = 24 },
  [4] = { .MuxReg = 0xC, .MuxBit = 16, .CtlReg = 0x14, .CtlBit = 26 },
  [5] = { .MuxReg = 0xC, .MuxBit = 20, .CtlReg = 0x14, .CtlBit = 28 },
  [6] = { .MuxReg = 0xC, .MuxBit = 24, .CtlReg = 0x18, .CtlBit = 0 },
  [7] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [8] = { .MuxReg = 0xC, .MuxBit = 28, .CtlReg = 0x18, .CtlBit = 2 },
  [9] = { .MuxReg = 0x10, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 4 },
  [10] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [11] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [12] = { .MuxReg = 0x10, .MuxBit = 4, .CtlReg = 0x18, .CtlBit = 6 },
  [13] = { .MuxReg = 0x10, .MuxBit = 8, .CtlReg = 0x18, .CtlBit = 8 },
  [14] = { .MuxReg = 0x10, .MuxBit = 12, .CtlReg = 0x18, .CtlBit = 10 },
  [15] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [16] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [17] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [18] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [19] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [20] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [21] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [22] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [23] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [24] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [25] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [26] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [27] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [28] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [29] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [30] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [31] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [32] = { .MuxReg = 0x0, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [33] = { .MuxReg = 0x0, .MuxBit = 4, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [34] = { .MuxReg = 0x0, .MuxBit = 8, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [35] = { .MuxReg = 0x0, .MuxBit = 12, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [36] = { .MuxReg = 0x4, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [37] = { .MuxReg = 0x8, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
};

//
// C1 stepping: main GIO bank.
// Applies only to the launch Pi 5 Model B rev 1.0. Mux registers are 4 bits
// per pin (vs 2-bit packed on D0) and ctl registers are 2 bits per pin.
//
STATIC BCM2712_PINCTRL_REGISTERS Bcm2712PinctrlGioRegistersC1[] = {
  [0] = { .MuxReg = 0x0, .MuxBit = 0, .CtlReg = 0x1C, .CtlBit = 14 },
  [1] = { .MuxReg = 0x0, .MuxBit = 4, .CtlReg = 0x1C, .CtlBit = 16 },
  [2] = { .MuxReg = 0x0, .MuxBit = 8, .CtlReg = 0x1C, .CtlBit = 18 },
  [3] = { .MuxReg = 0x0, .MuxBit = 12, .CtlReg = 0x1C, .CtlBit = 20 },
  [4] = { .MuxReg = 0x0, .MuxBit = 16, .CtlReg = 0x1C, .CtlBit = 22 },
  [5] = { .MuxReg = 0x0, .MuxBit = 20, .CtlReg = 0x1C, .CtlBit = 24 },
  [6] = { .MuxReg = 0x0, .MuxBit = 24, .CtlReg = 0x1C, .CtlBit = 26 },
  [7] = { .MuxReg = 0x0, .MuxBit = 28, .CtlReg = 0x1C, .CtlBit = 28 },
  [8] = { .MuxReg = 0x4, .MuxBit = 0, .CtlReg = 0x20, .CtlBit = 0 },
  [9] = { .MuxReg = 0x4, .MuxBit = 4, .CtlReg = 0x20, .CtlBit = 2 },
  [10] = { .MuxReg = 0x4, .MuxBit = 8, .CtlReg = 0x20, .CtlBit = 4 },
  [11] = { .MuxReg = 0x4, .MuxBit = 12, .CtlReg = 0x20, .CtlBit = 6 },
  [12] = { .MuxReg = 0x4, .MuxBit = 16, .CtlReg = 0x20, .CtlBit = 8 },
  [13] = { .MuxReg = 0x4, .MuxBit = 20, .CtlReg = 0x20, .CtlBit = 10 },
  [14] = { .MuxReg = 0x4, .MuxBit = 24, .CtlReg = 0x20, .CtlBit = 12 },
  [15] = { .MuxReg = 0x4, .MuxBit = 28, .CtlReg = 0x20, .CtlBit = 14 },
  [16] = { .MuxReg = 0x8, .MuxBit = 0, .CtlReg = 0x20, .CtlBit = 16 },
  [17] = { .MuxReg = 0x8, .MuxBit = 4, .CtlReg = 0x20, .CtlBit = 18 },
  [18] = { .MuxReg = 0x8, .MuxBit = 8, .CtlReg = 0x20, .CtlBit = 20 },
  [19] = { .MuxReg = 0x8, .MuxBit = 12, .CtlReg = 0x20, .CtlBit = 22 },
  [20] = { .MuxReg = 0x8, .MuxBit = 16, .CtlReg = 0x20, .CtlBit = 24 },
  [21] = { .MuxReg = 0x8, .MuxBit = 20, .CtlReg = 0x20, .CtlBit = 26 },
  [22] = { .MuxReg = 0x8, .MuxBit = 24, .CtlReg = 0x20, .CtlBit = 28 },
  [23] = { .MuxReg = 0x8, .MuxBit = 28, .CtlReg = 0x24, .CtlBit = 0 },
  [24] = { .MuxReg = 0xC, .MuxBit = 0, .CtlReg = 0x24, .CtlBit = 2 },
  [25] = { .MuxReg = 0xC, .MuxBit = 4, .CtlReg = 0x24, .CtlBit = 4 },
  [26] = { .MuxReg = 0xC, .MuxBit = 8, .CtlReg = 0x24, .CtlBit = 6 },
  [27] = { .MuxReg = 0xC, .MuxBit = 12, .CtlReg = 0x24, .CtlBit = 8 },
  [28] = { .MuxReg = 0xC, .MuxBit = 16, .CtlReg = 0x24, .CtlBit = 10 },
  [29] = { .MuxReg = 0xC, .MuxBit = 20, .CtlReg = 0x24, .CtlBit = 12 },
  [30] = { .MuxReg = 0xC, .MuxBit = 24, .CtlReg = 0x24, .CtlBit = 14 },
  [31] = { .MuxReg = 0xC, .MuxBit = 28, .CtlReg = 0x24, .CtlBit = 16 },
  [32] = { .MuxReg = 0x10, .MuxBit = 0, .CtlReg = 0x24, .CtlBit = 18 },
  [33] = { .MuxReg = 0x10, .MuxBit = 4, .CtlReg = 0x24, .CtlBit = 20 },
  [34] = { .MuxReg = 0x10, .MuxBit = 8, .CtlReg = 0x24, .CtlBit = 22 },
  [35] = { .MuxReg = 0x10, .MuxBit = 12, .CtlReg = 0x24, .CtlBit = 24 },
  [36] = { .MuxReg = 0x10, .MuxBit = 16, .CtlReg = 0x24, .CtlBit = 26 },
  [37] = { .MuxReg = 0x10, .MuxBit = 20, .CtlReg = 0x24, .CtlBit = 28 },
  [38] = { .MuxReg = 0x10, .MuxBit = 24, .CtlReg = 0x28, .CtlBit = 0 },
  [39] = { .MuxReg = 0x10, .MuxBit = 28, .CtlReg = 0x28, .CtlBit = 2 },
  [40] = { .MuxReg = 0x14, .MuxBit = 0, .CtlReg = 0x28, .CtlBit = 4 },
  [41] = { .MuxReg = 0x14, .MuxBit = 4, .CtlReg = 0x28, .CtlBit = 6 },
  [42] = { .MuxReg = 0x14, .MuxBit = 8, .CtlReg = 0x28, .CtlBit = 8 },
  [43] = { .MuxReg = 0x14, .MuxBit = 12, .CtlReg = 0x28, .CtlBit = 10 },
  [44] = { .MuxReg = 0x14, .MuxBit = 16, .CtlReg = 0x28, .CtlBit = 12 },
  [45] = { .MuxReg = 0x14, .MuxBit = 20, .CtlReg = 0x28, .CtlBit = 14 },
  [46] = { .MuxReg = 0x14, .MuxBit = 24, .CtlReg = 0x28, .CtlBit = 16 },
  [47] = { .MuxReg = 0x14, .MuxBit = 28, .CtlReg = 0x28, .CtlBit = 18 },
  [48] = { .MuxReg = 0x18, .MuxBit = 0, .CtlReg = 0x28, .CtlBit = 20 },
  [49] = { .MuxReg = 0x18, .MuxBit = 4, .CtlReg = 0x28, .CtlBit = 22 },
  [50] = { .MuxReg = 0x18, .MuxBit = 8, .CtlReg = 0x28, .CtlBit = 24 },
  [51] = { .MuxReg = 0x18, .MuxBit = 12, .CtlReg = 0x28, .CtlBit = 26 },
  [52] = { .MuxReg = 0x18, .MuxBit = 16, .CtlReg = 0x28, .CtlBit = 28 },
  [53] = { .MuxReg = 0x18, .MuxBit = 20, .CtlReg = 0x2C, .CtlBit = 0 },
  [54] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 2 },
  [55] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 4 },
  [56] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 6 },
  [57] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 8 },
  [58] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 10 },
  [59] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 12 },
  [60] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 14 },
  [61] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 16 },
  [62] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 18 },
  [63] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 20 },
  [64] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x2C, .CtlBit = 22 },
};

//
// C1 stepping: AON GIO bank.
//
STATIC BCM2712_PINCTRL_REGISTERS Bcm2712PinctrlGioAonRegistersC1[] = {
  [0] = { .MuxReg = 0xC, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 20 },
  [1] = { .MuxReg = 0xC, .MuxBit = 4, .CtlReg = 0x18, .CtlBit = 22 },
  [2] = { .MuxReg = 0xC, .MuxBit = 8, .CtlReg = 0x18, .CtlBit = 24 },
  [3] = { .MuxReg = 0xC, .MuxBit = 12, .CtlReg = 0x18, .CtlBit = 26 },
  [4] = { .MuxReg = 0xC, .MuxBit = 16, .CtlReg = 0x18, .CtlBit = 28 },
  [5] = { .MuxReg = 0xC, .MuxBit = 20, .CtlReg = 0x1C, .CtlBit = 0 },
  [6] = { .MuxReg = 0xC, .MuxBit = 24, .CtlReg = 0x1C, .CtlBit = 2 },
  [7] = { .MuxReg = 0xC, .MuxBit = 28, .CtlReg = 0x1C, .CtlBit = 4 },
  [8] = { .MuxReg = 0x10, .MuxBit = 0, .CtlReg = 0x1C, .CtlBit = 6 },
  [9] = { .MuxReg = 0x10, .MuxBit = 4, .CtlReg = 0x1C, .CtlBit = 8 },
  [10] = { .MuxReg = 0x10, .MuxBit = 8, .CtlReg = 0x1C, .CtlBit = 10 },
  [11] = { .MuxReg = 0x10, .MuxBit = 12, .CtlReg = 0x1C, .CtlBit = 12 },
  [12] = { .MuxReg = 0x10, .MuxBit = 16, .CtlReg = 0x1C, .CtlBit = 14 },
  [13] = { .MuxReg = 0x10, .MuxBit = 20, .CtlReg = 0x1C, .CtlBit = 16 },
  [14] = { .MuxReg = 0x10, .MuxBit = 24, .CtlReg = 0x1C, .CtlBit = 18 },
  [15] = { .MuxReg = 0x10, .MuxBit = 28, .CtlReg = 0x1C, .CtlBit = 20 },
  [16] = { .MuxReg = 0x14, .MuxBit = 0, .CtlReg = 0x1C, .CtlBit = 22 },
  [17] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [18] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [19] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [20] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [21] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [22] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [23] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [24] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [25] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [26] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [27] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [28] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [29] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [30] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [31] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [32] = { .MuxReg = 0x0, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [33] = { .MuxReg = 0x0, .MuxBit = 4, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [34] = { .MuxReg = 0x0, .MuxBit = 8, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [35] = { .MuxReg = 0x0, .MuxBit = 12, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [36] = { .MuxReg = 0x4, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [37] = { .MuxReg = 0x8, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
};

STATIC BCM2712_GPIO_CONTROLLER ControllersD0[BCM2712_GIO_COUNT] = {
  {
    .GioBase            = BCM2712_BRCMSTB_GIO_BASE,
    .PinctrlBase        = BCM2712_PINCTRL_BASE,
    .PinctrlRegisters   = Bcm2712PinctrlGioRegistersD0,
    .PinCount           = ARRAY_SIZE (Bcm2712PinctrlGioRegistersD0)
  }, {
    .GioBase            = BCM2712_BRCMSTB_GIO_AON_BASE,
    .PinctrlBase        = BCM2712_PINCTRL_AON_BASE,
    .PinctrlRegisters   = Bcm2712PinctrlGioAonRegistersD0,
    .PinCount           = ARRAY_SIZE (Bcm2712PinctrlGioAonRegistersD0)
  }
};

STATIC BCM2712_GPIO_CONTROLLER ControllersC1[BCM2712_GIO_COUNT] = {
  {
    .GioBase            = BCM2712_BRCMSTB_GIO_BASE,
    .PinctrlBase        = BCM2712_PINCTRL_BASE,
    .PinctrlRegisters   = Bcm2712PinctrlGioRegistersC1,
    .PinCount           = ARRAY_SIZE (Bcm2712PinctrlGioRegistersC1)
  }, {
    .GioBase            = BCM2712_BRCMSTB_GIO_AON_BASE,
    .PinctrlBase        = BCM2712_PINCTRL_AON_BASE,
    .PinctrlRegisters   = Bcm2712PinctrlGioAonRegistersC1,
    .PinCount           = ARRAY_SIZE (Bcm2712PinctrlGioAonRegistersC1)
  }
};

STATIC
BCM2712_GPIO_CONTROLLER *
GetControllers (
  VOID
  )
{
  if (PcdGet8 (PcdBcm2712Stepping) == BCM2712_STEPPING_C1_VALUE) {
    return ControllersC1;
  }
  return ControllersD0;
}

#define GPIOLIB_ASSERT_OR_FAIL(Expression, FailAction) \
  do { \
    ASSERT(Expression); \
    if (!(Expression)) { \
      FailAction; \
    } \
  } while (FALSE)

#define GPIOLIB_ASSERT_COMMON_PARAMS(ControllerTable, Type, Pin, FailAction) \
  GPIOLIB_ASSERT_OR_FAIL ((Type < BCM2712_GIO_COUNT) \
                          && (Pin < (ControllerTable)[Type].PinCount), \
                          FailAction)

UINT8
EFIAPI
GpioGetFunction (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;
  UINT32                      Value = BCM2712_GPIO_ALT_COUNT;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return Value);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Regs->MuxReg != PINCTRL_REG_UNUSED, return Value);

  Value = MmioRead32 (Controller->PinctrlBase + Regs->MuxReg);

  return (Value >> Regs->MuxBit) & BCM2712_PINCTRL_FSEL_MASK;
}

VOID
EFIAPI
GpioSetFunction (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  UINT8                           Function
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Regs->MuxReg != PINCTRL_REG_UNUSED, return);

  MmioAndThenOr32 (Controller->PinctrlBase + Regs->MuxReg,
                   ~(BCM2712_PINCTRL_FSEL_MASK << Regs->MuxBit),
                   Function << Regs->MuxBit);
}

BCM2712_GPIO_PIN_PULL
EFIAPI
GpioGetPull (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;
  UINT32                      Value = BCM2712_GPIO_PIN_PULL_NONE;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return Value);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Regs->CtlReg != PINCTRL_REG_UNUSED, return Value);

  Value = MmioRead32 (Controller->PinctrlBase + Regs->CtlReg);

  return (Value >> Regs->CtlBit) & BCM2712_PINCTRL_PULL_MASK;
}

VOID
EFIAPI
GpioSetPull (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BCM2712_GPIO_PIN_PULL           Pull
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Pull == BCM2712_GPIO_PIN_PULL_NONE
                          || Pull == BCM2712_GPIO_PIN_PULL_UP
                          || Pull == BCM2712_GPIO_PIN_PULL_DOWN,
                          return);

  GPIOLIB_ASSERT_OR_FAIL (Regs->CtlReg != PINCTRL_REG_UNUSED, return);

  MmioAndThenOr32 (Controller->PinctrlBase + Regs->CtlReg,
                   ~(BCM2712_PINCTRL_PULL_MASK << Regs->CtlBit),
                   Pull << Regs->CtlBit);
}

BOOLEAN
EFIAPI
GpioRead (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;
  UINT32                      Value = FALSE;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return Value);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  Value = MmioRead32 (BankReg + BCM2712_GIO_DATA_REG);

  return (Value & BCM2712_GIO_REG_BIT (Pin)) != 0;
}

VOID
EFIAPI
GpioWrite (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BOOLEAN                         Value
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  if (Value) {
    MmioOr32 (BankReg + BCM2712_GIO_DATA_REG, BCM2712_GIO_REG_BIT (Pin));
  } else {
    MmioAnd32 (BankReg + BCM2712_GIO_DATA_REG, ~BCM2712_GIO_REG_BIT (Pin));
  }
}

BCM2712_GPIO_PIN_DIRECTION
EFIAPI
GpioGetDirection (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;
  UINT32                      Value = BCM2712_GPIO_PIN_OUTPUT;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return Value);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  Value = MmioRead32 (BankReg + BCM2712_GIO_IODIR_REG);

  if (Value & BCM2712_GIO_REG_BIT (Pin)) {
    return BCM2712_GPIO_PIN_INPUT;
  } else {
    return BCM2712_GPIO_PIN_OUTPUT;
  }
}

VOID
EFIAPI
GpioSetDirection (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BCM2712_GPIO_PIN_DIRECTION      Direction
  )
{
  BCM2712_GPIO_CONTROLLER     *Controllers;
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;

  Controllers = GetControllers ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Controllers, Type, Pin, return);

  GPIOLIB_ASSERT_OR_FAIL (Direction != BCM2712_GPIO_PIN_OUTPUT
                          || Direction != BCM2712_GPIO_PIN_INPUT,
                          return);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  switch (Direction) {
    case BCM2712_GPIO_PIN_INPUT:
      MmioOr32 (BankReg + BCM2712_GIO_IODIR_REG, BCM2712_GIO_REG_BIT (Pin));
      break;
    case BCM2712_GPIO_PIN_OUTPUT:
      MmioAnd32 (BankReg + BCM2712_GIO_IODIR_REG, ~BCM2712_GIO_REG_BIT (Pin));
      break;
    default:
     break;
  }
}
