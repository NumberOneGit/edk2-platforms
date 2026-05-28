/** @file
 *
 *  Logical-function -> (pin, alt) table for the BCM2712 GPIO library.
 *
 *  The user-facing API is two functions (GpioApplyFunc, GpioResolveFunc)
 *  declared in Bcm2712GpioLib.h. The exhaustive routing data is private to
 *  this file. Callers reference logical functions by string name; values are
 *  resolved at lookup time using:
 *      stepping        from PcdBcm2712Stepping
 *      board family    from PcdBoardType (CM5 / CM5L override one column)
 *
 *  ----------------------------------------------------------------
 *  Source of truth: drivers/pinctrl/bcm/pinctrl-bcm2712.c in linux2.
 *
 *  Linux declares per-stepping pin-funcs tables:
 *      bcm2712_c0_gpio_pin_funcs[]      (main GIO, C1 silicon)
 *      bcm2712_c0_aon_gpio_pin_funcs[]  (AON GIO, C1 silicon)
 *      bcm2712_d0_gpio_pin_funcs[]      (main GIO, D0 silicon)
 *      bcm2712_d0_aon_gpio_pin_funcs[]  (AON GIO, D0 silicon)
 *
 *  Each row is PIN(pin, f0, f1, f2, f3, f4, f5, f6, f7) where f0..f7 are
 *  function names at positions 0..7. The BCM2712 pinmux register treats
 *  raw-value 0 as "GPIO mode" and values 1..8 as the named alts, so the
 *  EDK2 alt value programmed into the register equals (position + 1).
 *  EDK2 reference headers (Bcm2712C1PinReference.h / Bcm2712D0PinReference.h)
 *  encode this directly: enum entries auto-increment from 1.
 *
 *  Adding a new row:
 *    1. Find the function name in the relevant Linux pin_funcs table.
 *    2. Note the (pin, position) on both C1 and D0; alt = position + 1.
 *    3. Append a row here with the C1 and D0 routes. Use ROUTE_NONE when
 *       the function does not exist on a stepping.
 *    4. CM5/CM5L override: only set D0Cm5 when the carrier board reroutes
 *       the pin (e.g. AON pin 5 is sd_card_g on Pi 5B / Pi 500 but doubles
 *       as the wifi antenna ANT1 GPIO on CM5/CM5L per bcm2712-rpi-cm5.dtsi).
 *
 *  Naming: lowercase, mirrors Linux. Where a function appears on multiple
 *  pins, suffix with "_<role>" (e.g. "sd2_clk", "sd2_dat0") so callers can
 *  pick the specific pin without knowing the number.
 **/

#include <Uefi.h>
#include <Library/Bcm2712GpioLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>

//
// Mirrors BCM2712_STEPPING_C1 in Platform/.../BoardRevisionHelperLib.h.
// Duplicated locally to keep this silicon lib free of a Platform header dep.
//
#define BCM2712_STEPPING_C1_VALUE  1

//
// CM5 / CM5L board type values. PcdBoardType carries the raw (rev >> 4) & 0xFF
// byte from the FDT revision code.
//
#define BCM2712_BOARD_TYPE_CM5       0x18
#define BCM2712_BOARD_TYPE_CM5_LITE  0x1A

typedef struct {
  BCM2712_GPIO_TYPE  Type;
  UINT8              Pin;
  UINT8              Alt;
} BCM2712_PIN_ROUTE;

//
// ROUTE_NONE = function does not exist on this variant. The Alt sentinel
// (MAX_UINT8) lets a single field carry "unmapped" without sacrificing a
// real alt value, and lets callers distinguish "this function is irrelevant
// on this board" (EFI_UNSUPPORTED) from "you typo'd the name" (EFI_NOT_FOUND).
//
#define ROUTE_NONE  { 0, 0, MAX_UINT8 }
#define ROUTE_VALID(r)  ((r).Alt != MAX_UINT8)

typedef struct {
  CONST CHAR8        *Name;     // Linux pin-funcs function name (+pin suffix)
  BCM2712_PIN_ROUTE   C1;       // route on BCM2712 C1 silicon
  BCM2712_PIN_ROUTE   D0;       // route on BCM2712 D0 silicon (default)
  BCM2712_PIN_ROUTE   D0Cm5;    // CM5/CM5L override, if the carrier reroutes
} BCM2712_LOGICAL_FUNC;

