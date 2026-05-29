/** @file
 *
 *  BCM2712 DWC2 USB gadget controller driver (Pi 5 fork).
 *  Scaffolding only: dr_mode gate + FIFO config read + diagnostics.
 *  UDC implementation lands in subsequent commits.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BoardInfoLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "DwUsbGadgetDxe.h"

EFI_STATUS
EFIAPI
DwUsbGadgetEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  USB_DR_MODE             Mode;
  USB_GADGET_FIFO_CONFIG  Fifo;
  CONST CHAR8             *ModeStr;
  UINTN                   Idx;

  Mode = BoardInfoGetUsbDrMode ();
  switch (Mode) {
    case UsbDrModeHost:       ModeStr = "host";       break;
    case UsbDrModePeripheral: ModeStr = "peripheral"; break;
    case UsbDrModeOtg:        ModeStr = "otg";        break;
    default:                  ModeStr = "unset";      break;
  }

  if ((Mode != UsbDrModePeripheral) && (Mode != UsbDrModeOtg)) {
    DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: dr_mode=%a, skipping gadget driver load\n", ModeStr));
    return EFI_UNSUPPORTED;
  }

  DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: dr_mode=%a, loading gadget driver\n", ModeStr));
  DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: controller base = 0x%lx\n",
          (UINT64)PI5_BCM2712_USB_BASE_ADDRESS));

  BoardInfoGetUsbGadgetFifo (&Fifo);
  DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: g-rx-fifo-size    = %u\n", Fifo.RxFifoSize));
  DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: g-np-tx-fifo-size = %u\n", Fifo.NpTxFifoSize));
  DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: g-tx-fifo-size    = %u entries\n",
          (UINT32)Fifo.TxFifoCount));
  for (Idx = 0; Idx < Fifo.TxFifoCount; Idx++) {
    DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe:   tx[%u] = %u\n", (UINT32)Idx, Fifo.TxFifoSize[Idx]));
  }
  DEBUG ((DEBUG_INFO, "DwUsbGadgetDxe: disable-over-current = %a\n",
          Fifo.DisableOverCurrent ? "TRUE" : "FALSE"));

  return EFI_UNSUPPORTED;
}
