/** @file
 *
 *  RP1 device init driver. Hosts MMIO bring-up that the Rp1Bus enumeration
 *  driver shouldn't be responsible for. The cooling-fan PWM bring-up was
 *  previously bolted onto Rp1BusDxe; it lives here now so Rp1Bus can stay
 *  focused on bus/PCIe topology. New per-device inits (GPIO defaults,
 *  status LED, etc.) should follow the same pattern: a small Configure*
 *  function called from the entry point.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Rp1Bus.h>
#include <Rp1.h>
#include <Rp1Clock.h>
#include <Rp1Gpio.h>
#include <Rp1Pwm.h>
#include <Uefi.h>

#include "Rp1Dxe.h"

//
// Cooling fan PWM wiring (matches bcm2712-rpi-5-b.dts cooling_fan node):
//   pwms = <&rp1_pwm1 3 41566 PWM_POLARITY_INVERTED>
// Pin 45 of RP1 routes PWM1 channel 3 when set to alt0.
//
#define RP1_FAN_PWM_GPIO      45
#define RP1_FAN_PWM_FUNCTION  Rp1GpioFunctionAlt0
#define RP1_FAN_PWM_CHANNEL   3
#define RP1_FAN_PWM_RANGE     2000
#define RP1_FAN_PWM_DUTY      500
#define RP1_FAN_PWM_INVERTED  TRUE

STATIC
EFI_STATUS
EFIAPI
ConfigureFanPrereqs (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase
  )
{
  RP1_GPIO_PIN  FanPin;
  UINT32        Ctrl;
  EFI_STATUS    Status;

  Status = Rp1GpioGetPin (PeripheralBase, RP1_FAN_PWM_GPIO, &FanPin);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Rp1Dxe: Failed to resolve fan GPIO. Status=%r\n", Status));
    return Status;
  }

  //
  // Bring up the PWM1 clock at a known divider before routing the pad.
  // Without this the PWM block sees no clock and the fan never spins.
  //
  Rp1ClockWrite32 (
    PeripheralBase + RP1_CLOCKS_MAIN_BASE,
    RP1_CLK_BLOCK_PWM1,
    RP1_CLK_REG_DIV_INT,
    1
    );
  Rp1ClockWrite32 (
    PeripheralBase + RP1_CLOCKS_MAIN_BASE,
    RP1_CLK_BLOCK_PWM1,
    RP1_CLK_FRAC_REG_DIV_FRAC,
    0
    );

  Ctrl = Rp1ClockRead32 (
           PeripheralBase + RP1_CLOCKS_MAIN_BASE,
           RP1_CLK_BLOCK_PWM1,
           RP1_CLK_REG_CTRL
           );
  Ctrl = Rp1ClockSetField (
           Ctrl,
           RP1_CLK_CTRL_AUXSRC_MASK,
           RP1_CLK_CTRL_AUXSRC_OFFSET,
           2
           );
  Ctrl |= RP1_CLK_CTRL_ENABLE;
  Rp1ClockWrite32 (
    PeripheralBase + RP1_CLOCKS_MAIN_BASE,
    RP1_CLK_BLOCK_PWM1,
    RP1_CLK_REG_CTRL,
    Ctrl
    );

  //
  // Route GPIO pin 45 to PWM1.
  // Without these OUTOVER / OEOVER / INOVER writes the pad ignores the PWM
  // peripheral and the fan stays off even after the PWM controller starts
  // generating the signal.
  //
  Rp1GpioSetFunction (&FanPin, RP1_FAN_PWM_FUNCTION);

  Ctrl = Rp1GpioReadCtrl (&FanPin);
  Ctrl = Rp1GpioSetField (
           Ctrl,
           RP1_GPIO_CTRL_OUTOVER_MASK,
           RP1_GPIO_CTRL_OUTOVER_OFFSET,
           RP1_GPIO_OUTOVER_PERI
           );
  Ctrl = Rp1GpioSetField (
           Ctrl,
           RP1_GPIO_CTRL_OEOVER_MASK,
           RP1_GPIO_CTRL_OEOVER_OFFSET,
           RP1_GPIO_OEOVER_PERI
           );
  Ctrl = Rp1GpioSetField (
           Ctrl,
           RP1_GPIO_CTRL_INOVER_MASK,
           RP1_GPIO_CTRL_INOVER_OFFSET,
           RP1_GPIO_INOVER_PERI
           );
  Rp1GpioWriteCtrl (&FanPin, Ctrl);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ConfigurePwm1Ch3 (
  IN EFI_PHYSICAL_ADDRESS  PeripheralBase
  )
{
  return Rp1PwmConfigure (
           PeripheralBase + RP1_PWM1_BASE,
           RP1_FAN_PWM_CHANNEL,
           RP1_FAN_PWM_RANGE,
           RP1_FAN_PWM_DUTY,
           RP1_FAN_PWM_INVERTED,
           TRUE
           );
}

EFI_STATUS
EFIAPI
Rp1DxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS              Status;
  RP1_BUS_PROTOCOL        *Rp1Bus;
  EFI_PHYSICAL_ADDRESS    PeripheralBase;

  Status = gBS->LocateProtocol (
                  &gRp1BusProtocolGuid,
                  NULL,
                  (VOID **)&Rp1Bus
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Rp1Dxe: Rp1Bus protocol not located. Status=%r\n", Status));
    return Status;
  }

  PeripheralBase = Rp1Bus->GetPeripheralBase (Rp1Bus);

  Status = ConfigureFanPrereqs (PeripheralBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Rp1Dxe: ConfigureFanPrereqs failed. Status=%r\n", Status));
  }

  Status = ConfigurePwm1Ch3 (PeripheralBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Rp1Dxe: ConfigurePwm1Ch3 failed. Status=%r\n", Status));
  }

  return EFI_SUCCESS;
}
