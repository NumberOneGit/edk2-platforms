/** @file
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_BUS_H__
#define __RP1_BUS_H__

#include <Protocol/PciIo.h>

#define RP1_BUS_PROTOCOL_GUID                                                  \
  {                                                                            \
    0xf1417a30, 0x5418, 0x4cd5, {                                              \
      0x8e, 0x65, 0xf9, 0x02, 0x51, 0x21, 0xb5, 0x7f                           \
    }                                                                          \
  }

typedef struct _RP1_BUS_PROTOCOL RP1_BUS_PROTOCOL;

typedef EFI_PHYSICAL_ADDRESS(EFIAPI *RP1_BUS_GET_PERIPHERAL_BASE)(
    IN RP1_BUS_PROTOCOL *This);

typedef EFI_STATUS(EFIAPI *RP1_BUS_DMA_MAP)(
    IN RP1_BUS_PROTOCOL *This, IN EFI_PCI_IO_PROTOCOL_OPERATION Operation,
    IN VOID *HostAddress, IN OUT UINTN *NumberOfBytes,
    OUT EFI_PHYSICAL_ADDRESS *DeviceAddress, OUT VOID **Mapping);

typedef EFI_STATUS(EFIAPI *RP1_BUS_DMA_UNMAP)(IN RP1_BUS_PROTOCOL *This,
                                              IN VOID *Mapping);

typedef EFI_STATUS(EFIAPI *RP1_BUS_DMA_ALLOCATE_BUFFER)(
    IN RP1_BUS_PROTOCOL *This, IN EFI_MEMORY_TYPE MemoryType, IN UINTN Pages,
    OUT VOID **HostAddress, IN UINT64 Attributes);

typedef EFI_STATUS(EFIAPI *RP1_BUS_DMA_FREE_BUFFER)(IN RP1_BUS_PROTOCOL *This,
                                                    IN UINTN Pages,
                                                    IN VOID *HostAddress);

struct _RP1_BUS_PROTOCOL {
  RP1_BUS_GET_PERIPHERAL_BASE GetPeripheralBase;
  RP1_BUS_DMA_MAP DmaMap;
  RP1_BUS_DMA_UNMAP DmaUnmap;
  RP1_BUS_DMA_ALLOCATE_BUFFER DmaAllocateBuffer;
  RP1_BUS_DMA_FREE_BUFFER DmaFreeBuffer;
};

extern EFI_GUID gRp1BusProtocolGuid;

#endif // __RP1_BUS_H__
