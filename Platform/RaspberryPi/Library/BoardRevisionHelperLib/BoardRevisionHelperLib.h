#ifndef BOARD_REVISION_HELPER_LIB_H
#define BOARD_REVISION_HELPER_LIB_H

#include <Uefi.h> // For UINT32, etc.

// SoC stepping values
#define BCM2712_C1 0
#define BCM2712_D0 1

/**
  Returns the SoC stepping for the given board revision code.
  @param[in]  RevisionCode  The 32-bit board revision code.
  @retval     BCM2712_C1    For Pi 5 C1 stepping (model 0x17, boardRev 0)
  @retval     BCM2712_D0    For D0 stepping (all other cases)
**/
UINT32
EFIAPI
BoardRevisionGetSoCStepping (
    IN UINT32 RevisionCode
    );

#endif // BOARD_REVISION_HELPER_LIB_H