//
// Exhaustive table. Order is grouped logically (SDIO bus together, antennas
// together, etc.) not alphabetical. Lookup is linear; the table is small
// enough that an index/hash is unnecessary at DXE init.
//
// Each row's comment cites the Linux source line range so a reviewer can
// re-derive the alt values without trusting this file.
//
STATIC CONST BCM2712_LOGICAL_FUNC mBcm2712Functions[] = {
  //
  // Wi-Fi SDIO2 bus, pins 30-35 on main GIO.
  // C1: bcm2712_c0_gpio_pin_funcs PIN(30..35)  -- positions vary per pin
  // D0: bcm2712_d0_gpio_pin_funcs PIN(30..35)  -- sd2 at position 0 every pin
  //
  { "sd2_clk",  { BCM2712_GIO, 30, 4 }, { BCM2712_GIO, 30, 1 }, ROUTE_NONE },
  { "sd2_cmd",  { BCM2712_GIO, 31, 4 }, { BCM2712_GIO, 31, 1 }, ROUTE_NONE },
  { "sd2_dat0", { BCM2712_GIO, 32, 4 }, { BCM2712_GIO, 32, 1 }, ROUTE_NONE },
  { "sd2_dat1", { BCM2712_GIO, 33, 3 }, { BCM2712_GIO, 33, 1 }, ROUTE_NONE },
  { "sd2_dat2", { BCM2712_GIO, 34, 4 }, { BCM2712_GIO, 34, 1 }, ROUTE_NONE },
  { "sd2_dat3", { BCM2712_GIO, 35, 3 }, { BCM2712_GIO, 35, 1 }, ROUTE_NONE },

  //
  // SD card detect (AON pin 5).
  // C1 AON PIN(5, gpclk1, ir_in, vc_i2csl, clk_observe, aon_pwm, sd_card_g, ...) -> pos 5 -> alt 6
  // D0 AON PIN(5, gpclk1, ir_in, aon_pwm, sd_card_g, ...)                       -> pos 3 -> alt 4
  // CM5/CM5L reuse this pin for the WiFi internal-antenna select GPIO
  // (bcm2712-rpi-cm5.dtsi ant_pins) so D0Cm5 = ROUTE_NONE here. The antenna
  // role is exposed as "wifi_ant1" below.
  //
  { "sd_card_g", { BCM2712_GIO_AON, 5, 6 }, { BCM2712_GIO_AON, 5, 4 }, ROUTE_NONE },

  //
  // CM5 / CM5L WiFi antenna control GPIOs. AON GIO pins 5 and 6 in GPIO mode
  // (alt 0). Per bcm2712-rpi-cm5.dtsi:
  //    ant1 (AON_GPIO_05) high -> internal antenna on
  //    ant2 (AON_GPIO_06) high -> external antenna on
  // These pins serve unrelated purposes on Pi 5B / Pi 500 so the C1 and D0
  // columns are ROUTE_NONE (the function does not exist on those boards).
  //
  { "wifi_ant1", ROUTE_NONE, ROUTE_NONE, { BCM2712_GIO_AON, 5, 0 } },
  { "wifi_ant2", ROUTE_NONE, ROUTE_NONE, { BCM2712_GIO_AON, 6, 0 } },

  //
  // ---------------------------------------------------------------
  // Sample rows below: cross-referenced from Linux but not yet driven
  // by any caller. Left in to demonstrate the row pattern and so the
  // common UART/I2C routings are immediately reachable by future code.
  // ---------------------------------------------------------------
  //

  //
  // vc_uart0 on AON pin 0 (BCM2712 console-class UART).
  // C1 AON PIN(0, ir_in, vc_spi0, vc_uart3, ...)  -- vc_uart0 not at pin 0 on C1
  // D0 AON PIN(0, ir_in, vc_spi0, vc_uart0, ...)  -> pos 2 -> alt 3
  //
  { "vc_uart0_aon0_tx", ROUTE_NONE, { BCM2712_GIO_AON, 0, 3 }, ROUTE_NONE },

  //
  // gpclk0 on AON pin 4 (general-purpose clock output).
  // C1 AON PIN(4, gpclk0, ...) -> pos 0 -> alt 1
  // D0 AON PIN(4, gpclk0, ...) -> pos 0 -> alt 1   (1:1 carry-over)
  //
  { "gpclk0_aon4", { BCM2712_GIO_AON, 4, 1 }, { BCM2712_GIO_AON, 4, 1 }, ROUTE_NONE },

  //
  // ir_in on AON pin 0 (consumer IR receiver, used by media-remote inputs).
  // C1 AON PIN(0, ir_in, ...) -> pos 0 -> alt 1
  // D0 AON PIN(0, ir_in, ...) -> pos 0 -> alt 1   (1:1 carry-over)
  //
  { "ir_in_aon0",  { BCM2712_GIO_AON, 0, 1 }, { BCM2712_GIO_AON, 0, 1 }, ROUTE_NONE },

  //
  // uart1 on AON pin 6 (the 2712 secondary UART).
  // C1 AON PIN(6, uart1, ...) -> pos 0 -> alt 1
  // D0 AON PIN(6, uart1, ...) -> pos 0 -> alt 1   (1:1 carry-over)
  //
  { "uart1_aon6_tx", { BCM2712_GIO_AON, 6, 1 }, { BCM2712_GIO_AON, 6, 1 }, ROUTE_NONE },

  //
  // bsc_m1 on AON pin 13 (BSC master 1 SDA).
  // C1 AON PIN(13, bsc_m1, ...) -> pos 0 -> alt 1
  // D0 AON PIN(13, bsc_m1, ...) -> pos 0 -> alt 1
  //
  { "bsc_m1_aon13_sda", { BCM2712_GIO_AON, 13, 1 }, { BCM2712_GIO_AON, 13, 1 }, ROUTE_NONE },
};

