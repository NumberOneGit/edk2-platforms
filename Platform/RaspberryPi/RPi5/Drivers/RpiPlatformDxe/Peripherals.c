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
  // Wi-Fi bring-up. Skipped entirely when PcdBoardHasWifi is FALSE so a
  // no-wifi CM5 / CM5L doesn't waste time programming SDIO2 pinmux or the
  // wl_on_reg for hardware that isn't populated. The paired DSDT SDC1
  // device is gated via an SSDT install in ConfigTable.c.
  //
  if (PcdGetBool (PcdBoardHasWifi)) {
    //
    // SDIO bus to wifi controller, pins 30-35. Logical-function dispatch
    // hides the C1 vs D0 alt-value divergence; routing data lives in
    // Bcm2712LogicalFunctions.c. Pull settings are not stepping-dependent
    // and stay inline.
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

    // wl_on_reg
    GpioWrite (BCM2712_GIO, 28, TRUE);

    //
    // CM5 / CM5 Lite antenna select GPIOs. AON pin 5 (ANT1) and pin 6
    // (ANT2) in generic GPIO mode (alt 0). Defaults match the
    // bcm2712-rpi-cm5.dtsi hogs: internal antenna ON, external OFF.
    //
    // wifi_ant1 / wifi_ant2 only have D0Cm5 routes in the logical-function
    // table, so the resolver returns EFI_UNSUPPORTED on Pi 5B / Pi 500 and
    // the direction/write below are skipped silently.
    //
    if (GpioApplyFunc ("wifi_ant1") == EFI_SUCCESS) {
      GpioSetDirection (BCM2712_GIO_AON, 5, BCM2712_GPIO_PIN_OUTPUT);
      GpioWrite        (BCM2712_GIO_AON, 5, TRUE);
    }
    if (GpioApplyFunc ("wifi_ant2") == EFI_SUCCESS) {
      GpioSetDirection (BCM2712_GIO_AON, 6, BCM2712_GPIO_PIN_OUTPUT);
      GpioWrite        (BCM2712_GIO_AON, 6, FALSE);
    }
  }

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

    case 0x18: // CM5: SDIO1 wired to on-module eMMC, 8-bit mode
      mSdController.IsSlotRemovable = FALSE;
      mSdController.NoCD = TRUE;
      //
      // eMMC pad pulls per the Linux DT defaults. The VPU/start.elf has
      // already programmed the mux for these pins (otherwise eMMC boot
      // would never have reached UEFI), but pad pulls aren't guaranteed
      // to match what the eMMC datasheet recommends - so re-assert them
      // here. CMD and DAT0..7 want pull-up; DS (data strobe) wants pull-
      // down so it parks low when the controller isn't driving it. CM5
      // only; CM5 Lite has no on-module eMMC.
      //
      GpioSetPull (BCM2712_GIO, 36, BCM2712_GPIO_PIN_PULL_UP);    // CMD
      GpioSetPull (BCM2712_GIO, 37, BCM2712_GPIO_PIN_PULL_DOWN);  // DS
      GpioSetPull (BCM2712_GIO, 39, BCM2712_GPIO_PIN_PULL_UP);    // DAT0
      GpioSetPull (BCM2712_GIO, 40, BCM2712_GPIO_PIN_PULL_UP);    // DAT1
      GpioSetPull (BCM2712_GIO, 41, BCM2712_GPIO_PIN_PULL_UP);    // DAT2
      GpioSetPull (BCM2712_GIO, 42, BCM2712_GPIO_PIN_PULL_UP);    // DAT3
      GpioSetPull (BCM2712_GIO, 43, BCM2712_GPIO_PIN_PULL_UP);    // DAT4
      GpioSetPull (BCM2712_GIO, 44, BCM2712_GPIO_PIN_PULL_UP);    // DAT5
      GpioSetPull (BCM2712_GIO, 45, BCM2712_GPIO_PIN_PULL_UP);    // DAT6
      GpioSetPull (BCM2712_GIO, 46, BCM2712_GPIO_PIN_PULL_UP);    // DAT7
      break;

    case 0x1a: // CM5 Lite: SD card on carrier with no card-detect signal
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
