/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BoardInfoLib.h>
#include <Library/FdtPlatformLib.h>
#include <Library/FdtLib.h>

EFI_STATUS
EFIAPI
BoardInfoGetRevisionCode (
  OUT   UINT32  *RevisionCode
  )
{
  VOID            *Fdt;
  INT32           Node;
  CONST VOID      *Property;
  INT32           Length;

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return EFI_NOT_FOUND;
  }

  Node = FdtPathOffset (Fdt, "/system");
  if (Node < 0) {
    return EFI_NOT_FOUND;
  }

  Property = FdtGetProp (Fdt, Node, "linux,revision", &Length);
  if (Property == NULL) {
    return EFI_NOT_FOUND;
  } else if (Length != sizeof (UINT32)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *RevisionCode = Fdt32ToCpu (*(UINT32 *) Property);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BoardInfoGetSerialNumber (
  OUT   UINT64  *SerialNumber
  )
{
  VOID            *Fdt;
  INT32           Node;
  CONST VOID      *Property;
  INT32           Length;

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return EFI_NOT_FOUND;
  }

  Node = FdtPathOffset (Fdt, "/system");
  if (Node < 0) {
    return EFI_NOT_FOUND;
  }

  Property = FdtGetProp (Fdt, Node, "linux,serial", &Length);
  if (Property == NULL) {
    return EFI_NOT_FOUND;
  } else if (Length != sizeof (UINT64)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *SerialNumber = Fdt64ToCpu (*(UINT64 *) Property);

  return EFI_SUCCESS;
}

USB_DR_MODE
EFIAPI
BoardInfoGetUsbDrMode (
  VOID
  )
{
  VOID         *Fdt;
  INT32         Node;
  CONST CHAR8  *Mode;
  INT32         Length;

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return UsbDrModeUnset;
  }

  for (Node = FdtNextNode (Fdt, -1, NULL);
       Node >= 0;
       Node = FdtNextNode (Fdt, Node, NULL)) {
    Mode = FdtGetProp (Fdt, Node, "dr_mode", &Length);
    if ((Mode == NULL) || (Length <= 0)) {
      continue;
    }
    if (AsciiStrCmp (Mode, "host") == 0) {
      return UsbDrModeHost;
    }
    if (AsciiStrCmp (Mode, "peripheral") == 0) {
      return UsbDrModePeripheral;
    }
    if (AsciiStrCmp (Mode, "otg") == 0) {
      return UsbDrModeOtg;
    }
    break;
  }

  return UsbDrModeUnset;
}
