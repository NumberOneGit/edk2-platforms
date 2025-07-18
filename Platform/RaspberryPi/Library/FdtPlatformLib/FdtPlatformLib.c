/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/FdtPlatformLib.h>
#include <Library/FdtLib.h>

VOID *
EFIAPI
FdtPlatformGetBase (
  VOID
  )
{
  VOID    *Fdt;
  INT32   FdtError;

  Fdt = (VOID *)(UINTN) PcdGet32 (PcdFdtBaseAddress);

  FdtError = FdtCheckHeader (Fdt);
  if (FdtError != 0) {
    DEBUG ((DEBUG_ERROR, "%a: Bad/missing FDT at 0x%p! Ret=%a\n",
            __func__, Fdt, FdtStrerror (FdtError)));
    ASSERT (FALSE);
    return NULL;
  }
  return Fdt;
}
