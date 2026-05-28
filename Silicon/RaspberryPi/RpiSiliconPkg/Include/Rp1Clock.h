/** @file
  RP1 clock, PLL, and reset helpers for repeated RP1 block layouts.

  @note PLL helpers that wait for lock use MicroSecondDelay(), so any module
        calling Rp1PllEnsureOn() must list TimerLib in its INF [LibraryClasses].

  @note Reset helpers are intentionally register-oriented. Do not add guessed
        reset IDs here; prefer adding named reset bits only after confirming
        them against RP1 documentation, Linux, or boot traces.

  Copyright (c) 2026
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __RP1_CLOCK_H__
#define __RP1_CLOCK_H__

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Rp1.h>
#include <Rp1Mmio.h>
#include <Uefi.h>

//
// RP1 main clock controller block offsets.
// These are offsets from RP1_CLOCKS_MAIN_BASE to each CLK_*_CTRL register.
//
#define RP1_CLK_BLOCK_SYS                0x0014
#define RP1_CLK_BLOCK_SLOW_SYS           0x0024
#define RP1_CLK_BLOCK_DMA                0x0044
#define RP1_CLK_BLOCK_UART               0x0054
#define RP1_CLK_BLOCK_ETH                0x0064
#define RP1_CLK_BLOCK_PWM0               0x0074
#define RP1_CLK_BLOCK_PWM1               0x0084
#define RP1_CLK_BLOCK_AUDIO_IN           0x0094
#define RP1_CLK_BLOCK_AUDIO_OUT          0x00A4
#define RP1_CLK_BLOCK_I2S                0x00B4
#define RP1_CLK_BLOCK_MIPI0_CFG          0x00C4
#define RP1_CLK_BLOCK_MIPI1_CFG          0x00D4
#define RP1_CLK_BLOCK_PCIE_AUX           0x00E4
#define RP1_CLK_BLOCK_USBH0_MICROFRAME   0x00F4
#define RP1_CLK_BLOCK_USBH1_MICROFRAME   0x0104
#define RP1_CLK_BLOCK_USBH0_SUSPEND      0x0114
#define RP1_CLK_BLOCK_USBH1_SUSPEND      0x0124
#define RP1_CLK_BLOCK_ETH_TSU            0x0134
#define RP1_CLK_BLOCK_ADC                0x0144
#define RP1_CLK_BLOCK_SDIO_TIMER         0x0154
#define RP1_CLK_BLOCK_SDIO_ALT_SRC       0x0164
#define RP1_CLK_BLOCK_GP0                0x0174
#define RP1_CLK_BLOCK_GP1                0x0184
#define RP1_CLK_BLOCK_GP2                0x0194
#define RP1_CLK_BLOCK_GP3                0x01A4
#define RP1_CLK_BLOCK_GP4                0x01B4
#define RP1_CLK_BLOCK_GP5                0x01C4

//
// Video clock blocks are offsets from RP1_CLOCKS_MAIN_BASE as in Linux.
//
#define RP1_CLK_BLOCK_VEC        0x4000
#define RP1_CLK_BLOCK_DPI        0x4010
#define RP1_CLK_BLOCK_MIPI0_DPI  0x4020
#define RP1_CLK_BLOCK_MIPI1_DPI  0x4030

//
// Simple clocks (ETH, ETH_TSU, UART, DMA, ...): no DIV_FRAC, SEL at +0xC.
//
#define RP1_CLK_REG_CTRL     0x0000
#define RP1_CLK_REG_DIV_INT  0x0004
#define RP1_CLK_REG_SEL      0x000C

//
// Frac-capable clocks (PWM, GP0-5, ...): DIV_FRAC +0x8, SEL remains +0xC.
//
#define RP1_CLK_FRAC_REG_DIV_FRAC  0x0008
#define RP1_CLK_FRAC_REG_SEL       RP1_CLK_REG_SEL

#define RP1_CLK_GPCLK_OE_CTRL  0x0000
#define RP1_CLK_GPCLK_OE_GP0   BIT0
#define RP1_CLK_GPCLK_OE_GP1   BIT1
#define RP1_CLK_GPCLK_OE_GP2   BIT2
#define RP1_CLK_GPCLK_OE_GP3   BIT3
#define RP1_CLK_GPCLK_OE_GP4   BIT4
#define RP1_CLK_GPCLK_OE_GP5   BIT5

#define RP1_CLK_CTRL_SRC_OFFSET     0
#define RP1_CLK_CTRL_SRC_MASK_1BIT  0x00000001
#define RP1_CLK_CTRL_SRC_MASK_2BIT  0x00000003
#define RP1_CLK_CTRL_SRC_MASK_4BIT  0x0000000F
#define RP1_CLK_CTRL_AUXSRC_OFFSET  5
#define RP1_CLK_CTRL_AUXSRC_MASK    0x000003E0
#define RP1_CLK_CTRL_KHZ            BIT8
#define RP1_CLK_CTRL_KILL           BIT10
#define RP1_CLK_CTRL_ENABLE         BIT11

#define RP1_CLK_DIV_INT_8BIT_MAX   0x000000FFu
#define RP1_CLK_DIV_INT_16BIT_MAX  0x0000FFFFu
#define RP1_CLK_DIV_INT_24BIT_MAX  0x00FFFFFFu
#define RP1_CLK_DIV_FRAC_BITS      16

#define RP1_PLL_SYS_OFFSET    0x8000
#define RP1_PLL_AUDIO_OFFSET  0xC000
#define RP1_PLL_VIDEO_OFFSET  0x10000

#define RP1_PLL_REG_CS          0x0000
#define RP1_PLL_REG_PWR         0x0004
#define RP1_PLL_REG_FBDIV_INT   0x0008
#define RP1_PLL_REG_FBDIV_FRAC  0x000C
#define RP1_PLL_REG_PRIM        0x0010
#define RP1_PLL_REG_SEC         0x0014
#define RP1_PLL_REG_TERN        0x0018

#define RP1_PLL_PRIM_DIV1_OFFSET  16
#define RP1_PLL_PRIM_DIV1_MASK    0x00070000
#define RP1_PLL_PRIM_DIV2_OFFSET  12
#define RP1_PLL_PRIM_DIV2_MASK    0x00007000

#define RP1_PLL_SEC_DIV_OFFSET  8
#define RP1_PLL_SEC_DIV_MASK    0x00001F00
#define RP1_PLL_SEC_RST         BIT16
#define RP1_PLL_SEC_IMPL        BIT31

#define RP1_PLL_CS_REFDIV_OFFSET  0
#define RP1_PLL_CS_LOCK           BIT31

#define RP1_PLL_PWR_PD         BIT0
#define RP1_PLL_PWR_DACPD      BIT1
#define RP1_PLL_PWR_DSMPD      BIT2
#define RP1_PLL_PWR_POSTDIVPD  BIT3
#define RP1_PLL_PWR_4PHASEPD   BIT4
#define RP1_PLL_PWR_VCOPD      BIT5
#define RP1_PLL_PWR_MASK       0x0000003F

#define RP1_PLL_LOCK_TIMEOUT_US  100000

typedef enum {
  Rp1ClockLayoutSimple,
  Rp1ClockLayoutFractional
} RP1_CLOCK_LAYOUT;

STATIC
inline
UINTN
Rp1ClockRegAddr (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                RegOffset
  )
{
  return (UINTN)(ClockBase + BlockOffset + RegOffset);
}

STATIC
inline
UINT32
Rp1ClockSetField (
  IN UINT32  Value,
  IN UINT32  Mask,
  IN UINTN   Offset,
  IN UINT32  FieldValue
  )
{
  return (Value & ~Mask) | ((FieldValue << Offset) & Mask);
}

STATIC
inline
UINT32
Rp1ClockSelRegOffset (
  IN RP1_CLOCK_LAYOUT  Layout
  )
{
  return (Layout == Rp1ClockLayoutFractional) ? RP1_CLK_FRAC_REG_SEL :
                                                RP1_CLK_REG_SEL;
}

STATIC
inline
UINT32
Rp1ClockRead32 (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                RegOffset
  )
{
  ASSERT ((RegOffset & 3) == 0);

  return Rp1MmioRead32 (Rp1ClockRegAddr (ClockBase, BlockOffset, RegOffset));
}

STATIC
inline
VOID
Rp1ClockWrite32 (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                RegOffset,
  IN UINT32                Value
  )
{
  ASSERT ((RegOffset & 3) == 0);

  Rp1MmioWrite32 (Rp1ClockRegAddr (ClockBase, BlockOffset, RegOffset), Value);
}

STATIC
inline
UINT32
Rp1ClockSetBits (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                RegOffset,
  IN UINT32                SetMask
  )
{
  ASSERT ((RegOffset & 3) == 0);

  return Rp1MmioOr32 (
           Rp1ClockRegAddr (ClockBase, BlockOffset, RegOffset),
           SetMask
           );
}

STATIC
inline
UINT32
Rp1ClockClearBits (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                RegOffset,
  IN UINT32                ClearMask
  )
{
  ASSERT ((RegOffset & 3) == 0);

  return Rp1MmioAndNot32 (
           Rp1ClockRegAddr (ClockBase, BlockOffset, RegOffset),
           ClearMask
           );
}

STATIC
inline
BOOLEAN
Rp1ClockIsEnabled (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset
  )
{
  return (Rp1ClockRead32 (ClockBase, BlockOffset, RP1_CLK_REG_CTRL) &
          RP1_CLK_CTRL_ENABLE) != 0;
}

STATIC
inline
VOID
Rp1ClockEnable (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset
  )
{
  Rp1ClockSetBits (ClockBase, BlockOffset, RP1_CLK_REG_CTRL, RP1_CLK_CTRL_ENABLE);
}

STATIC
inline
VOID
Rp1ClockDisable (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset
  )
{
  Rp1ClockClearBits (
    ClockBase,
    BlockOffset,
    RP1_CLK_REG_CTRL,
    RP1_CLK_CTRL_ENABLE
    );
}

STATIC
inline
VOID
Rp1ClockSetGpOutputEnable (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                GpMask,
  IN BOOLEAN               Enable
  )
{
  if (Enable) {
    Rp1MmioOr32 (Rp1ClockRegAddr (ClockBase, 0, RP1_CLK_GPCLK_OE_CTRL), GpMask);
  } else {
    Rp1MmioAndNot32 (
      Rp1ClockRegAddr (ClockBase, 0, RP1_CLK_GPCLK_OE_CTRL),
      GpMask
      );
  }
}

STATIC
inline
VOID
Rp1ClockSetIntegerDivider (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                Divider
  )
{
  Rp1ClockWrite32 (ClockBase, BlockOffset, RP1_CLK_REG_DIV_INT, Divider);
}

STATIC
inline
VOID
Rp1ClockSetFractionalDivider (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                IntegerDivider,
  IN UINT32                FractionalDivider
  )
{
  Rp1ClockWrite32 (
    ClockBase,
    BlockOffset,
    RP1_CLK_REG_DIV_INT,
    IntegerDivider
    );
  Rp1ClockWrite32 (
    ClockBase,
    BlockOffset,
    RP1_CLK_FRAC_REG_DIV_FRAC,
    FractionalDivider
    );
}

STATIC
inline
VOID
Rp1ClockSetSource (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                SourceMask,
  IN UINT32                Source
  )
{
  UINT32  Ctrl;

  Ctrl = Rp1ClockRead32 (ClockBase, BlockOffset, RP1_CLK_REG_CTRL);
  Ctrl = Rp1ClockSetField (
           Ctrl,
           SourceMask,
           RP1_CLK_CTRL_SRC_OFFSET,
           Source
           );
  Rp1ClockWrite32 (ClockBase, BlockOffset, RP1_CLK_REG_CTRL, Ctrl);
}

STATIC
inline
VOID
Rp1ClockSetAuxSource (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                SourceMask,
  IN UINT32                AuxSource
  )
{
  UINT32  Ctrl;

  Ctrl = Rp1ClockRead32 (ClockBase, BlockOffset, RP1_CLK_REG_CTRL);
  Ctrl = Rp1ClockSetField (
           Ctrl,
           RP1_CLK_CTRL_AUXSRC_MASK,
           RP1_CLK_CTRL_AUXSRC_OFFSET,
           AuxSource
           );
  Ctrl = Rp1ClockSetField (Ctrl, SourceMask, RP1_CLK_CTRL_SRC_OFFSET, 1);
  Rp1ClockWrite32 (ClockBase, BlockOffset, RP1_CLK_REG_CTRL, Ctrl);
}

STATIC
inline
VOID
Rp1ClockConfigureSimple (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                BlockOffset,
  IN UINT32                Divider,
  IN UINT32                SourceMask,
  IN UINT32                Source,
  IN BOOLEAN               Enable
  )
{
  Rp1ClockSetIntegerDivider (ClockBase, BlockOffset, Divider);
  Rp1ClockSetSource (ClockBase, BlockOffset, SourceMask, Source);

  if (Enable) {
    Rp1ClockEnable (ClockBase, BlockOffset);
  }
}

STATIC
inline
UINTN
Rp1PllRegAddr (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset,
  IN UINT32                RegOffset
  )
{
  return (UINTN)(ClockBase + PllOffset + RegOffset);
}

STATIC
inline
UINT32
Rp1PllRead32 (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset,
  IN UINT32                RegOffset
  )
{
  return Rp1MmioRead32 (Rp1PllRegAddr (ClockBase, PllOffset, RegOffset));
}

STATIC
inline
VOID
Rp1PllWrite32 (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset,
  IN UINT32                RegOffset,
  IN UINT32                Value
  )
{
  Rp1MmioWrite32 (Rp1PllRegAddr (ClockBase, PllOffset, RegOffset), Value);
}

STATIC
inline
BOOLEAN
Rp1PllIsLocked (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset
  )
{
  return (Rp1PllRead32 (ClockBase, PllOffset, RP1_PLL_REG_CS) &
          RP1_PLL_CS_LOCK) != 0;
}

STATIC
inline
VOID
Rp1PllPowerDown (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset
  )
{
  Rp1PllWrite32 (ClockBase, PllOffset, RP1_PLL_REG_PWR, RP1_PLL_PWR_MASK);
}

STATIC
inline
VOID
Rp1PllPowerUp (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset
  )
{
  UINT32  FbdivFrac;

  FbdivFrac = Rp1PllRead32 (ClockBase, PllOffset, RP1_PLL_REG_FBDIV_FRAC);
  Rp1PllWrite32 (
    ClockBase,
    PllOffset,
    RP1_PLL_REG_PWR,
    FbdivFrac != 0 ? 0 : RP1_PLL_PWR_DSMPD
    );
}

STATIC
inline
EFI_STATUS
Rp1PllWaitForLock (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset,
  IN UINT32                TimeoutUs
  )
{
  while (TimeoutUs-- > 0) {
    if (Rp1PllIsLocked (ClockBase, PllOffset)) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (1);
  }

  return EFI_TIMEOUT;
}

STATIC
inline
EFI_STATUS
Rp1PllEnsureOn (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset
  )
{
  if (!Rp1PllIsLocked (ClockBase, PllOffset)) {
    Rp1PllPowerDown (ClockBase, PllOffset);
    Rp1PllWrite32 (ClockBase, PllOffset, RP1_PLL_REG_FBDIV_INT, 20);
    Rp1PllWrite32 (ClockBase, PllOffset, RP1_PLL_REG_FBDIV_FRAC, 0);
    Rp1PllWrite32 (
      ClockBase,
      PllOffset,
      RP1_PLL_REG_CS,
      (UINT32)1u << RP1_PLL_CS_REFDIV_OFFSET
      );
  }

  Rp1PllPowerUp (ClockBase, PllOffset);
  return Rp1PllWaitForLock (ClockBase, PllOffset, RP1_PLL_LOCK_TIMEOUT_US);
}

STATIC
inline
VOID
Rp1PllSetPrimaryDividers (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset,
  IN UINT32                Div1,
  IN UINT32                Div2
  )
{
  UINT32  Prim;

  Prim = Rp1PllRead32 (ClockBase, PllOffset, RP1_PLL_REG_PRIM);
  Prim = Rp1ClockSetField (
           Prim,
           RP1_PLL_PRIM_DIV1_MASK,
           RP1_PLL_PRIM_DIV1_OFFSET,
           Div1
           );
  Prim = Rp1ClockSetField (
           Prim,
           RP1_PLL_PRIM_DIV2_MASK,
           RP1_PLL_PRIM_DIV2_OFFSET,
           Div2
           );
  Rp1PllWrite32 (ClockBase, PllOffset, RP1_PLL_REG_PRIM, Prim);
}

STATIC
inline
VOID
Rp1PllSetSecondaryDivider (
  IN EFI_PHYSICAL_ADDRESS  ClockBase,
  IN UINT32                PllOffset,
  IN UINT32                RegOffset,
  IN UINT32                Divider
  )
{
  UINT32  Sec;

  Sec = Rp1PllRead32 (ClockBase, PllOffset, RegOffset);
  Sec = Rp1ClockSetField (
          Sec | RP1_PLL_SEC_RST,
          RP1_PLL_SEC_DIV_MASK,
          RP1_PLL_SEC_DIV_OFFSET,
          Divider
          );
  Rp1PllWrite32 (ClockBase, PllOffset, RegOffset, Sec);
  Rp1PllWrite32 (ClockBase, PllOffset, RegOffset, Sec & ~RP1_PLL_SEC_RST);
}

//
// Reset block helpers.
//
// Linux exposes RP1_RESETS_BASE in the RP1 MFD binding, but this tree does not
// currently include a full RP1 reset-controller driver. Keep this conservative
// until the per-device reset bits are confirmed.
//

STATIC
inline
UINTN
Rp1ResetRegAddr (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT32                RegOffset
  )
{
  return Rp1MmioAddr (PeripheralBase, RP1_RESETS_BASE + RegOffset);
}

STATIC
inline
UINT32
Rp1ResetRead32 (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT32                RegOffset
  )
{
  return Rp1MmioRead32 (Rp1ResetRegAddr (PeripheralBase, RegOffset));
}

STATIC
inline
VOID
Rp1ResetWrite32 (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT32                RegOffset,
  IN UINT32                Value
  )
{
  Rp1MmioWrite32 (Rp1ResetRegAddr (PeripheralBase, RegOffset), Value);
}

STATIC
inline
VOID
Rp1ResetSetBits (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT32                RegOffset,
  IN UINT32                Bits
  )
{
  Rp1MmioBusAtomicSet32 (Rp1ResetRegAddr (PeripheralBase, RegOffset), Bits);
}

STATIC
inline
VOID
Rp1ResetClearBits (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT32                RegOffset,
  IN UINT32                Bits
  )
{
  Rp1MmioBusAtomicClear32 (Rp1ResetRegAddr (PeripheralBase, RegOffset), Bits);
}

#endif // __RP1_CLOCK_H__
