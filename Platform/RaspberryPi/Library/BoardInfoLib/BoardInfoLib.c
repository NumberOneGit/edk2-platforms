/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
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
