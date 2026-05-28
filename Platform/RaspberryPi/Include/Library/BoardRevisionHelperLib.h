/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __BOARD_REVISION_HELPER_LIB_H__
#define __BOARD_REVISION_HELPER_LIB_H__

//
// BCM2712 silicon stepping. The launch Pi 5 Model B (rev 1.0) shipped with C1;
// every later Pi 5, CM5, CM5 Lite, and Pi 500 uses D0. D0 is the default so any
// failure to detect leaves the system on the layout the current code targets.
//
typedef enum {
  BCM2712_STEPPING_D0 = 0,
  BCM2712_STEPPING_C1
} BCM2712_STEPPING;

UINT8
EFIAPI
BoardRevisionGetBoardType (
  IN  UINT32  RevisionCode
  );

BCM2712_STEPPING
EFIAPI
BoardRevisionGetStepping (
  IN  UINT32  RevisionCode
  );

BOOLEAN
EFIAPI
BoardRevisionGetHasWifi (
  IN  UINT32  RevisionCode,
  IN  UINT32  ExtendedRevisionCode
  );

UINT64
EFIAPI
BoardRevisionGetMemorySize (
  IN  UINT32  RevisionCode
  );

UINT32
EFIAPI
BoardRevisionGetModelFamily (
  IN  UINT32  RevisionCode
  );

CHAR8 *
EFIAPI
BoardRevisionGetModelName (
  IN  UINT32  RevisionCode
  );

CHAR8 *
EFIAPI
BoardRevisionGetManufacturerName (
  IN  UINT32  RevisionCode
  );

CHAR8 *
EFIAPI
BoardRevisionGetProcessorName (
  IN  UINT32  RevisionCode
  );

#endif /* __BOARD_REVISION_HELPER_LIB_H__ */
