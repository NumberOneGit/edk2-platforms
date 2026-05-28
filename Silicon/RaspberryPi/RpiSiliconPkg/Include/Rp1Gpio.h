/** @file
  RP1 GPIO, RIO, and pad helper functions.

  These helpers follow the RP1 Linux pinctrl layout: GPIO0..53 are split
  across three banks, but each bank uses the same per-pin register format.

  Copyright (c) 2026
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __RP1_GPIO_H__
#define __RP1_GPIO_H__

#include <Library/BaseLib.h>
#include <Rp1.h>
#include <Rp1Mmio.h>
#include <Uefi.h>

#define RP1_GPIO_COUNT  54

#define RP1_GPIO_STATUS  0x0000
#define RP1_GPIO_CTRL    0x0004
#define RP1_GPIO_STRIDE  0x0008

#define RP1_GPIO_PCIE_INTE  0x011C
#define RP1_GPIO_PCIE_INTS  0x0124

#define RP1_GPIO_CTRL_FUNCSEL_OFFSET  0
#define RP1_GPIO_CTRL_FUNCSEL_MASK    0x0000001F
#define RP1_GPIO_CTRL_OUTOVER_OFFSET  12
#define RP1_GPIO_CTRL_OUTOVER_MASK    0x00003000
#define RP1_GPIO_CTRL_OEOVER_OFFSET   14
#define RP1_GPIO_CTRL_OEOVER_MASK     0x0000C000
#define RP1_GPIO_CTRL_INOVER_OFFSET   16
#define RP1_GPIO_CTRL_INOVER_MASK     0x00030000
#define RP1_GPIO_CTRL_IRQRESET        BIT28
#define RP1_GPIO_CTRL_IRQOVER_OFFSET  30
#define RP1_GPIO_CTRL_IRQOVER_MASK    0xC0000000

#define RP1_RIO_OUT  0x00
#define RP1_RIO_OE   0x04
#define RP1_RIO_IN   0x08

#define RP1_PAD_STRIDE              0x0004
#define RP1_PAD_SLEWFAST_MASK       0x00000001
#define RP1_PAD_SCHMITT_MASK        0x00000002
#define RP1_PAD_PULL_OFFSET         2
#define RP1_PAD_PULL_MASK           0x0000000C
#define RP1_PAD_DRIVE_OFFSET        4
#define RP1_PAD_DRIVE_MASK          0x00000030
#define RP1_PAD_INPUT_ENABLE        BIT6
#define RP1_PAD_OUTPUT_DISABLE      BIT7

typedef enum {
  Rp1GpioFunctionAlt0   = 0,
  Rp1GpioFunctionAlt1   = 1,
  Rp1GpioFunctionAlt2   = 2,
  Rp1GpioFunctionAlt3   = 3,
  Rp1GpioFunctionAlt4   = 4,
  Rp1GpioFunctionGpio   = 5,
  Rp1GpioFunctionAlt6   = 6,
  Rp1GpioFunctionAlt7   = 7,
  Rp1GpioFunctionAlt8   = 8,
  Rp1GpioFunctionNone   = 9,
  Rp1GpioFunctionNoneHw = 31
} RP1_GPIO_FUNCTION;

typedef enum {
  Rp1GpioDirectionOutput = 0,
  Rp1GpioDirectionInput  = 1
} RP1_GPIO_DIRECTION;

typedef enum {
  Rp1GpioPullNone = 0,
  Rp1GpioPullDown = 1,
  Rp1GpioPullUp   = 2
} RP1_GPIO_PULL;

typedef enum {
  Rp1GpioDrive2mA  = 0,
  Rp1GpioDrive4mA  = 1,
  Rp1GpioDrive8mA  = 2,
  Rp1GpioDrive12mA = 3
} RP1_GPIO_DRIVE_STRENGTH;

#define RP1_GPIO_OUTOVER_PERI      0
#define RP1_GPIO_OUTOVER_INVPERI   1
#define RP1_GPIO_OUTOVER_LOW       2
#define RP1_GPIO_OUTOVER_HIGH      3
#define RP1_GPIO_OEOVER_PERI       0
#define RP1_GPIO_OEOVER_INVPERI    1
#define RP1_GPIO_OEOVER_DISABLE    2
#define RP1_GPIO_OEOVER_ENABLE     3
#define RP1_GPIO_INOVER_PERI       0
#define RP1_GPIO_INOVER_INVPERI    1
#define RP1_GPIO_INOVER_LOW        2
#define RP1_GPIO_INOVER_HIGH       3

typedef struct {
  UINTN  IoBankBase;
  UINTN  RioBase;
  UINTN  PadsBase;
  UINT8  Bank;
  UINT8  BankGpio;
} RP1_GPIO_PIN;

STATIC
inline
EFI_STATUS
Rp1GpioGetPin (
  IN  EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN  UINT8                 Gpio,
  OUT RP1_GPIO_PIN          *Pin
  )
{
  if ((Pin == NULL) || (Gpio >= RP1_GPIO_COUNT)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Gpio < 28) {
    Pin->IoBankBase = Rp1MmioAddr (PeripheralBase, RP1_IO_BANK0_BASE);
    Pin->RioBase    = Rp1MmioAddr (PeripheralBase, RP1_SYS_RIO0_BASE);
    Pin->PadsBase   = Rp1MmioAddr (PeripheralBase, RP1_PADS_BANK0_BASE);
    Pin->Bank       = 0;
    Pin->BankGpio   = Gpio;
  } else if (Gpio < 34) {
    Pin->IoBankBase = Rp1MmioAddr (PeripheralBase, RP1_IO_BANK1_BASE);
    Pin->RioBase    = Rp1MmioAddr (PeripheralBase, RP1_SYS_RIO1_BASE);
    Pin->PadsBase   = Rp1MmioAddr (PeripheralBase, RP1_PADS_BANK1_BASE);
    Pin->Bank       = 1;
    Pin->BankGpio   = Gpio - 28;
  } else {
    Pin->IoBankBase = Rp1MmioAddr (PeripheralBase, RP1_IO_BANK2_BASE);
    Pin->RioBase    = Rp1MmioAddr (PeripheralBase, RP1_SYS_RIO2_BASE);
    Pin->PadsBase   = Rp1MmioAddr (PeripheralBase, RP1_PADS_BANK2_BASE);
    Pin->Bank       = 2;
    Pin->BankGpio   = Gpio - 34;
  }

  return EFI_SUCCESS;
}

STATIC
inline
UINTN
Rp1GpioRegAddr (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN UINTN               RegisterOffset
  )
{
  return Pin->IoBankBase + ((UINTN)Pin->BankGpio * RP1_GPIO_STRIDE) +
         RegisterOffset;
}

STATIC
inline
UINTN
Rp1GpioPadAddr (
  IN CONST RP1_GPIO_PIN  *Pin
  )
{
  return Pin->PadsBase + RP1_PAD_STRIDE +
         ((UINTN)Pin->BankGpio * RP1_PAD_STRIDE);
}

STATIC
inline
UINTN
Rp1GpioRioAddr (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN UINTN               RegisterOffset
  )
{
  return Pin->RioBase + RegisterOffset;
}

STATIC
inline
UINT32
Rp1GpioField (
  IN UINT32  Value,
  IN UINT32  Mask,
  IN UINTN   Offset
  )
{
  return (Value & Mask) >> Offset;
}

STATIC
inline
UINT32
Rp1GpioSetField (
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
Rp1GpioReadCtrl (
  IN CONST RP1_GPIO_PIN  *Pin
  )
{
  return Rp1MmioRead32 (Rp1GpioRegAddr (Pin, RP1_GPIO_CTRL));
}

STATIC
inline
VOID
Rp1GpioWriteCtrl (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN UINT32              Value
  )
{
  Rp1MmioWrite32 (Rp1GpioRegAddr (Pin, RP1_GPIO_CTRL), Value);
}

STATIC
inline
VOID
Rp1GpioSetPadBits (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN UINT32              ClearMask,
  IN UINT32              SetMask
  )
{
  Rp1MmioRmw32 (Rp1GpioPadAddr (Pin), ClearMask, SetMask);
}

STATIC
inline
VOID
Rp1GpioSetFunction (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN RP1_GPIO_FUNCTION   Function
  )
{
  UINT32  Ctrl;

  Ctrl = Rp1GpioReadCtrl (Pin);

  Rp1GpioSetPadBits (Pin, 0, RP1_PAD_INPUT_ENABLE);
  Rp1GpioSetPadBits (Pin, RP1_PAD_OUTPUT_DISABLE, 0);

  if (Function == Rp1GpioFunctionNone) {
    Ctrl = Rp1GpioSetField (
             Ctrl,
             RP1_GPIO_CTRL_OEOVER_MASK,
             RP1_GPIO_CTRL_OEOVER_OFFSET,
             RP1_GPIO_OEOVER_DISABLE
             );
  } else {
    Ctrl = Rp1GpioSetField (
             Ctrl,
             RP1_GPIO_CTRL_OUTOVER_MASK,
             RP1_GPIO_CTRL_OUTOVER_OFFSET,
             RP1_GPIO_OUTOVER_PERI
             );
    Ctrl = Rp1GpioSetField (
             Ctrl,
             RP1_GPIO_CTRL_OEOVER_MASK,
             RP1_GPIO_CTRL_OEOVER_OFFSET,
             RP1_GPIO_OEOVER_PERI
             );
  }

  if ((Function > Rp1GpioFunctionNone) &&
      (Function != Rp1GpioFunctionNoneHw))
  {
    Function = Rp1GpioFunctionNoneHw;
  }

  Ctrl = Rp1GpioSetField (
           Ctrl,
           RP1_GPIO_CTRL_FUNCSEL_MASK,
           RP1_GPIO_CTRL_FUNCSEL_OFFSET,
           Function
           );
  Rp1GpioWriteCtrl (Pin, Ctrl);
}

STATIC
inline
EFI_STATUS
Rp1GpioSetFunctionByNumber (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT8                 Gpio,
  IN RP1_GPIO_FUNCTION     Function
  )
{
  EFI_STATUS    Status;
  RP1_GPIO_PIN  Pin;

  Status = Rp1GpioGetPin (PeripheralBase, Gpio, &Pin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Rp1GpioSetFunction (&Pin, Function);
  return EFI_SUCCESS;
}

STATIC
inline
VOID
Rp1GpioSetDirection (
  IN CONST RP1_GPIO_PIN   *Pin,
  IN RP1_GPIO_DIRECTION   Direction
  )
{
  UINTN   OeReg;
  UINT32  Mask;

  OeReg = Rp1GpioRioAddr (Pin, RP1_RIO_OE);
  Mask  = (UINT32)1u << Pin->BankGpio;

  if (Direction == Rp1GpioDirectionInput) {
    Rp1MmioBusAtomicClear32 (OeReg, Mask);
  } else {
    Rp1MmioBusAtomicSet32 (OeReg, Mask);
  }
}

STATIC
inline
VOID
Rp1GpioWrite (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN BOOLEAN             High
  )
{
  UINTN   OutReg;
  UINT32  Mask;

  OutReg = Rp1GpioRioAddr (Pin, RP1_RIO_OUT);
  Mask   = (UINT32)1u << Pin->BankGpio;

  if (High) {
    Rp1MmioBusAtomicSet32 (OutReg, Mask);
  } else {
    Rp1MmioBusAtomicClear32 (OutReg, Mask);
  }
}

STATIC
inline
BOOLEAN
Rp1GpioRead (
  IN CONST RP1_GPIO_PIN  *Pin
  )
{
  return (Rp1MmioRead32 (Rp1GpioRioAddr (Pin, RP1_RIO_IN)) &
          ((UINT32)1u << Pin->BankGpio)) != 0;
}

STATIC
inline
VOID
Rp1GpioConfigureOutput (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN BOOLEAN             InitialHigh
  )
{
  Rp1GpioWrite (Pin, InitialHigh);
  Rp1GpioSetDirection (Pin, Rp1GpioDirectionOutput);
  Rp1GpioSetFunction (Pin, Rp1GpioFunctionGpio);
}

STATIC
inline
EFI_STATUS
Rp1GpioConfigureOutputByNumber (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT8                 Gpio,
  IN BOOLEAN               InitialHigh
  )
{
  EFI_STATUS    Status;
  RP1_GPIO_PIN  Pin;

  Status = Rp1GpioGetPin (PeripheralBase, Gpio, &Pin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Rp1GpioConfigureOutput (&Pin, InitialHigh);
  return EFI_SUCCESS;
}

STATIC
inline
VOID
Rp1GpioConfigureInput (
  IN CONST RP1_GPIO_PIN  *Pin
  )
{
  Rp1GpioSetDirection (Pin, Rp1GpioDirectionInput);
  Rp1GpioSetFunction (Pin, Rp1GpioFunctionGpio);
}

STATIC
inline
EFI_STATUS
Rp1GpioConfigureInputByNumber (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase,
  IN UINT8                 Gpio
  )
{
  EFI_STATUS    Status;
  RP1_GPIO_PIN  Pin;

  Status = Rp1GpioGetPin (PeripheralBase, Gpio, &Pin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Rp1GpioConfigureInput (&Pin);
  return EFI_SUCCESS;
}

STATIC
inline
VOID
Rp1PadSetPull (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN RP1_GPIO_PULL       Pull
  )
{
  Rp1GpioSetPadBits (
    Pin,
    RP1_PAD_PULL_MASK,
    ((UINT32)Pull << RP1_PAD_PULL_OFFSET) & RP1_PAD_PULL_MASK
    );
}

STATIC
inline
VOID
Rp1PadSetDriveStrength (
  IN CONST RP1_GPIO_PIN           *Pin,
  IN RP1_GPIO_DRIVE_STRENGTH     DriveStrength
  )
{
  Rp1GpioSetPadBits (
    Pin,
    RP1_PAD_DRIVE_MASK,
    ((UINT32)DriveStrength << RP1_PAD_DRIVE_OFFSET) & RP1_PAD_DRIVE_MASK
    );
}

STATIC
inline
VOID
Rp1PadSetInputEnable (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN BOOLEAN             Enable
  )
{
  Rp1GpioSetPadBits (
    Pin,
    RP1_PAD_INPUT_ENABLE,
    Enable ? RP1_PAD_INPUT_ENABLE : 0
    );
}

STATIC
inline
VOID
Rp1PadSetOutputEnable (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN BOOLEAN             Enable
  )
{
  Rp1GpioSetPadBits (
    Pin,
    RP1_PAD_OUTPUT_DISABLE,
    Enable ? 0 : RP1_PAD_OUTPUT_DISABLE
    );
}

STATIC
inline
VOID
Rp1PadSetSchmitt (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN BOOLEAN             Enable
  )
{
  Rp1GpioSetPadBits (
    Pin,
    RP1_PAD_SCHMITT_MASK,
    Enable ? RP1_PAD_SCHMITT_MASK : 0
    );
}

STATIC
inline
VOID
Rp1PadSetSlewFast (
  IN CONST RP1_GPIO_PIN  *Pin,
  IN BOOLEAN             Enable
  )
{
  Rp1GpioSetPadBits (
    Pin,
    RP1_PAD_SLEWFAST_MASK,
    Enable ? RP1_PAD_SLEWFAST_MASK : 0
    );
}

#endif // __RP1_GPIO_H__
