/** @file
  RP1 BAR1 MMIO helpers: read-modify-write and optional bus-fabric aliases.

  Copyright (c) 2026
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Mechanisms (not interchangeable)

  1) Normal RMW — read 32-bit register, apply mask/OR, write back to the same
     address. Used by Linux clk-rp1 for clock CTRL and by many controllers.
     Not atomic with respect to other masters; use when the block has no
     set/clear alias or when software must preserve undocumented fields.

  2) Bus-fabric set/clear aliases — Linux adds 0x2000 (set) or 0x3000 (clear)
     to the *register offset from the peripheral instance base* (same layout
     as rp1-adc, rp1-mailbox SYSCFG events, and ATF rp1_msi.h). Writing a word
     sets bits (1) or clears bits (1) in the underlying register without a
     read. Only valid where the hardware documents these aliases.

  3) PCIe MSI-X window — separate +0x800 / +0xc00 from the MSI-X register area
     base (see Rp1.h RP1_PCIE_REG_SET / RP1_PCIE_REG_CLR). Not the same as
     0x2000/0x3000.

  Requires IoLib in the module that includes this header.
**/

#ifndef __RP1_MMIO_H__
#define __RP1_MMIO_H__

#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Uefi.h>

//
// Offsets added to a normal 32-bit register address to hit bus aliases.
// (Linux: rp1-adc RP1_ADC_RWTYPE_*, rp1-mailbox HW_*_BITS, ATF RP1_ATOMIC_*.)
//
#define RP1_MMIO_BUS_ALIAS_SET_U32 0x2000u
#define RP1_MMIO_BUS_ALIAS_CLR_U32 0x3000u

/** Read 32-bit register at absolute MMIO address. */
#define Rp1MmioRead32(Address)  MmioRead32 (Address)

/** Write 32-bit register at absolute MMIO address (full replace). */
#define Rp1MmioWrite32(Address, Value) \
  do { \
    MemoryFence (); \
    MmioWrite32 ((Address), (Value)); \
  } while (0)

/** Read-modify-write: (Old & ~ClearMask) | SetMask. Returns value written. */
#define Rp1MmioRmw32(Address, ClearMask, SetMask) \
  MmioAndThenOr32 ((Address), ~((UINT32)(ClearMask)), (SetMask))

/** OR bits into a register (RMW). Returns value written. */
#define Rp1MmioOr32(Address, Bits)  MmioOr32 ((Address), (Bits))

/** Clear bits in a register (RMW). Returns value written. */
#define Rp1MmioAndNot32(Address, Bits)  MmioAnd32 ((Address), ~((UINT32)(Bits)))

/**
  Bus-fabric atomic set: ones in BitsToSet become 1 in the target register.

  @param NormalRegAddress  Address of the normal RW copy of the register.

  @note Only for blocks that implement this alias.
**/
#define Rp1MmioBusAtomicSet32(NormalRegAddress, BitsToSet) \
  do { \
    MemoryFence (); \
    MmioWrite32 ( \
      (UINTN)(NormalRegAddress) + RP1_MMIO_BUS_ALIAS_SET_U32, \
      (BitsToSet) \
      ); \
  } while (0)

/**
  Bus-fabric atomic clear: ones in BitsToClear become 0 in the target register.

  @param NormalRegAddress  Address of the normal RW copy of the register.
**/
#define Rp1MmioBusAtomicClear32(NormalRegAddress, BitsToClear) \
  do { \
    MemoryFence (); \
    MmioWrite32 ( \
      (UINTN)(NormalRegAddress) + RP1_MMIO_BUS_ALIAS_CLR_U32, \
      (BitsToClear) \
      ); \
  } while (0)

/** Full physical address: peripheral base + offset (RP1 BAR-relative). */
#define Rp1MmioAddr(PeripheralBase, Offset) \
  ((UINTN)((PeripheralBase) + (Offset)))

#endif // __RP1_MMIO_H__
