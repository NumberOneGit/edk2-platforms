/** @file
 *
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Guid/RpiPlatformFormSetGuid.h>
#include <Library/BoardInfoLib.h>
#include <Library/BoardRevisionHelperLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "ConfigTable.h"
#include "Peripherals.h"

UINT32 mBoardRevisionCode;
UINT64 mSystemMemorySize;

extern UINT8 RpiPlatformDxeHiiBin[];
extern UINT8 RpiPlatformDxeStrings[];

typedef struct {
  VENDOR_DEVICE_PATH VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL End;
} HII_VENDOR_DEVICE_PATH;

STATIC HII_VENDOR_DEVICE_PATH mVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    RPI_PLATFORM_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};

STATIC
EFI_STATUS
EFIAPI
InstallHiiPages (
  VOID
  )
{
  EFI_STATUS        Status;
  EFI_HII_HANDLE    HiiHandle;
  EFI_HANDLE        DriverHandle;

  DriverHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&DriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mVendorDevicePath,
                  NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HiiHandle = HiiAddPackages (&gRpiPlatformFormSetGuid,
                DriverHandle,
                RpiPlatformDxeStrings,
                RpiPlatformDxeHiiBin,
                NULL);

  if (HiiHandle == NULL) {
    gBS->UninstallMultipleProtocolInterfaces (DriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mVendorDevicePath,
           NULL);
    return EFI_OUT_OF_RESOURCES;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
SetupVariables (
  VOID
  )
{
  SetupConfigTableVariables ();
  SetupPeripheralVariables ();
}

STATIC
VOID
EFIAPI
ApplyVariables (
  VOID
  )
{
  ApplyConfigTableVariables ();
  ApplyPeripheralVariables ();
}

EFI_STATUS
EFIAPI
RpiPlatformDxeEntryPoint (
  IN  EFI_HANDLE          ImageHandle,
  IN  EFI_SYSTEM_TABLE    *SystemTable
  )
{
  EFI_STATUS Status;

  Status = BoardInfoGetRevisionCode (&mBoardRevisionCode);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get board revision. Status=%r\n",
            __func__, Status));
    ASSERT (FALSE);
  }

  //
  // Publish board identity to Silicon-level consumers (pinctrl, GemDxe, ACPI
  // fixups, ...). PcdBoardHasWifi stays at its default until the firmware
  // protocol exposes an extended-revision (OTP) read; absence of evidence
  // means "assume wifi present" so no hardware is silently disabled.
  //
  PcdSet8S (PcdBoardType,       BoardRevisionGetBoardType (mBoardRevisionCode));
  PcdSet8S (PcdBcm2712Stepping, BoardRevisionGetStepping  (mBoardRevisionCode));

  //
  // Producer-side report: this is what RpiPlatformDxe just published. A
  // consumer-side poll lives in any module that reads these PCDs (e.g.
  // GemDxeEntryPoint) so cross-driver plumbing can be confirmed by diffing
  // values in the serial log.
  //
  DEBUG ((DEBUG_INFO, "RpiBoardId: PcdBoardType = 0x%02x\n",
          PcdGet8 (PcdBoardType)));
  DEBUG ((DEBUG_INFO, "RpiBoardId: PcdBcm2712Stepping = %a\n",
          (PcdGet8 (PcdBcm2712Stepping) == BCM2712_STEPPING_C1) ? "C1" : "D0"));
  DEBUG ((DEBUG_INFO, "RpiBoardId: PcdBoardHasWifi (default until OTP wired) = %a\n",
          PcdGetBool (PcdBoardHasWifi) ? "TRUE" : "FALSE"));

  {
    CONST CHAR8 *DrModeStr;
    switch (BoardInfoGetUsbDrMode ()) {
      case UsbDrModeHost:       DrModeStr = "host";       break;
      case UsbDrModePeripheral: DrModeStr = "peripheral"; break;
      case UsbDrModeOtg:        DrModeStr = "otg";        break;
      default:                  DrModeStr = "unset";      break;
    }
    DEBUG ((DEBUG_INFO, "RpiBoardId: UsbDrMode = %a\n", DrModeStr));
  }

  mSystemMemorySize = BoardRevisionGetMemorySize (mBoardRevisionCode);

  SetupVariables ();
  ApplyVariables ();

  SetupPeripherals ();

  Status = InstallHiiPages ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Couldn't install HII pages. Status=%r\n", __func__, Status));
  }

  return EFI_SUCCESS;
}
