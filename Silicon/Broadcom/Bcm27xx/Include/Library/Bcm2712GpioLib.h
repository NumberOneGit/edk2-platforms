/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/


#ifndef __BCM2712_GPIO_LIB_H__
#define __BCM2712_GPIO_LIB_H__

typedef enum {
  BCM2712_GIO = 0,
  BCM2712_GIO_AON,
  BCM2712_GIO_COUNT
} BCM2712_GPIO_TYPE;

typedef enum {
  BCM2712_GPIO_ALT_IO = 0,
  BCM2712_GPIO_ALT_1,
  BCM2712_GPIO_ALT_2,
  BCM2712_GPIO_ALT_3,
  BCM2712_GPIO_ALT_4,
  BCM2712_GPIO_ALT_5,
  BCM2712_GPIO_ALT_6,
  BCM2712_GPIO_ALT_7,
  BCM2712_GPIO_ALT_8,
  BCM2712_GPIO_ALT_COUNT
} BCM2712_GPIO_ALT;

typedef enum {
  BCM2712_GPIO_PIN_PULL_NONE  = 0,
  BCM2712_GPIO_PIN_PULL_DOWN  = 1,
  BCM2712_GPIO_PIN_PULL_UP    = 2
} BCM2712_GPIO_PIN_PULL;

typedef enum {
  BCM2712_GPIO_PIN_OUTPUT = 0,
  BCM2712_GPIO_PIN_INPUT  = 1
} BCM2712_GPIO_PIN_DIRECTION;

UINT8
EFIAPI
GpioGetFunction (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  );

VOID
EFIAPI
GpioSetFunction (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  UINT8                           Function
  );

BCM2712_GPIO_PIN_PULL
EFIAPI
GpioGetPull (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  );

VOID
EFIAPI
GpioSetPull (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BCM2712_GPIO_PIN_PULL           Pull
  );

BOOLEAN
EFIAPI
GpioRead (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  );

VOID
EFIAPI
GpioWrite (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BOOLEAN                         Value
  );

BCM2712_GPIO_PIN_DIRECTION
EFIAPI
GpioGetDirection (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  );

VOID
EFIAPI
GpioSetDirection (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BCM2712_GPIO_PIN_DIRECTION      Direction
  );

//
// Logical-function dispatch. Programs the correct (pin, alt) for the named
// function on the current silicon stepping and board family. Routing data
// lives in the private Bcm2712LogicalFunctions.c table cross-referenced from
// Linux's bcm2712 pin_funcs[] arrays.
//
// Function names follow the Linux convention (lowercase, e.g. "sd2_clk",
// "sd_card_g", "vc_uart0"). Per-pin disambiguation suffixes are used where
// a single Linux function name lands on multiple pins (e.g. "sd2_clk" for
// SDIO2 pin 30 vs the rest of the SDIO bus).
//
// Returns EFI_NOT_FOUND if the function name is unknown, or EFI_UNSUPPORTED
// if the function has no routing on the current variant (i.e. callers can
// invoke it unconditionally and let the resolver short-circuit when the
// function does not exist on this board).
//
EFI_STATUS
EFIAPI
GpioApplyFunc (
  IN  CONST CHAR8                     *Name
  );

//
// Same lookup as GpioApplyFunc but returns the resolved route without
// programming the mux. Useful for callers that need to know the pin
// number (e.g. to apply pull settings or read the GPIO line).
//
EFI_STATUS
EFIAPI
GpioResolveFunc (
  IN  CONST CHAR8                     *Name,
  OUT BCM2712_GPIO_TYPE               *Type,
  OUT UINT8                           *Pin,
  OUT UINT8                           *Alt
  );

#endif // __BCM2712_GPIO_LIB_H__
