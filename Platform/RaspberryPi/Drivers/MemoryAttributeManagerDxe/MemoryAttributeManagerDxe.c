/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "MemoryAttributeManagerDxe.h"

extern UINT8  MemoryAttributeManagerDxeHiiBin[];
extern UINT8  MemoryAttributeManagerDxeStrings[];

typedef struct {
  VENDOR_DEVICE_PATH          VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL    End;
} HII_VENDOR_DEVICE_PATH;

STATIC HII_VENDOR_DEVICE_PATH  mVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    MEMORY_ATTRIBUTE_MANAGER_FORMSET_GUID
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
  EFI_STATUS      Status;
  EFI_HII_HANDLE  HiiHandle;
  EFI_HANDLE      DriverHandle;

  DriverHandle = NULL;
  Status       = gBS->InstallMultipleProtocolInterfaces (
                        &DriverHandle,
                        &gEfiDevicePathProtocolGuid,
                        &mVendorDevicePath,
                        NULL
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HiiHandle = HiiAddPackages (
                &gMemoryAttributeManagerFormSetGuid,
                DriverHandle,
                MemoryAttributeManagerDxeStrings,
                MemoryAttributeManagerDxeHiiBin,
                NULL
                );

  if (HiiHandle == NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           DriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mVendorDevicePath,
           NULL
           );
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

/**
  This function uninstalls the recently added EFI_MEMORY_ATTRIBUTE_PROTOCOL
  to workaround older versions of OS loaders/shims using it incorrectly and
  throwing a Synchronous Exception.
  See:
    - https://github.com/microsoft/mu_silicon_arm_tiano/issues/124
    - https://edk2.groups.io/g/devel/topic/99631663
**/
STATIC
VOID
UninstallEfiMemoryAttributeProtocol (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINTN       Size;
  VOID        *MemoryAttributeProtocol;

  Size   = sizeof (Handle);
  Status = gBS->LocateHandle (
                  ByProtocol,
                  &gEfiMemoryAttributeProtocolGuid,
                  NULL,
                  &Size,
                  &Handle
                  );
  if (EFI_ERROR (Status)) {
    ASSERT (Status == EFI_NOT_FOUND);
    return;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiMemoryAttributeProtocolGuid,
                  &MemoryAttributeProtocol
                  );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = gBS->UninstallProtocolInterface (
                  Handle,
                  &gEfiMemoryAttributeProtocolGuid,
                  MemoryAttributeProtocol
                  );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "%a: Done!\n", __func__));
}

EFI_STATUS
EFIAPI
MemoryAttributeManagerInitialize (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                              Status;
  UINTN                                   Size;
  MEMORY_ATTRIBUTE_MANAGER_VARSTORE_DATA  Config;

  Config.Enabled = PROTOCOL_ENABLED_DEFAULT;

  Size   = sizeof (MEMORY_ATTRIBUTE_MANAGER_VARSTORE_DATA);
  Status = gRT->GetVariable (
                  MEMORY_ATTRIBUTE_MANAGER_DATA_VAR_NAME,
                  &gMemoryAttributeManagerFormSetGuid,
                  NULL,
                  &Size,
                  &Config
                  );
  if (EFI_ERROR (Status)) {
    Status = gRT->SetVariable (
                    MEMORY_ATTRIBUTE_MANAGER_DATA_VAR_NAME,
                    &gMemoryAttributeManagerFormSetGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                    Size,
                    &Config
                    );
    ASSERT_EFI_ERROR (Status);
  }

  if (!Config.Enabled) {
    UninstallEfiMemoryAttributeProtocol ();
  }

  return InstallHiiPages ();
}
