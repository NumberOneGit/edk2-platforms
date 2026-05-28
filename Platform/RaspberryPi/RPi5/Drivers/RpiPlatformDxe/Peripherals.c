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
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BrcmStbSdhciDevice.h>

#include "Peripherals.h"
#include "ConfigTable.h"
#include "RpiPlatformDxe.h"

STATIC
EFI_STATUS
EFIAPI
SdControllerSetSignalingVoltage (
  IN BRCMSTB_SDHCI_DEVICE_PROTOCOL      *This,
  IN SD_MMC_SIGNALING_VOLTAGE           Voltage
  )
{
  // CM5 (0x18) routes SDIO1 to eMMC at fixed 1.8V; skip the AON GPIO toggle
  // there. PcdBoardType replaces the gBoardType global used in the upstream
  // commit; same discriminator, sourced from the same FDT revision code.
  if (PcdGet8 (PcdBoardType) != 0x18) {
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
  .NoCD                   = FALSE,
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
  //
  // Wi-Fi SDIO bus (pins 30-35). Logical-function dispatch hides the C1 vs
  // D0 alt-value divergence; routing data lives in
  // Bcm2712LogicalFunctions.c. Pull settings are not stepping-dependent and
  // stay inline.
  //
  GpioApplyFunc ("sd2_clk");
  GpioSetPull (BCM2712_GIO, 30, BCM2712_GPIO_PIN_PULL_NONE);
  GpioApplyFunc ("sd2_cmd");
  GpioSetPull (BCM2712_GIO, 31, BCM2712_GPIO_PIN_PULL_UP);
  GpioApplyFunc ("sd2_dat0");
  GpioSetPull (BCM2712_GIO, 32, BCM2712_GPIO_PIN_PULL_UP);
  GpioApplyFunc ("sd2_dat1");
  GpioSetPull (BCM2712_GIO, 33, BCM2712_GPIO_PIN_PULL_UP);
  GpioApplyFunc ("sd2_dat2");
  GpioSetPull (BCM2712_GIO, 34, BCM2712_GPIO_PIN_PULL_UP);
  GpioApplyFunc ("sd2_dat3");
  GpioSetPull (BCM2712_GIO, 35, BCM2712_GPIO_PIN_PULL_UP);

  // wl_on_reg - consistent across all models
  GpioWrite (BCM2712_GIO, 28, TRUE);

  //
  // Per-board configuration: SD card detect on Pi 5B / Pi 500, NoCD plus
  // 8-bit slot config on CM5, NoCD on CM5 Lite. Board type values come
  // straight from PcdBoardType (the (rev >> 4) & 0xFF nibble); see the
  // SDHCI quick-fix commit at
  // https://github.com/NumberOneGit/edk2-platforms/commit/60121b8 for the
  // upstream rationale of these per-board decisions.
  //
  switch (PcdGet8 (PcdBoardType)) {
    case 0x17: // Pi 5 Model B
    case 0x19: // Pi 500
      GpioApplyFunc ("sd_card_g");
      GpioSetPull (BCM2712_GIO_AON, 5, BCM2712_GPIO_PIN_PULL_UP);
      break;

    case 0x18: // CM5: SDIO1 wired to on-module eMMC at fixed signaling
      mSdController.IsSlotRemovable = FALSE;
      mSdController.NoCD = TRUE;
      break;

    case 0x1a: // CM5 Lite
      mSdController.NoCD = TRUE;
      break;
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
