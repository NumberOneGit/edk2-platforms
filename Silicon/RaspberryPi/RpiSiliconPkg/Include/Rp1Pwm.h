/** @file
  RP1 PWM helper functions.

  PWM0 and PWM1 use the same 4-channel register layout. These helpers operate
  on an already-mapped absolute PWM block base.

  Copyright (c) 2026
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __RP1_PWM_H__
#define __RP1_PWM_H__

#include <Library/BaseLib.h>
#include <Rp1Mmio.h>
#include <Uefi.h>

#define RP1_PWM_SIZE           0x4000
#define RP1_PWM_CHANNEL_COUNT  4

#define RP1_PWM_GLOBAL_CTRL        0x000
#define RP1_PWM_CHANNEL_CTRL(Ch)   (0x014 + ((UINTN)(Ch) * 0x10))
#define RP1_PWM_CHANNEL_RANGE(Ch)  (0x018 + ((UINTN)(Ch) * 0x10))
#define RP1_PWM_CHANNEL_DUTY(Ch)   (0x020 + ((UINTN)(Ch) * 0x10))

#define RP1_PWM_CHANNEL_DEFAULT  (BIT8 | BIT0)
#define RP1_PWM_MODE_MASK        (BIT1 | BIT0)
#define RP1_PWM_POLARITY         BIT3
#define RP1_PWM_SET_UPDATE       BIT31
#define RP1_PWM_CHANNEL_ENABLE(Ch)  ((UINT32)1u << (Ch))

#define RP1_PWM1_CHAN3_RANGE  RP1_PWM_CHANNEL_RANGE (3)
#define RP1_PWM1_CHAN3_DUTY   RP1_PWM_CHANNEL_DUTY (3)

STATIC
inline
BOOLEAN
Rp1PwmChannelValid (
  IN UINT8  Channel
  )
{
  return Channel < RP1_PWM_CHANNEL_COUNT;
}

STATIC
inline
UINTN
Rp1PwmRegAddr (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINTN                 RegisterOffset
  )
{
  return (UINTN)PwmBase + RegisterOffset;
}

STATIC
inline
VOID
Rp1PwmApplyUpdate (
  IN EFI_PHYSICAL_ADDRESS  PwmBase
  )
{
  Rp1MmioOr32 (Rp1PwmRegAddr (PwmBase, RP1_PWM_GLOBAL_CTRL), RP1_PWM_SET_UPDATE);
}

STATIC
inline
EFI_STATUS
Rp1PwmRequestChannel (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINT8                 Channel
  )
{
  if (!Rp1PwmChannelValid (Channel)) {
    return EFI_INVALID_PARAMETER;
  }

  Rp1MmioWrite32 (
    Rp1PwmRegAddr (PwmBase, RP1_PWM_CHANNEL_CTRL (Channel)),
    RP1_PWM_CHANNEL_DEFAULT
    );

  return EFI_SUCCESS;
}

STATIC
inline
EFI_STATUS
Rp1PwmReleaseChannel (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINT8                 Channel
  )
{
  if (!Rp1PwmChannelValid (Channel)) {
    return EFI_INVALID_PARAMETER;
  }

  Rp1MmioAndNot32 (
    Rp1PwmRegAddr (PwmBase, RP1_PWM_CHANNEL_CTRL (Channel)),
    RP1_PWM_MODE_MASK
    );
  Rp1PwmApplyUpdate (PwmBase);

  return EFI_SUCCESS;
}

STATIC
inline
EFI_STATUS
Rp1PwmSetRangeDuty (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINT8                 Channel,
  IN UINT32                Range,
  IN UINT32                Duty
  )
{
  if (!Rp1PwmChannelValid (Channel) || (Duty > Range)) {
    return EFI_INVALID_PARAMETER;
  }

  Rp1MmioWrite32 (Rp1PwmRegAddr (PwmBase, RP1_PWM_CHANNEL_DUTY (Channel)), Duty);
  Rp1MmioWrite32 (
    Rp1PwmRegAddr (PwmBase, RP1_PWM_CHANNEL_RANGE (Channel)),
    Range
    );

  return EFI_SUCCESS;
}

STATIC
inline
EFI_STATUS
Rp1PwmSetPolarity (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINT8                 Channel,
  IN BOOLEAN               Inverted
  )
{
  if (!Rp1PwmChannelValid (Channel)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Inverted) {
    Rp1MmioOr32 (
      Rp1PwmRegAddr (PwmBase, RP1_PWM_CHANNEL_CTRL (Channel)),
      RP1_PWM_POLARITY
      );
  } else {
    Rp1MmioAndNot32 (
      Rp1PwmRegAddr (PwmBase, RP1_PWM_CHANNEL_CTRL (Channel)),
      RP1_PWM_POLARITY
      );
  }

  return EFI_SUCCESS;
}

STATIC
inline
EFI_STATUS
Rp1PwmEnable (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINT8                 Channel,
  IN BOOLEAN               Enable
  )
{
  if (!Rp1PwmChannelValid (Channel)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Enable) {
    Rp1MmioOr32 (
      Rp1PwmRegAddr (PwmBase, RP1_PWM_GLOBAL_CTRL),
      RP1_PWM_CHANNEL_ENABLE (Channel)
      );
  } else {
    Rp1MmioAndNot32 (
      Rp1PwmRegAddr (PwmBase, RP1_PWM_GLOBAL_CTRL),
      RP1_PWM_CHANNEL_ENABLE (Channel)
      );
  }

  Rp1PwmApplyUpdate (PwmBase);

  return EFI_SUCCESS;
}

STATIC
inline
EFI_STATUS
Rp1PwmConfigure (
  IN EFI_PHYSICAL_ADDRESS  PwmBase,
  IN UINT8                 Channel,
  IN UINT32                Range,
  IN UINT32                Duty,
  IN BOOLEAN               Inverted,
  IN BOOLEAN               Enable
  )
{
  EFI_STATUS  Status;

  Status = Rp1PwmRequestChannel (PwmBase, Channel);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Rp1PwmSetRangeDuty (PwmBase, Channel, Range, Duty);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Rp1PwmSetPolarity (PwmBase, Channel, Inverted);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Rp1PwmEnable (PwmBase, Channel, Enable);
}

#endif // __RP1_PWM_H__
