/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __BOARD_INFO_LIB_H__
#define __BOARD_INFO_LIB_H__

typedef enum {
  UsbDrModeUnset = 0,
  UsbDrModeHost,
  UsbDrModePeripheral,
  UsbDrModeOtg
} USB_DR_MODE;

EFI_STATUS
EFIAPI
BoardInfoGetRevisionCode (
  OUT   UINT32  *RevisionCode
  );

EFI_STATUS
EFIAPI
BoardInfoGetSerialNumber (
  OUT   UINT64  *SerialNumber
  );

USB_DR_MODE
EFIAPI
BoardInfoGetUsbDrMode (
  VOID
  );

#endif /* __BOARD_INFO_LIB_H__ */
