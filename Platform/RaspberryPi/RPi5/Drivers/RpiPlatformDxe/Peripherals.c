/** @file
 *
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/Bcm2712.h>
#include <IndustryStandard/Bcm2712Pinctrl.h>
#include <Library/Bcm2712GpioLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BrcmStbSdhciDevice.h>
#include <Library/BoardRevisionHelperLib.h>
#include <Library/BaseMemoryLib.h>

#include "Peripherals.h"
#include "ConfigTable.h"
#include "RpiPlatformDxe.h"

extern UINT32 gBoardType;

typedef struct {
  // Lower 32 bits
  UINT32 TimeoutFreq   : 6;
  UINT32 Reserved      : 1;
  UINT32 TimeoutUnit   : 1;
  UINT32 BaseClkFreq   : 8;
  UINT32 MaxBlkLen     : 2;
  UINT32 BusWidth8     : 1;
  UINT32 Adma2         : 1;
  UINT32 Reserved2     : 1;
  UINT32 HighSpeed     : 1;
  UINT32 Sdma          : 1;
  UINT32 SuspRes       : 1;
  UINT32 Voltage33     : 1;
  UINT32 Voltage30     : 1;
  UINT32 Voltage18     : 1;
  UINT32 SysBus64V4    : 1;
  UINT32 SysBus64V3    : 1;
  UINT32 AsyncInt      : 1;
  UINT32 SlotType      : 2;

  // Upper 32 bits
  UINT32 Sdr50         : 1;
  UINT32 Sdr104        : 1;
  UINT32 Ddr50         : 1;
  UINT32 Reserved3     : 1;
  UINT32 DriverTypeA   : 1;
  UINT32 DriverTypeC   : 1;
  UINT32 DriverTypeD   : 1;
  UINT32 DriverType4   : 1;
  UINT32 TimerCount    : 4;
  UINT32 Reserved4     : 1;
  UINT32 TuningSDR50   : 1;
  UINT32 RetuningMod   : 2;
  UINT32 ClkMultiplier : 8;
  UINT32 Reserved5     : 7;
  UINT32 Hs400         : 1;
} SD_MMC_HC_SLOT_CAP;

EFI_STATUS
EFIAPI
Cm5EmmcSlotCapability(
  IN  BRCMSTB_SDHCI_DEVICE_PROTOCOL *This,
  IN  UINT8                         Slot,
  OUT VOID                         *CapabilityBuffer,
  IN  UINTN                        CapabilityBufferSize,
  OUT UINT32                       *BaseClkFreq OPTIONAL
);

STATIC
EFI_STATUS
EFIAPI
SdControllerSetSignalingVoltage (
  IN BRCMSTB_SDHCI_DEVICE_PROTOCOL      *This,
  IN SD_MMC_SIGNALING_VOLTAGE           Voltage
  )
{
  if (gBoardType != 0x18) {
    // sd_io_1v8_reg
    GpioWrite (BCM2712_GIO_AON, 3, Voltage == SdMmcSignalingVoltage18);
  }

  return EFI_SUCCESS;
}

STATIC BRCMSTB_SDHCI_DEVICE_PROTOCOL mSdController = {
  .HostAddress            = BCM2712_BRCMSTB_SDIO1_HOST_BASE,
  .CfgAddress             = BCM2712_BRCMSTB_SDIO1_CFG_BASE,
  .DmaType                = NonDiscoverableDeviceDmaTypeNonCoherent,
  .IsSlotRemovable        = TRUE,
  .SetSignalingVoltage    = SdControllerSetSignalingVoltage
};

STATIC
EFI_STATUS
EFIAPI
RegisterSdControllers (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle = NULL;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gBrcmStbSdhciDeviceProtocolGuid,
                  &mSdController,
                  NULL);
  ASSERT_EFI_ERROR (Status);

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
InitGpioPinctrls (
  VOID
  )
{
  // Common WiFi pins (30-35) - consistent across all models
  GpioSetFunction(BCM2712_GIO, 30, GIO_PIN30_ALT_SD2);
  GpioSetPull(BCM2712_GIO, 30, BCM2712_GPIO_PIN_PULL_NONE);
  GpioSetFunction(BCM2712_GIO, 31, GIO_PIN31_ALT_SD2);
  GpioSetPull(BCM2712_GIO, 31, BCM2712_GPIO_PIN_PULL_UP);
  GpioSetFunction(BCM2712_GIO, 32, GIO_PIN32_ALT_SD2);
  GpioSetPull(BCM2712_GIO, 32, BCM2712_GPIO_PIN_PULL_UP);
  GpioSetFunction(BCM2712_GIO, 33, GIO_PIN33_ALT_SD2);
  GpioSetPull(BCM2712_GIO, 33, BCM2712_GPIO_PIN_PULL_UP);
  GpioSetFunction(BCM2712_GIO, 34, GIO_PIN34_ALT_SD2);
  GpioSetPull(BCM2712_GIO, 34, BCM2712_GPIO_PIN_PULL_UP);
  GpioSetFunction(BCM2712_GIO, 35, GIO_PIN35_ALT_SD2);
  GpioSetPull(BCM2712_GIO, 35, BCM2712_GPIO_PIN_PULL_UP);

  // wl_on_reg - consistent across all models
  GpioWrite(BCM2712_GIO, 28, TRUE);

  switch (gBoardType) {
    case 0x17: // Pi 5 Model B
    case 0x19: // Pi 500
      // Enable card detect for Pi 5/Pi 500
      mSdController.IsSlotRemovable = TRUE;
      GpioSetFunction(BCM2712_GIO_AON, 5, GIO_AON_PIN5_ALT_SD_CARD_G);
      GpioSetPull(BCM2712_GIO_AON, 5, BCM2712_GPIO_PIN_PULL_UP);
      break;

    case 0x18: // CM5
      // No card detect, enable 8-bit mode for CM5
      mSdController.IsSlotRemovable = FALSE;
      mSdController.GetSlotCapability = Cm5EmmcSlotCapability;
      break;

    case 0x1a: // CM5 Lite
      // No card detect for CM5 Lite
      mSdController.IsSlotRemovable = FALSE;
      break;
  }

  return EFI_SUCCESS;
}

__attribute__((unused))
EFI_STATUS
EFIAPI
Cm5EmmcSlotCapability(
  IN  BRCMSTB_SDHCI_DEVICE_PROTOCOL *This,
  IN  UINT8                         Slot,
  OUT VOID                         *CapabilityBuffer,
  IN  UINTN                        CapabilityBufferSize,
  OUT UINT32                       *BaseClkFreq OPTIONAL
)
{
  SD_MMC_HC_SLOT_CAP *Capability;

  if (Slot != 0 || CapabilityBuffer == NULL || CapabilityBufferSize < sizeof(SD_MMC_HC_SLOT_CAP)) {
    return EFI_INVALID_PARAMETER;
  }

  Capability = (SD_MMC_HC_SLOT_CAP *)CapabilityBuffer;

  ZeroMem(Capability, sizeof(SD_MMC_HC_SLOT_CAP));
  Capability->BaseClkFreq   = 200;
  Capability->MaxBlkLen     = 2;
  Capability->BusWidth8     = 1;
  Capability->HighSpeed     = 1;
  Capability->Sdr50         = 1;
  Capability->Sdr104        = 1;
  Capability->Ddr50         = 1;
  Capability->Hs400         = 1;
  Capability->Voltage18     = 1;
  Capability->SlotType      = 0;
  Capability->SysBus64V3    = 1;
  Capability->SysBus64V4    = 1;
  Capability->Adma2         = 1;
  Capability->Sdma          = 1;
  Capability->DriverTypeA   = 1;
  Capability->TimerCount    = 1;
  Capability->RetuningMod   = 2;
  Capability->TuningSDR50   = 1;

  if (BaseClkFreq != NULL) {
    *BaseClkFreq = Capability->BaseClkFreq;
  }
  return EFI_SUCCESS;
}

BCM2712_PCIE_PLATFORM_PROTOCOL  mPciePlatform = {
  .Mem32BusBase = PCI_RESERVED_MEM32_BASE,
  .Mem32Size    = PCI_RESERVED_MEM32_SIZE,

  .Settings         = {
    [1] = { // Connector (configurable)
      .Enabled      = PCIE1_SETTINGS_ENABLED_DEFAULT,
      .MaxLinkSpeed = PCIE1_SETTINGS_MAX_LINK_SPEED_DEFAULT
    },
    [2] = { // RP1 (fixed)
      .Enabled      = TRUE,
      .MaxLinkSpeed = 2,
      .RcbMatchMps  = TRUE,
      .VdmToQosMap  = 0xbbaa9888
    }
  }
};

STATIC
EFI_STATUS
EFIAPI
RegisterPciePlatform (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle = NULL;

  ASSERT_PROTOCOL_ALREADY_INSTALLED (NULL, &gBcm2712PciePlatformProtocolGuid);
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gBcm2712PciePlatformProtocolGuid,
                  &mPciePlatform,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}

EFI_STATUS
EFIAPI
SetupPeripherals (
  VOID
  )
{
  InitGpioPinctrls ();

  RegisterSdControllers ();
  RegisterPciePlatform ();

  return EFI_SUCCESS;
}

VOID
EFIAPI
ApplyPeripheralVariables (
  VOID
  )
{
}

VOID
EFIAPI
SetupPeripheralVariables (
  VOID
  )
{
  EFI_STATUS    Status;
  UINTN         Size;

  Size = sizeof (BCM2712_PCIE_CONTROLLER_SETTINGS);
  Status = gRT->GetVariable (L"Pcie1Settings",
                  &gRpiPlatformFormSetGuid,
                  NULL, &Size, &mPciePlatform.Settings[1]);
  if (EFI_ERROR (Status)) {
    Status = gRT->SetVariable (
                    L"Pcie1Settings",
                    &gRpiPlatformFormSetGuid,
                    EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                    Size,
                    &mPciePlatform.Settings[1]);
    ASSERT_EFI_ERROR (Status);
  }
}
