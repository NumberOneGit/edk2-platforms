/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/BrcmStbSdhciDevice.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/SdMmcOverride.h>

#include "BrcmStbSdhciDxe.h"

STATIC
EFI_STATUS
EFIAPI
SdMmcCapability (
  IN      EFI_HANDLE                      ControllerHandle,
  IN      UINT8                           Slot,
  IN OUT  VOID                            *SdMmcHcSlotCapability,
  IN OUT  UINT32                          *BaseClkFreq
  )
{
  SD_MMC_HC_SLOT_CAP     *Capability;

  if (Slot != 0) {
    return EFI_UNSUPPORTED;
  }
  if (SdMmcHcSlotCapability == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Capability = SdMmcHcSlotCapability;

// Set base clock frequency (200MHz)
  Capability->BaseClkFreq = 200;

  // Set max block length (512 bytes)
  Capability->MaxBlkLen = 2;

  // Enable 8-bit support
  Capability->BusWidth8 = 1;

  // Enable high-speed support
  Capability->HighSpeed = 1;

  // Enable SDR50 support
  Capability->Sdr50 = 1;

  // Enable SDR104 support
  Capability->Sdr104 = 1;

  // Enable DDR50 support
  Capability->Ddr50 = 1;

  // Enable HS400 support
  Capability->Hs400 = 1;

  // Support 1.8V voltage
  Capability->Voltage18 = 1;

  // Support 3.3V voltage
  Capability->Voltage33 = 0;

  // Set non-removable slot
  Capability->SlotType = 0;

  // Set 64-bit system bus support
  Capability->SysBus64V3 = 1;

  // Set DMA support
  Capability->Adma2 = 1;
  Capability->Sdma = 1;

  // Set driver type support
  Capability->DriverTypeA = 1;
  Capability->DriverTypeC = 0;
  Capability->DriverTypeD = 0;

  // Hardware retuning is not supported.
  Capability->TimerCount = 1;
  Capability->RetuningMod = 2;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
SdMmcNotifyPhase (
  IN      EFI_HANDLE                      ControllerHandle,
  IN      UINT8                           Slot,
  IN      EDKII_SD_MMC_PHASE_TYPE         PhaseType,
  IN OUT  VOID                            *PhaseData
  )
{
  EFI_STATUS                        Status;
  BRCMSTB_SDHCI_DEVICE_PROTOCOL     *Device;

  if (Slot != 0) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->HandleProtocol (
                  ControllerHandle,
                  &gBrcmStbSdhciDeviceProtocolGuid,
                  (VOID **)&Device);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get protocol. Status=%r\n",
            __func__, Status));
    return EFI_UNSUPPORTED;
  }

  switch (PhaseType) {
    case EdkiiSdMmcSetSignalingVoltage:
      if (PhaseData == NULL) {
        return EFI_INVALID_PARAMETER;
      }
      if (Device->SetSignalingVoltage != NULL) {
        return Device->SetSignalingVoltage (Device, *(SD_MMC_SIGNALING_VOLTAGE *)PhaseData);
      }
      break;

    default:
      break;
  }

  return EFI_SUCCESS;
}

STATIC EDKII_SD_MMC_OVERRIDE mSdMmcOverride = {
  EDKII_SD_MMC_OVERRIDE_PROTOCOL_VERSION,
  SdMmcCapability,
  SdMmcNotifyPhase,
};