STATIC
CONST BCM2712_LOGICAL_FUNC *
LookupFunc (
  IN  CONST CHAR8  *Name
  )
{
  UINTN  Index;

  if (Name == NULL) {
    return NULL;
  }

  for (Index = 0; Index < ARRAY_SIZE (mBcm2712Functions); Index++) {
    if (AsciiStrCmp (mBcm2712Functions[Index].Name, Name) == 0) {
      return &mBcm2712Functions[Index];
    }
  }
  return NULL;
}

STATIC
CONST BCM2712_PIN_ROUTE *
SelectRoute (
  IN  CONST BCM2712_LOGICAL_FUNC  *Func
  )
{
  UINT8  BoardType;

  //
  // Selection order matches the table column order. CM5/CM5L overrides D0
  // only when a D0Cm5 entry is present; otherwise the D0 column applies.
  //
  if (PcdGet8 (PcdBcm2712Stepping) == BCM2712_STEPPING_C1_VALUE) {
    return ROUTE_VALID (Func->C1) ? &Func->C1 : NULL;
  }

  BoardType = PcdGet8 (PcdBoardType);
  if (((BoardType == BCM2712_BOARD_TYPE_CM5) ||
       (BoardType == BCM2712_BOARD_TYPE_CM5_LITE)) &&
      ROUTE_VALID (Func->D0Cm5)) {
    return &Func->D0Cm5;
  }
  return ROUTE_VALID (Func->D0) ? &Func->D0 : NULL;
}

EFI_STATUS
EFIAPI
GpioResolveFunc (
  IN  CONST CHAR8         *Name,
  OUT BCM2712_GPIO_TYPE   *Type,
  OUT UINT8               *Pin,
  OUT UINT8               *Alt
  )
{
  CONST BCM2712_LOGICAL_FUNC  *Func;
  CONST BCM2712_PIN_ROUTE     *Route;

  if ((Type == NULL) || (Pin == NULL) || (Alt == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Func = LookupFunc (Name);
  if (Func == NULL) {
    return EFI_NOT_FOUND;
  }

  Route = SelectRoute (Func);
  if (Route == NULL) {
    return EFI_UNSUPPORTED;
  }

  *Type = Route->Type;
  *Pin  = Route->Pin;
  *Alt  = Route->Alt;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GpioApplyFunc (
  IN  CONST CHAR8  *Name
  )
{
  EFI_STATUS         Status;
  BCM2712_GPIO_TYPE  Type;
  UINT8              Pin;
  UINT8              Alt;

  Status = GpioResolveFunc (Name, &Type, &Pin, &Alt);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_UNSUPPORTED) {
      DEBUG ((DEBUG_VERBOSE, "%a: %a not routed on current variant\n",
              __func__, Name));
    } else {
      DEBUG ((DEBUG_ERROR, "%a: lookup '%a' failed: %r\n",
              __func__, Name, Status));
    }
    return Status;
  }

  GpioSetFunction (Type, Pin, Alt);
  return EFI_SUCCESS;
}