STATIC
EFI_STATUS
EFIAPI
StartDevice (
  IN BRCMSTB_SDHCI_DEVICE_PROTOCOL    *This,
  IN EFI_HANDLE                       ControllerHandle
  )
{
  EFI_STATUS Status;

  // Set bus width to 8-bit
  MmioAndThenOr32 (This->CfgAddress + 0x48,  // SDIO_CFG_BUS_WIDTH
                   ~(BIT2 | BIT1 | BIT0),
                   BIT2);  // 8-bit mode

  // Enable HS200 mode
  MmioAndThenOr32 (This->CfgAddress + 0x1b0,  // SDIO_CFG_HS200_MODE
                   ~BIT0,
                   BIT0);

  // Enable HS400 mode
  MmioAndThenOr32 (This->CfgAddress + 0x1b4,  // SDIO_CFG_HS400_MODE
                   ~BIT0,
                   BIT0);

  //
  // Set the PHY DLL as clock source to support higher speed modes
  // reliably.
  //
  MmioAndThenOr32 (This->CfgAddress + SDIO_CFG_MAX_50MHZ_MODE,
                   ~SDIO_CFG_MAX_50MHZ_MODE_ENABLE,
                   SDIO_CFG_MAX_50MHZ_MODE_STRAP_OVERRIDE);

  if (This->IsSlotRemovable) {
    MmioAndThenOr32 (This->CfgAddress + SDIO_CFG_SD_PIN_SEL,
                     ~SDIO_CFG_SD_PIN_SEL_MASK,
                     SDIO_CFG_SD_PIN_SEL_CARD);
  } else {
    MmioAndThenOr32 (This->CfgAddress + SDIO_CFG_CTRL,
                     ~SDIO_CFG_CTRL_SDCD_N_TEST_LEV,
                     SDIO_CFG_CTRL_SDCD_N_TEST_EN);
  }

  Status = RegisterNonDiscoverableMmioDevice (
              NonDiscoverableDeviceTypeSdhci,
              This->DmaType,
              NULL,
              &ControllerHandle,
              1,
              This->HostAddress, SDIO_HOST_SIZE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "%a: Failed to register Broadcom STB SDHCI controller at 0x%lx. Status=%r\n",
      __func__, This->HostAddress, Status));
    return Status;
  }

  return Status;
}

STATIC VOID  *mProtocolInstallEventRegistration;

STATIC
VOID
EFIAPI
NotifyProtocolInstall (
  IN EFI_EVENT    Event,
  IN VOID         *Context
  )
{
  EFI_STATUS                          Status;
  EFI_HANDLE                          Handle;
  UINTN                               BufferSize;
  BRCMSTB_SDHCI_DEVICE_PROTOCOL       *Device;

  while (TRUE) {
    BufferSize = sizeof (EFI_HANDLE);
    Status = gBS->LocateHandle (
                    ByRegisterNotify,
                    NULL,
                    mProtocolInstallEventRegistration,
                    &BufferSize,
                    &Handle);
    if (EFI_ERROR (Status)) {
      if (Status != EFI_NOT_FOUND) {
        DEBUG ((DEBUG_ERROR, "%a: Failed to locate protocol. Status=%r\n",
                __func__, Status));
      }
      break;
    }

    Status = gBS->HandleProtocol (
                    Handle,
                    &gBrcmStbSdhciDeviceProtocolGuid,
                    (VOID **)&Device);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to get protocol. Status=%r\n",
              __func__, Status));
      break;
    }

    Status = StartDevice (Device, Handle);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to start device. Status=%r\n",
              __func__, Status));
      break;
    }
  }
}

EFI_STATUS
EFIAPI
BrcmStbSdhciDxeInitialize (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS     Status;
  EFI_HANDLE     Handle;
  EFI_EVENT      ProtocolInstallEvent;

  ProtocolInstallEvent = EfiCreateProtocolNotifyEvent (
        &gBrcmStbSdhciDeviceProtocolGuid,
        TPL_CALLBACK,
        NotifyProtocolInstall,
        NULL,
        &mProtocolInstallEventRegistration);
  if (ProtocolInstallEvent == NULL) {
    ASSERT (FALSE);
    return EFI_OUT_OF_RESOURCES;
  }

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
              &Handle,
              &gEdkiiSdMmcOverrideProtocolGuid,
              &mSdMmcOverride,
              NULL);
  ASSERT_EFI_ERROR (Status);

  return Status;
}
