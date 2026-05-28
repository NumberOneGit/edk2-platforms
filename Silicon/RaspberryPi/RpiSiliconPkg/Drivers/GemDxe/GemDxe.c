/** @file
 *
 *  Copyright (c) 2026
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "GemDxe.h"

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/RpiFirmware.h>
#include <Protocol/Rp1Bus.h>
#include <Rp1.h>
#include <Rp1Gpio.h>
#include <Rp1Mmio.h>

//
// =============================================================================
// COPY/PASTE REQUIRED: GemDxe.inf must declare the EBS event GUID. Add this
// section (or merge into an existing [Guids] section if you later add one):
//
//     [Guids]
//       gEfiEventExitBootServicesGuid    ## CONSUMES
//
// Without this declaration the build links cleanly but the GUID symbol may
// not be exported, and CreateEventEx will return EFI_INVALID_PARAMETER.
// =============================================================================
//

//
// Single GEM controller on Pi 5; one EBS event covers it. We also stash the
// Private pointer at file scope so the EBS handler — which has no caller-
// supplied context after the event fires — can reach the GEM registers.
//
STATIC EFI_EVENT mGemExitBootServicesEvent = NULL;
STATIC GEM_DXE_PRIVATE_DATA *mGemExitBootServicesPrivate = NULL;

STATIC EFI_GUID mGemDxePrivateGuid = {
    0x8bf91d24,
    0x93d5,
    0x42bb,
    {0xb4, 0x45, 0x30, 0x45, 0x34, 0xda, 0x56, 0x37}};

#define GEM_RP1_AMP_MAX_PIPE 8

#define GEM_RP1_PHY_ADDR_DEFAULT 1
#define GEM_RP1_PHY_ADDR_CM5 0
#define GEM_RP1_PHY_IRQ_GPIO_CM5 37

//
// Compute Module 5 (board type 0x18) and CM5 Lite (0x1A) share the same RP1
// GEM wiring: PHY at MDIO addr 0 and ETH_IRQ_N routed to RP1 GPIO37. All other
// Pi 5 family boards (Pi 5 Model B 0x17, Pi 500 0x19) use PHY addr 1 and have
// no IRQ wired. PcdBoardType is published by RpiPlatformDxe from the FDT-
// sourced revision code.
//
STATIC
BOOLEAN
IsCm5FamilyBoard (
  VOID
  )
{
  UINT8 BoardType = PcdGet8 (PcdBoardType);
  return (BoardType == 0x18) || (BoardType == 0x1A);
}
#define GEM_MDIO_TIMEOUT_US 1000000
#define GEM_PHY_REG_BMCR 0
#define GEM_PHY_REG_BMSR 1
#define GEM_PHY_REG_ID1 2
#define GEM_PHY_REG_ID2 3
#define GEM_PHY_REG_ADVERTISE 4
#define GEM_PHY_REG_LPA 5
#define GEM_PHY_REG_STAT1000 10
#define GEM_PHY_BMCR_ANRESTART BIT9
#define GEM_PHY_BMCR_ANENABLE BIT12
#define GEM_PHY_BMSR_LSTATUS BIT2
#define GEM_PHY_BMSR_ANEGCOMPLETE BIT5
#define GEM_PHY_ADVERTISE_CSMA BIT0
#define GEM_PHY_ADVERTISE_10HALF BIT5
#define GEM_PHY_ADVERTISE_10FULL BIT6
#define GEM_PHY_ADVERTISE_100HALF BIT7
#define GEM_PHY_ADVERTISE_100FULL BIT8
#define GEM_PHY_LPA_10HALF BIT5
#define GEM_PHY_LPA_10FULL BIT6
#define GEM_PHY_LPA_100HALF BIT7
#define GEM_PHY_LPA_100FULL BIT8
#define GEM_PHY_STAT1000_1000HALF BIT10
#define GEM_PHY_STAT1000_1000FULL BIT11
//
// MMD (Clause-45-over-Clause-22 indirect) access. Used by GemDisableEeeAdv
// to clear EEE_ADV in MMD7.60 because bcm2712-rpi-5-b.dts marks
// eee-broken-1000t / eee-broken-100tx on the BCM54213PE; Linux's PHY core
// honours those DT properties and clears the EEE advertisement.
//
#define GEM_PHY_REG_MMD_CTRL      13     // selects MMD device + op
#define GEM_PHY_REG_MMD_DATA      14     // data register for the selected MMD reg
#define GEM_MMD_CTRL_OP_ADDR      0x0000 // function code 0: address mode
#define GEM_MMD_CTRL_OP_DATA      0x4000 // function code 1: data, no post-incr
#define GEM_MMD_DEV_AN            7      // MMD device 7 = Auto-Negotiation
#define GEM_MMD_AN_EEE_ADV        60     // MMD7 reg 60 = EEE advertisement
#define GEM_EEE_ADV_100TX         BIT1   // 100BASE-TX EEE advertisement bit
#define GEM_EEE_ADV_1000T         BIT2   // 1000BASE-T EEE advertisement bit
#define GEM_PHY_AUTONEG_TIMEOUT_US 5000000
#define GEM_PHY_AUTONEG_POLL_US 100000
#define GEM_RP1_PHY_RESET_GPIO 32
#define GEM_RP1_PHY_RESET_ASSERT_MS 5
#define GEM_RP1_PHY_RESET_RELEASE_MS 5
#define GEM_DMA_PROBE_SIZE EFI_PAGE_SIZE
#define GEM_DMA_DESC_RX_USED BIT0
#define GEM_DMA_DESC_RX_WRAP BIT1
#define GEM_DMA_DESC_RX_ADDR_MASK 0xFFFFFFFCu
#define GEM_DMA_DESC_RX_LENGTH_MASK 0x00001FFFu
#define GEM_DMA_DESC_TX_LAST BIT15
#define GEM_DMA_DESC_TX_WRAP BIT30
#define GEM_DMA_DESC_TX_USED BIT31
#define GEM_TX_MIN_FRAME_SIZE 60
#define GEM_TX_MAX_FRAME_SIZE 1536
#define GEM_DMA_DESC_TX_LENGTH_MASK 0x00003FFFu
#define GEM_TSR_COMP BIT5
#define GEM_DMACFG_RP1_RX_BUFFER_SIZE_VALUE                               \
  (GEM_DMA_RX_BUFFER_SIZE / 64)
#define GEM_DMACFG_RP1_BURST_LENGTH 16
#define GEM_INTERRUPT_DISABLE_ALL 0x3FFFFFFF
#define GEM_SNP_RX_FILTER_MASK                                                \
  (EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST | \
   EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST |                                      \
   EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS |                                    \
   EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)
#define GEM_SNP_MAX_MCAST_FILTER_COUNT 16

//
// Register Access Functions
//

/**
  Read a GEM register.

  @param  Private[in]  Pointer to GEM_DXE_PRIVATE_DATA.
  @param  Offset[in]   Register offset from GEM base.

  @retval Value read from register.

**/
UINT32
GemMmioRead(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset) {
  ASSERT((Offset & 3) == 0);

  return MmioRead32(Private->GemBase + Offset);
}

/**
  Write a GEM register.

  @param  Private[in]  Pointer to GEM_DXE_PRIVATE_DATA.
  @param  Offset[in]   Register offset from GEM base.
  @param  Value[in]    Value to write.

**/
VOID GemMmioWrite(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset,
                  IN UINT32 Value) {
  ASSERT((Offset & 3) == 0);

  MemoryFence();
  MmioWrite32(Private->GemBase + Offset, Value);
}

/**
  Set bits in a GEM register with debug logging.

  @param  Private[in]  Pointer to GEM_DXE_PRIVATE_DATA.
  @param  Offset[in]   Register offset from GEM base.
  @param  SetBits[in]  Bits to set.

  @retval Value after setting bits.

**/
UINT32
GemMmioSetBits(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset,
               IN UINT32 SetBits) {
  UINT32 Value;
  UINT32 OldValue;

  OldValue = GemMmioRead(Private, Offset);
  Value = OldValue | SetBits;
  GemMmioWrite(Private, Offset, Value);

  return Value;
}

/**
  Clear bits in a GEM register with debug logging.

  @param  Private[in]   Pointer to GEM_DXE_PRIVATE_DATA.
  @param  Offset[in]    Register offset from GEM base.
  @param  ClearBits[in] Bits to clear.

  @retval Value after clearing bits.

**/
UINT32
GemMmioClearBits(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset,
                 IN UINT32 ClearBits) {
  UINT32 Value;
  UINT32 OldValue;

  OldValue = GemMmioRead(Private, Offset);
  Value = OldValue & ~ClearBits;
  GemMmioWrite(Private, Offset, Value);

  return Value;
}

//
// PCIe posted-write flush. RP1 sits behind the BCM2712 PCIe root complex,
// so CPU stores to GEM registers are posted writes that can linger in the
// bridge's queue. A read from any GEM register forces prior posted writes
// from the same path to complete before the read returns. Linux's macb
// driver applies this specifically after TSTART because TX has been
// observed to stall on RP1 without it; we apply it at all doorbell,
// queue-base, and enable/disable sites for the same reason.
//
VOID
GemPciePostedWriteFlush(IN GEM_DXE_PRIVATE_DATA *Private) {
  (VOID)MmioRead32(Private->GemBase + GEM_NCR);
}

STATIC
EFI_STATUS
GemProbeRp1DmaTranslation(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_STATUS Status;
  VOID *HostAddress;
  VOID *Mapping;
  EFI_PHYSICAL_ADDRESS DeviceAddress;
  UINTN NumberOfBytes;

  HostAddress = NULL;
  Mapping = NULL;
  DeviceAddress = 0;
  NumberOfBytes = GEM_DMA_PROBE_SIZE;

  Status = Private->Rp1Bus->DmaAllocateBuffer(
      Private->Rp1Bus, EfiBootServicesData,
      EFI_SIZE_TO_PAGES(GEM_DMA_PROBE_SIZE), &HostAddress, 0);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: DMA probe AllocateBuffer failed. Status=%r\n",
           Status));
    return Status;
  }

  Status = Private->Rp1Bus->DmaMap(
      Private->Rp1Bus, EfiPciIoOperationBusMasterCommonBuffer, HostAddress,
      &NumberOfBytes, &DeviceAddress, &Mapping);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: DMA probe Map failed. Status=%r\n", Status));
    goto FreeBuffer;
  }

  Status = Private->Rp1Bus->DmaUnmap(Private->Rp1Bus, Mapping);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: DMA probe Unmap failed. Status=%r\n", Status));
    goto FreeBuffer;
  }

  Mapping = NULL;

FreeBuffer:
  if (Mapping != NULL) {
    Private->Rp1Bus->DmaUnmap(Private->Rp1Bus, Mapping);
  }

  Private->Rp1Bus->DmaFreeBuffer(
      Private->Rp1Bus, EFI_SIZE_TO_PAGES(GEM_DMA_PROBE_SIZE), HostAddress);

  return Status;
}

STATIC
VOID
GemZeroAllocation(IN GEM_DMA_ALLOCATION *Allocation) {
  Allocation->HostAddress = NULL;
  Allocation->Mapping = NULL;
  Allocation->DeviceAddress = 0;
  Allocation->NumberOfBytes = 0;
  Allocation->Pages = 0;
}

STATIC
EFI_STATUS
GemDmaAllocateAndMap(IN GEM_DXE_PRIVATE_DATA *Private, IN UINTN Pages,
                     OUT GEM_DMA_ALLOCATION *Allocation) {
  EFI_STATUS Status;

  GemZeroAllocation(Allocation);
  Allocation->Pages = Pages;
  Allocation->NumberOfBytes = EFI_PAGES_TO_SIZE(Pages);

  Status = Private->Rp1Bus->DmaAllocateBuffer(
      Private->Rp1Bus, EfiBootServicesData, Pages, &Allocation->HostAddress,
      0);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: DMA AllocateBuffer(%u pages) failed. Status=%r\n",
           (UINT32)Pages, Status));
    return Status;
  }

  Status = Private->Rp1Bus->DmaMap(
      Private->Rp1Bus, EfiPciIoOperationBusMasterCommonBuffer,
      Allocation->HostAddress, &Allocation->NumberOfBytes,
      &Allocation->DeviceAddress, &Allocation->Mapping);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: DMA Map(%p, 0x%lx bytes) failed. Status=%r\n",
           Allocation->HostAddress, Allocation->NumberOfBytes, Status));
    Private->Rp1Bus->DmaFreeBuffer(Private->Rp1Bus, Pages,
                                   Allocation->HostAddress);
    GemZeroAllocation(Allocation);
  }

  return Status;
}

STATIC
VOID
GemDmaUnmapAndFree(IN GEM_DXE_PRIVATE_DATA *Private,
                   IN GEM_DMA_ALLOCATION *Allocation) {
  if (Allocation->Mapping != NULL) {
    Private->Rp1Bus->DmaUnmap(Private->Rp1Bus, Allocation->Mapping);
  }

  if (Allocation->HostAddress != NULL) {
    Private->Rp1Bus->DmaFreeBuffer(Private->Rp1Bus, Allocation->Pages,
                                   Allocation->HostAddress);
  }

  GemZeroAllocation(Allocation);
}

STATIC
BOOLEAN
GemDmaAddressFits32(IN EFI_PHYSICAL_ADDRESS DeviceAddress) {
  return (DeviceAddress <= MAX_UINT32);
}

STATIC
VOID
GemConfigureDmaForBasicDescriptors(IN GEM_DXE_PRIVATE_DATA *Private) {
  UINT32 DmaCfg;
  UINT32 NewDmaCfg;

  DmaCfg = GemMmioRead(Private, GEM_DMACFG);
  NewDmaCfg = DmaCfg;
  NewDmaCfg &= ~(GEM_DMACFG_ADDR64 | GEM_DMACFG_TXEXT | GEM_DMACFG_RXEXT |
                 (GEM_DMACFG_RXBS_MASK << GEM_DMACFG_RXBS_OFFSET) |
                 (GEM_DMACFG_FBLDO_MASK << GEM_DMACFG_FBLDO_OFFSET));
  NewDmaCfg |= (GEM_DMACFG_RP1_RX_BUFFER_SIZE_VALUE & GEM_DMACFG_RXBS_MASK)
               << GEM_DMACFG_RXBS_OFFSET;
  NewDmaCfg |= (GEM_DMACFG_RP1_BURST_LENGTH & GEM_DMACFG_FBLDO_MASK)
               << GEM_DMACFG_FBLDO_OFFSET;
  NewDmaCfg |= GEM_DMACFG_TXPBMS |
               (GEM_DMACFG_RXBMS_MASK << GEM_DMACFG_RXBMS_OFFSET);

  GemMmioWrite(Private, GEM_DMACFG, NewDmaCfg);
  GemPciePostedWriteFlush(Private);
}

STATIC
VOID
GemReleaseDmaRings(IN GEM_DXE_PRIVATE_DATA *Private) {
  UINTN Index;

  if (Private == NULL) {
    return;
  }

  GemMmioWrite(Private, GEM_RBQP, 0);
  GemMmioWrite(Private, GEM_TBQP, 0);
  GemPciePostedWriteFlush(Private);

  for (Index = 0; Index < GEM_DMA_RX_DESC_COUNT; Index++) {
    GemDmaUnmapAndFree(Private, &Private->RxBuffers[Index]);
  }

  GemDmaUnmapAndFree(Private, &Private->TxRing);
  GemDmaUnmapAndFree(Private, &Private->RxRing);

  Private->RxDesc = NULL;
  Private->TxDesc = NULL;
  Private->RxIndex = 0;
  Private->TxIndex = 0;
  Private->DmaRingsInitialized = FALSE;
}

STATIC
EFI_STATUS
GemInitializeDmaRings(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_STATUS Status;
  UINTN Index;

  GemZeroAllocation(&Private->RxRing);
  GemZeroAllocation(&Private->TxRing);
  for (Index = 0; Index < GEM_DMA_RX_DESC_COUNT; Index++) {
    GemZeroAllocation(&Private->RxBuffers[Index]);
  }
  Private->RxDesc = NULL;
  Private->TxDesc = NULL;
  Private->DmaRingsInitialized = FALSE;

  Status = GemDmaAllocateAndMap(Private, GEM_DMA_RING_PAGES, &Private->RxRing);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  Status = GemDmaAllocateAndMap(Private, GEM_DMA_RING_PAGES, &Private->TxRing);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  if (!GemDmaAddressFits32(Private->RxRing.DeviceAddress) ||
      !GemDmaAddressFits32(Private->TxRing.DeviceAddress)) {
    DEBUG((DEBUG_ERROR,
           "GEM: DMA ring address exceeds 32-bit range "
           "(RX=0x%lx TX=0x%lx); aborting basic descriptor probe\n",
           Private->RxRing.DeviceAddress, Private->TxRing.DeviceAddress));
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  Private->RxDesc = (GEM_DMA_DESC *)Private->RxRing.HostAddress;
  Private->TxDesc = (GEM_DMA_DESC *)Private->TxRing.HostAddress;

  for (Index = 0; Index < GEM_DMA_RX_DESC_COUNT; Index++) {
    Status = GemDmaAllocateAndMap(Private, GEM_DMA_RX_BUFFER_PAGES,
                                  &Private->RxBuffers[Index]);
    if (EFI_ERROR(Status)) {
      goto Exit;
    }

    if (!GemDmaAddressFits32(Private->RxBuffers[Index].DeviceAddress)) {
      DEBUG((DEBUG_ERROR,
             "GEM: RX buffer %u device address exceeds 32-bit range: 0x%lx\n",
             (UINT32)Index, Private->RxBuffers[Index].DeviceAddress));
      Status = EFI_UNSUPPORTED;
      goto Exit;
    }

    Private->RxDesc[Index].Ctrl = 0;
    Private->RxDesc[Index].Addr =
        ((UINT32)Private->RxBuffers[Index].DeviceAddress &
         GEM_DMA_DESC_RX_ADDR_MASK);
    if (Index == (GEM_DMA_RX_DESC_COUNT - 1)) {
      Private->RxDesc[Index].Addr |= GEM_DMA_DESC_RX_WRAP;
    }
  }

  for (Index = 0; Index < GEM_DMA_TX_DESC_COUNT; Index++) {
    Private->TxDesc[Index].Addr = 0;
    Private->TxDesc[Index].Ctrl = GEM_DMA_DESC_TX_USED;
    if (Index == (GEM_DMA_TX_DESC_COUNT - 1)) {
      Private->TxDesc[Index].Ctrl |= GEM_DMA_DESC_TX_WRAP;
    }
  }

  MemoryFence();

  GemMmioWrite(Private, GEM_IDR, GEM_INTERRUPT_DISABLE_ALL);
  GemConfigureDmaForBasicDescriptors(Private);

  GemMmioWrite(Private, GEM_RBQP, (UINT32)Private->RxRing.DeviceAddress);
  GemMmioWrite(Private, GEM_TBQP, (UINT32)Private->TxRing.DeviceAddress);
  GemPciePostedWriteFlush(Private);

  Private->DmaRingsInitialized = TRUE;
  Status = EFI_SUCCESS;

Exit:
  if (EFI_ERROR(Status)) {
    GemReleaseDmaRings(Private);
  }

  DEBUG((DEBUG_INFO, "GEM: DMA rings ready. Status=%r\n", Status));
  return Status;
}

STATIC
EFI_STATUS
GemTransmitFrameSync(IN GEM_DXE_PRIVATE_DATA *Private,
                     IN EFI_PHYSICAL_ADDRESS DeviceAddress,
                     IN UINTN FrameLength) {
  EFI_STATUS Status;
  UINT32 Ctrl;
  UINT32 DescIndex;
  UINT32 Ncr;
  UINT32 Wrap;
  UINTN Poll;

  if (!Private->DmaRingsInitialized) {
    return EFI_NOT_READY;
  }

  if ((FrameLength == 0) || (FrameLength > GEM_DMA_DESC_TX_LENGTH_MASK) ||
      !GemDmaAddressFits32(DeviceAddress)) {
    return EFI_INVALID_PARAMETER;
  }

  DescIndex = (UINT32)Private->TxIndex;
  Ctrl = Private->TxDesc[DescIndex].Ctrl;
  if ((Ctrl & GEM_DMA_DESC_TX_USED) == 0) {
    return EFI_NOT_READY;
  }

  Wrap = Ctrl & GEM_DMA_DESC_TX_WRAP;
  Private->TxDesc[DescIndex].Addr = (UINT32)DeviceAddress;
  Private->TxDesc[DescIndex].Ctrl =
      Wrap | GEM_DMA_DESC_TX_LAST | ((UINT32)FrameLength &
                                      GEM_DMA_DESC_TX_LENGTH_MASK);
  MemoryFence();

  GemMmioWrite(Private, GEM_TSR, 0xFFFFFFFF);
  Ncr = GemMmioRead(Private, GEM_NCR);
  GemMmioWrite(Private, GEM_NCR, Ncr | GEM_NCR_TXEN);
  GemMmioWrite(Private, GEM_NCR, Ncr | GEM_NCR_TXEN | GEM_NCR_TSTART);
  //
  // Linux explicitly flushes the BCM2712 PCIe posted-write queue here
  // because RP1 TX has been observed to stall when TSTART lingers in
  // the bridge. See macb_main.c immediately after TSTART.
  //
  GemPciePostedWriteFlush(Private);

  Status = EFI_TIMEOUT;
  for (Poll = 0; Poll < 1000; Poll++) {
    MemoryFence();
    Ctrl = Private->TxDesc[DescIndex].Ctrl;
    if ((Ctrl & GEM_DMA_DESC_TX_USED) != 0) {
      Status = EFI_SUCCESS;
      break;
    }

    MicroSecondDelay(100);
  }

  if (!EFI_ERROR(Status)) {
    Private->TxIndex = (Private->TxIndex + 1) % GEM_DMA_TX_DESC_COUNT;
  }

  return Status;
}

STATIC
VOID
GemApplyRp1AxiPipelineConfig(IN GEM_DXE_PRIVATE_DATA *Private);

STATIC
EFI_STATUS
GemProbeMdio(IN GEM_DXE_PRIVATE_DATA *Private, OUT UINT32 *NcfgrSpeedBits,
             OUT BOOLEAN *LinkConfigValid);

STATIC
EFI_STATUS
GemInitializeDmaRings(IN GEM_DXE_PRIVATE_DATA *Private);

STATIC
VOID
GemReleaseDmaRings(IN GEM_DXE_PRIVATE_DATA *Private);

STATIC
EFI_STATUS
GemConfigureRp1PhyIrqGpio(IN GEM_DXE_PRIVATE_DATA *Private);

STATIC
EFI_STATUS
GemResetRp1Phy(IN GEM_DXE_PRIVATE_DATA *Private);

//
// Hardware Initialization Functions
//

/**
  Put GEM hardware in a quiet state.

  @param  Private[in]  Pointer to GEM_DXE_PRIVATE_DATA.

  @retval EFI_SUCCESS  Hardware quiesced successfully.

**/
EFI_STATUS
GemResetHardware(IN GEM_DXE_PRIVATE_DATA *Private) {
  GemMmioClearBits(Private, GEM_NCR, GEM_NCR_TXEN | GEM_NCR_RXEN);
  GemMmioSetBits(Private, GEM_NCR, GEM_NCR_CLRSTAT);
  GemPciePostedWriteFlush(Private);

  return EFI_SUCCESS;
}

/**
  Initialize GEM hardware with basic configuration.

  @param  Private[in]  Pointer to GEM_DXE_PRIVATE_DATA.

  @retval EFI_SUCCESS  Hardware initialized successfully.

**/
EFI_STATUS
GemHardwareInit(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_STATUS Status;
  UINT32 Value;
  UINT32 Ncfgr;
  UINT32 NcfgrSpeedBits;
  BOOLEAN LinkConfigValid;

  GemApplyRp1AxiPipelineConfig(Private);

  GemConfigureRp1PhyIrqGpio(Private);

  GemResetRp1Phy(Private);

  NcfgrSpeedBits = 0;
  LinkConfigValid = FALSE;
  Status = GemProbeMdio(Private, &NcfgrSpeedBits, &LinkConfigValid);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "GEM: Continuing after MDIO probe failure. Status=%r\n",
           Status));
  }
  Private->MediaPresent = LinkConfigValid;

  if (LinkConfigValid) {
    //
    // NCFGR.DRFCS strips the 4-byte FCS before delivering RX frames; without
    // it the descriptor length includes FCS and our SNP CopyMem consumer
    // delivers 4 trailing garbage bytes to the IP stack.
    //
    Ncfgr = GemMmioRead(Private, GEM_NCFGR);
    Value = (Ncfgr & ~(GEM_NCFGR_SPD | GEM_NCFGR_FD | GEM_NCFGR_GBE)) |
            NcfgrSpeedBits | GEM_NCFGR_DRFCS;
    GemMmioWrite(Private, GEM_NCFGR, Value);
  }

  Status = GemInitializeDmaRings(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "GEM: DMA ring init failed. Status=%r\n", Status));
    return EFI_SUCCESS;
  }

  return EFI_SUCCESS;
}

STATIC
UINT32
GemMdioBuildClause22Frame(IN UINT8 Phy, IN UINT8 Reg, IN UINT16 Data,
                          IN UINT32 Operation) {
  return ((GEM_MAN_C22_SOF & GEM_MAN_SOF_MASK) << GEM_MAN_SOF_OFFSET) |
         ((Operation & GEM_MAN_RW_MASK) << GEM_MAN_RW_OFFSET) |
         (((UINT32)Phy & GEM_MAN_PHY_MASK) << GEM_MAN_PHY_OFFSET) |
         (((UINT32)Reg & GEM_MAN_REG_MASK) << GEM_MAN_REG_OFFSET) |
         ((GEM_MAN_C22_CODE & GEM_MAN_CODE_MASK) << GEM_MAN_CODE_OFFSET) |
         ((UINT32)Data & GEM_MAN_DATA_MASK);
}

STATIC
EFI_STATUS
GemMdioWaitIdle(IN GEM_DXE_PRIVATE_DATA *Private) {
  UINTN Index;
  UINT32 Nsr;

  for (Index = 0; Index < GEM_MDIO_TIMEOUT_US; Index++) {
    Nsr = MmioRead32(Private->GemBase + GEM_NSR);
    if ((Nsr & GEM_NSR_IDLE) != 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay(1);
  }

  DEBUG((DEBUG_ERROR, "GEM: MDIO timeout waiting for NSR.IDLE\n"));
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
GemMdioReadClause22(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT8 Phy,
                    IN UINT8 Reg, OUT UINT16 *Value) {
  EFI_STATUS Status;
  UINT32 Frame;
  UINT32 Ncr;
  BOOLEAN RestoreMpe;

  Ncr = GemMmioRead(Private, GEM_NCR);
  RestoreMpe = (BOOLEAN)((Ncr & GEM_NCR_MPE) == 0);
  if (RestoreMpe) {
    GemMmioWrite(Private, GEM_NCR, Ncr | GEM_NCR_MPE);
  }

  Status = GemMdioWaitIdle(Private);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  Frame = GemMdioBuildClause22Frame(Phy, Reg, 0, GEM_MAN_C22_READ);
  GemMmioWrite(Private, GEM_MAN, Frame);

  Status = GemMdioWaitIdle(Private);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  Frame = GemMmioRead(Private, GEM_MAN);
  *Value = (UINT16)(Frame & GEM_MAN_DATA_MASK);

Exit:
  if (RestoreMpe) {
    GemMmioWrite(Private, GEM_NCR, Ncr);
  }

  return Status;
}

STATIC
EFI_STATUS
GemMdioWriteClause22(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT8 Phy,
                     IN UINT8 Reg, IN UINT16 Value) {
  EFI_STATUS Status;
  UINT32 Frame;
  UINT32 Ncr;
  BOOLEAN RestoreMpe;

  Ncr = GemMmioRead(Private, GEM_NCR);
  RestoreMpe = (BOOLEAN)((Ncr & GEM_NCR_MPE) == 0);
  if (RestoreMpe) {
    GemMmioWrite(Private, GEM_NCR, Ncr | GEM_NCR_MPE);
  }

  Status = GemMdioWaitIdle(Private);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  Frame = GemMdioBuildClause22Frame(Phy, Reg, Value, GEM_MAN_C22_WRITE);
  GemMmioWrite(Private, GEM_MAN, Frame);

  Status = GemMdioWaitIdle(Private);

Exit:
  if (RestoreMpe) {
    GemMmioWrite(Private, GEM_NCR, Ncr);
  }

  return Status;
}

STATIC
EFI_STATUS
GemMdioReadClause22Quiet(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT8 Phy,
                         IN UINT8 Reg, OUT UINT16 *Value) {
  EFI_STATUS Status;
  UINT32 Frame;
  UINT32 Ncr;
  BOOLEAN RestoreMpe;

  Ncr = MmioRead32(Private->GemBase + GEM_NCR);
  RestoreMpe = (BOOLEAN)((Ncr & GEM_NCR_MPE) == 0);
  if (RestoreMpe) {
    MmioWrite32(Private->GemBase + GEM_NCR, Ncr | GEM_NCR_MPE);
  }

  Status = GemMdioWaitIdle(Private);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  Frame = GemMdioBuildClause22Frame(Phy, Reg, 0, GEM_MAN_C22_READ);
  MmioWrite32(Private->GemBase + GEM_MAN, Frame);

  Status = GemMdioWaitIdle(Private);
  if (EFI_ERROR(Status)) {
    goto Exit;
  }

  Frame = MmioRead32(Private->GemBase + GEM_MAN);
  *Value = (UINT16)(Frame & GEM_MAN_DATA_MASK);

Exit:
  if (RestoreMpe) {
    MmioWrite32(Private->GemBase + GEM_NCR, Ncr);
  }

  return Status;
}

//
// GemDisableEeeAdv -- clear the IEEE 802.3az Energy Efficient Ethernet
// advertisement bits for 100BASE-TX and 1000BASE-T in the PHY's MMD7
// register 60 (EEE_ADV). Required because bcm2712-rpi-5-b.dts marks
// `eee-broken-1000t` and `eee-broken-100tx` on the BCM54213PE: the
// PHY's EEE LPI exit is broken on this silicon. Linux's PHY core
// reads those DT properties and clears these bits before autoneg;
// xt-uboot does not, so we mirror Linux explicitly.
//
// Access path: Clause-22 indirect MDIO to MMD7 via PHY regs 13/14:
//   1) MMD_CTRL = OP_ADDR | DEV_AN      -> selects MMD7, address mode
//   2) MMD_DATA = 60                    -> selects EEE_ADV register
//   3) MMD_CTRL = OP_DATA | DEV_AN      -> switches to data mode
//   4) read  MMD_DATA                   -> reads EEE_ADV value
//   5) (write path) MMD_DATA = new      -> writes EEE_ADV new value
//
// Read-before-write: skip the write entirely if 100TX and 1000T are
// already 0. Pre-write DEBUG line lets a future reader confirm whether
// this function did anything across boots.
//
// MPE handling is implicit: every MDIO call here goes through
// GemMdioRead/WriteClause22, which save NCR, transiently enable MPE,
// do the transaction, and restore NCR on exit. No persistent MPE
// state change.
//
STATIC
EFI_STATUS
GemDisableEeeAdv(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT8 PhyAddr) {
  EFI_STATUS Status;

  //
  // Step 1: select MMD7 in address mode.
  //
  Status = GemMdioWriteClause22(Private, PhyAddr, GEM_PHY_REG_MMD_CTRL,
                                (UINT16)(GEM_MMD_CTRL_OP_ADDR | GEM_MMD_DEV_AN));
  if (EFI_ERROR(Status)) { return Status; }

  //
  // Step 2: write the MMD7 register address (60 = EEE_ADV).
  //
  Status = GemMdioWriteClause22(Private, PhyAddr, GEM_PHY_REG_MMD_DATA,
                                GEM_MMD_AN_EEE_ADV);
  if (EFI_ERROR(Status)) { return Status; }

  //
  // Step 3: switch to data mode (still MMD7).
  //
  Status = GemMdioWriteClause22(Private, PhyAddr, GEM_PHY_REG_MMD_CTRL,
                                (UINT16)(GEM_MMD_CTRL_OP_DATA | GEM_MMD_DEV_AN));
  if (EFI_ERROR(Status)) { return Status; }

  //
  // Step 4: write 0 to EEE_ADV. BCM54213PE boots with bits 1+2 (100TX,
  // 1000T) set; only those two bits are meaningful on this PHY. Other
  // PHYs that surface bits we'd want to preserve should re-introduce a
  // read-modify-write here.
  //
  return GemMdioWriteClause22(Private, PhyAddr, GEM_PHY_REG_MMD_DATA, 0);
}

STATIC
EFI_STATUS
GemRestartAutonegIfNeeded(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT8 PhyAddr,
                          IN UINT16 Bmsr) {
  EFI_STATUS Status;
  UINT16 Advertise;
  UINTN ElapsedUs;

  if (((Bmsr & GEM_PHY_BMSR_LSTATUS) != 0) &&
      ((Bmsr & GEM_PHY_BMSR_ANEGCOMPLETE) != 0)) {
    return EFI_SUCCESS;
  }

  Advertise = GEM_PHY_ADVERTISE_CSMA | GEM_PHY_ADVERTISE_10HALF |
              GEM_PHY_ADVERTISE_10FULL | GEM_PHY_ADVERTISE_100HALF |
              GEM_PHY_ADVERTISE_100FULL;

  DEBUG((DEBUG_INFO,
         "GEM: PHY link/autoneg not ready; restarting autonegotiation\n"));

  Status = GemMdioWriteClause22(Private, PhyAddr, GEM_PHY_REG_ADVERTISE,
                                Advertise);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO advertise write failed. Status=%r\n",
           Status));
    return Status;
  }

  Status = GemMdioWriteClause22(Private, PhyAddr, GEM_PHY_REG_BMCR,
                                GEM_PHY_BMCR_ANENABLE |
                                    GEM_PHY_BMCR_ANRESTART);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO BMCR autoneg restart failed. Status=%r\n",
           Status));
    return Status;
  }

  for (ElapsedUs = 0; ElapsedUs < GEM_PHY_AUTONEG_TIMEOUT_US;
       ElapsedUs += GEM_PHY_AUTONEG_POLL_US) {
    MicroSecondDelay(GEM_PHY_AUTONEG_POLL_US);

    Status = GemMdioReadClause22Quiet(Private, PhyAddr, GEM_PHY_REG_BMSR,
                                      &Bmsr);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    if (((Bmsr & GEM_PHY_BMSR_LSTATUS) != 0) &&
        ((Bmsr & GEM_PHY_BMSR_ANEGCOMPLETE) != 0)) {
      DEBUG((DEBUG_INFO,
             "GEM: PHY autoneg/link became ready after %u ms. BMSR=0x%04x\n",
             (UINT32)((ElapsedUs + GEM_PHY_AUTONEG_POLL_US) / 1000), Bmsr));
      return EFI_SUCCESS;
    }
  }

  DEBUG((DEBUG_WARN, "GEM: PHY autoneg wait timed out. BMSR=0x%04x\n", Bmsr));
  return EFI_TIMEOUT;
}

STATIC
UINT8
GemGetRp1PhyAddress(VOID) {
  UINT8 PhyAddr = IsCm5FamilyBoard()
                  ? GEM_RP1_PHY_ADDR_CM5
                  : GEM_RP1_PHY_ADDR_DEFAULT;
  DEBUG((DEBUG_INFO,
         "GEM: PcdBoardType=0x%02x -> PHY addr %u\n",
         PcdGet8(PcdBoardType), PhyAddr));
  return PhyAddr;
}

STATIC
VOID
GemDebugPrintRp1GpioState(IN CONST CHAR8 *Label, IN CONST RP1_GPIO_PIN *Pin) {
  DEBUG((DEBUG_INFO,
         "GEM: %a GPIO%u CTRL=0x%08x PAD=0x%08x RIO_IN=0x%08x\n", Label,
         GEM_RP1_PHY_IRQ_GPIO_CM5, Rp1GpioReadCtrl(Pin),
         Rp1MmioRead32(Rp1GpioPadAddr(Pin)),
         Rp1MmioRead32(Rp1GpioRioAddr(Pin, RP1_RIO_IN))));
}

STATIC
EFI_STATUS
GemConfigureRp1PhyIrqGpio(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_STATUS Status;
  RP1_GPIO_PIN Pin;

  if (!IsCm5FamilyBoard()) {
    return EFI_SUCCESS;
  }

  Status = Rp1GpioGetPin(Private->PeripheralBase, GEM_RP1_PHY_IRQ_GPIO_CM5,
                         &Pin);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to resolve CM5 ETH_IRQ_N GPIO37. Status=%r\n",
           Status));
    return Status;
  }

  DEBUG((DEBUG_INFO,
         "GEM: Configuring CM5 ETH_IRQ_N GPIO37 as input with pull-up\n"));
  GemDebugPrintRp1GpioState("ETH_IRQ_N before", &Pin);

  Rp1PadSetPull(&Pin, Rp1GpioPullUp);
  Rp1PadSetInputEnable(&Pin, TRUE);
  Rp1PadSetOutputEnable(&Pin, FALSE);
  Rp1GpioConfigureInput(&Pin);

  GemDebugPrintRp1GpioState("ETH_IRQ_N after ", &Pin);
  DEBUG((DEBUG_INFO,
         "GEM: CM5 ETH_IRQ_N level is %a; polling MDIO for now\n",
         Rp1GpioRead(&Pin) ? "high/inactive" : "low/asserted"));

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
GemLogPhyLink(IN UINT16 Bmsr, IN UINT16 Advertise, IN UINT16 Lpa,
              IN UINT16 Stat1000, OUT UINT32 *NcfgrSpeedBits) {
  CONST CHAR8 *Speed;
  CONST CHAR8 *Duplex;
  UINT16 Negotiated;
  UINT32 NcfgrCandidate;

  *NcfgrSpeedBits = 0;

  if ((Bmsr & GEM_PHY_BMSR_LSTATUS) == 0) {
    DEBUG((DEBUG_WARN, "GEM: PHY link is down. BMSR=0x%04x\n", Bmsr));
    return FALSE;
  }

  Speed = "10";
  Duplex = "half";
  NcfgrCandidate = 0;
  Negotiated = Advertise & Lpa;

  if ((Stat1000 & GEM_PHY_STAT1000_1000FULL) != 0) {
    Speed = "1000";
    Duplex = "full";
    NcfgrCandidate = GEM_NCFGR_GBE | GEM_NCFGR_FD;
  } else if ((Stat1000 & GEM_PHY_STAT1000_1000HALF) != 0) {
    Speed = "1000";
    NcfgrCandidate = GEM_NCFGR_GBE;
  } else if ((Negotiated & GEM_PHY_LPA_100FULL) != 0) {
    Speed = "100";
    Duplex = "full";
    NcfgrCandidate = GEM_NCFGR_SPD | GEM_NCFGR_FD;
  } else if ((Negotiated & GEM_PHY_LPA_100HALF) != 0) {
    Speed = "100";
    NcfgrCandidate = GEM_NCFGR_SPD;
  } else if ((Negotiated & GEM_PHY_LPA_10FULL) != 0) {
    Duplex = "full";
    NcfgrCandidate = GEM_NCFGR_FD;
  } else if ((Negotiated & GEM_PHY_LPA_10HALF) == 0) {
    DEBUG((DEBUG_WARN,
           "GEM: PHY link up but no common advertised mode. "
           "ADV=0x%04x LPA=0x%04x STAT1000=0x%04x\n",
           Advertise, Lpa, Stat1000));
    return FALSE;
  }

  *NcfgrSpeedBits = NcfgrCandidate;
  DEBUG((DEBUG_INFO,
         "GEM: PHY link up, %aMbps %a-duplex. "
         "ADV=0x%04x LPA=0x%04x STAT1000=0x%04x "
         "NCFGR speed candidate bits=0x%08x\n",
         Speed, Duplex, Advertise, Lpa, Stat1000, NcfgrCandidate));

  return TRUE;
}

STATIC UINT8 GemGetRp1PhyAddress(VOID);

STATIC
EFI_STATUS
GemProbeMdio(IN GEM_DXE_PRIVATE_DATA *Private, OUT UINT32 *NcfgrSpeedBits,
             OUT BOOLEAN *LinkConfigValid) {
  EFI_STATUS Status;
  UINT16 Advertise;
  UINT16 Bmcr;
  UINT16 Bmsr;
  UINT16 BmsrLatched;
  UINT16 Id1;
  UINT16 Id2;
  UINT16 Lpa;
  UINT16 Stat1000;
  UINT8 PhyAddr;

  *NcfgrSpeedBits = 0;
  *LinkConfigValid = FALSE;
  PhyAddr = GemGetRp1PhyAddress();

  DEBUG((DEBUG_INFO, "GEM: Probing MDIO PHY address %u for board type 0x%02x\n",
         PhyAddr, PcdGet8(PcdBoardType)));

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_ID1, &Id1);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO PHY ID1 read failed. Status=%r\n", Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_ID2, &Id2);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO PHY ID2 read failed. Status=%r\n", Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_BMCR, &Bmcr);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO BMCR read failed. Status=%r\n", Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_BMSR,
                               &BmsrLatched);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO BMSR read failed. Status=%r\n", Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_BMSR, &Bmsr);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO BMSR reread failed. Status=%r\n", Status));
    return Status;
  }

  DEBUG((DEBUG_INFO,
         "GEM: MDIO PHY%u ID1=0x%04x ID2=0x%04x BMCR=0x%04x "
         "BMSR=0x%04x/0x%04x\n",
         PhyAddr, Id1, Id2, Bmcr, BmsrLatched, Bmsr));

  if (((Id1 == 0x0000) && (Id2 == 0x0000)) ||
      ((Id1 == 0xFFFF) && (Id2 == 0xFFFF))) {
    DEBUG((DEBUG_WARN,
           "GEM: MDIO PHY%u ID looks empty; PHY reset GPIO may be needed\n",
           PhyAddr));
  }

  //
  // Clear EEE advertisement before autoneg. Placed after PHY ID verify
  // (so we know MDIO is working) and before GemRestartAutonegIfNeeded
  // (so the cleared advertisement is what gets negotiated with the link
  // partner). Non-fatal on failure; log and continue.
  //
  Status = GemDisableEeeAdv(Private, PhyAddr);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "GEM: EEE_ADV clear failed (non-fatal). Status=%r\n",
           Status));
  }

  Status = GemRestartAutonegIfNeeded(Private, PhyAddr, Bmsr);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "GEM: Continuing after PHY autoneg wait. Status=%r\n",
           Status));
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_BMSR,
                               &BmsrLatched);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO post-autoneg BMSR read failed. Status=%r\n",
           Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_BMSR, &Bmsr);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR,
           "GEM: MDIO post-autoneg BMSR reread failed. Status=%r\n", Status));
    return Status;
  }

  DEBUG((DEBUG_INFO, "GEM: MDIO post-autoneg BMSR=0x%04x/0x%04x\n",
         BmsrLatched, Bmsr));

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_ADVERTISE,
                               &Advertise);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO advertise read failed. Status=%r\n",
           Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_LPA, &Lpa);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO LPA read failed. Status=%r\n", Status));
    return Status;
  }

  Status = GemMdioReadClause22(Private, PhyAddr, GEM_PHY_REG_STAT1000,
                               &Stat1000);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: MDIO 1000BASE-T status read failed. Status=%r\n",
           Status));
    return Status;
  }

  *LinkConfigValid = GemLogPhyLink(Bmsr, Advertise, Lpa, Stat1000,
                                   NcfgrSpeedBits);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GemResetRp1Phy(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_STATUS Status;
  RP1_GPIO_PIN Pin;

  Status = Rp1GpioGetPin(Private->PeripheralBase, GEM_RP1_PHY_RESET_GPIO, &Pin);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to resolve PHY reset GPIO32. Status=%r\n",
           Status));
    return Status;
  }

  DEBUG((DEBUG_INFO,
         "GEM: Resetting PHY via RP1 GPIO32 ETH_RST_N active-low\n"));

  Rp1GpioConfigureOutput(&Pin, FALSE);
  MicroSecondDelay(GEM_RP1_PHY_RESET_ASSERT_MS * 1000);

  Rp1GpioWrite(&Pin, TRUE);
  MicroSecondDelay(GEM_RP1_PHY_RESET_RELEASE_MS * 1000);

  DEBUG((DEBUG_INFO, "GEM: PHY reset via GPIO32 complete\n"));
  return EFI_SUCCESS;
}

STATIC
VOID
GemApplyRp1AxiPipelineConfig(IN GEM_DXE_PRIVATE_DATA *Private) {
  UINT32 Amp;
  UINT32 ExpectedAmp;

  Amp = GemMmioRead(Private, GEM_AMP);
  ExpectedAmp =
      (Amp & ~((GEM_AMP_AR2R_MAX_PIPE_MASK << GEM_AMP_AR2R_MAX_PIPE_OFFSET) |
               (GEM_AMP_AW2W_MAX_PIPE_MASK << GEM_AMP_AW2W_MAX_PIPE_OFFSET) |
               GEM_AMP_AW2B_FILL)) |
      ((GEM_RP1_AMP_MAX_PIPE & GEM_AMP_AR2R_MAX_PIPE_MASK)
       << GEM_AMP_AR2R_MAX_PIPE_OFFSET) |
      ((GEM_RP1_AMP_MAX_PIPE & GEM_AMP_AW2W_MAX_PIPE_MASK)
       << GEM_AMP_AW2W_MAX_PIPE_OFFSET) |
      GEM_AMP_AW2B_FILL;

  if (Amp == ExpectedAmp) {
    return;
  }

  GemMmioWrite(Private, GEM_AMP, ExpectedAmp);
  Amp = GemMmioRead(Private, GEM_AMP);
  if (Amp != ExpectedAmp) {
    DEBUG((DEBUG_WARN,
           "GEM: RP1 AXI pipeline config did not read back. AMP=0x%08x "
           "expected=0x%08x\n",
           Amp, ExpectedAmp));
  }
}

STATIC
BOOLEAN
GemIsZeroMac(IN CONST EFI_MAC_ADDRESS *Address) {
  UINTN Index;

  for (Index = 0; Index < GEM_ETHERNET_HW_ADDRESS_SIZE; Index++) {
    if (Address->Addr[Index] != 0) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC
VOID
GemProgramMacAddress(IN GEM_DXE_PRIVATE_DATA *Private,
                     IN CONST EFI_MAC_ADDRESS *Address) {
  UINT32 SaBottom;
  UINT32 SaTop;

  SaBottom = ((UINT32)Address->Addr[3] << 24) |
             ((UINT32)Address->Addr[2] << 16) |
             ((UINT32)Address->Addr[1] << 8) |
             (UINT32)Address->Addr[0];
  SaTop = ((UINT32)Address->Addr[5] << 8) | (UINT32)Address->Addr[4];

  GemMmioWrite(Private, GEM_SA1B, SaBottom);
  GemMmioWrite(Private, GEM_SA1T, SaTop);
}

STATIC
VOID
GemLoadStationAddress(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_STATUS Status;
  RASPBERRY_PI_FIRMWARE_PROTOCOL *Firmware;
  EFI_MAC_ADDRESS Address;
  UINTN Retry;
  BOOLEAN Got;

  Status = gBS->LocateProtocol(&gRaspberryPiFirmwareProtocolGuid, NULL,
                               (VOID **)&Firmware);
  Got = FALSE;
  if (!EFI_ERROR(Status)) {
    //
    // Retry on transient firmware-side partial reads. Some VideoCore mailbox
    // paths have returned MACs with leading-zero bytes when the Ethernet
    // subsystem on RP1 hadn't fully enumerated yet. Two cheap checks per
    // attempt: non-error status and the upper 4 bytes are not all zero.
    //
    for (Retry = 0; Retry < 10; Retry++) {
      ZeroMem(&Address, sizeof(Address));
      Status = Firmware->GetMacAddress(Address.Addr);
      if (!EFI_ERROR(Status) && !GemIsZeroMac(&Address) &&
          !((Address.Addr[0] == 0) && (Address.Addr[1] == 0) &&
            (Address.Addr[2] == 0) && (Address.Addr[3] == 0))) {
        Got = TRUE;
        break;
      }
      MicroSecondDelay(50 * 1000);
    }
  }

  if (!Got) {
    DEBUG((DEBUG_WARN,
           "GEM: Firmware MAC unavailable after retries. Status=%r; using "
           "temporary local address for SNP bring-up\n",
           Status));
    ZeroMem(&Address, sizeof(Address));
    Address.Addr[0] = 0x02;
    Address.Addr[5] = 0x01;
  }

  CopyMem(&Private->SnpMode.PermanentAddress, &Address, sizeof(Address));
  CopyMem(&Private->SnpMode.CurrentAddress, &Address, sizeof(Address));
  GemProgramMacAddress(Private, &Address);

  DEBUG((DEBUG_INFO, "GEM: SNP MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
         Address.Addr[0], Address.Addr[1], Address.Addr[2], Address.Addr[3],
         Address.Addr[4], Address.Addr[5]));
}

STATIC
EFI_STATUS
GemBuildSnpDevicePath(IN GEM_DXE_PRIVATE_DATA *Private) {
  GEM_DEVICE_PATH *DevicePath;

  DevicePath = AllocateZeroPool(sizeof(*DevicePath));
  if (DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DevicePath->Mac.Header.Type = MESSAGING_DEVICE_PATH;
  DevicePath->Mac.Header.SubType = MSG_MAC_ADDR_DP;
  DevicePath->Mac.Header.Length[0] = (UINT8)(sizeof(MAC_ADDR_DEVICE_PATH));
  DevicePath->Mac.Header.Length[1] =
      (UINT8)(sizeof(MAC_ADDR_DEVICE_PATH) >> 8);
  CopyMem(&DevicePath->Mac.MacAddress, &Private->SnpMode.CurrentAddress,
          sizeof(EFI_MAC_ADDRESS));
  DevicePath->Mac.IfType = GEM_ETHERNET_IFTYPE;

  DevicePath->End.Type = END_DEVICE_PATH_TYPE;
  DevicePath->End.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  DevicePath->End.Length[0] = (UINT8)(sizeof(EFI_DEVICE_PATH_PROTOCOL));
  DevicePath->End.Length[1] =
      (UINT8)(sizeof(EFI_DEVICE_PATH_PROTOCOL) >> 8);

  Private->SnpDevicePath = DevicePath;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GemSnpCheckState(IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
                 OUT GEM_DXE_PRIVATE_DATA **Private OPTIONAL) {
  GEM_DXE_PRIVATE_DATA *LocalPrivate;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  LocalPrivate = GEM_DXE_PRIVATE_DATA_FROM_SNP(This);
  if (LocalPrivate->Signature != GEM_DXE_SIGNATURE) {
    return EFI_INVALID_PARAMETER;
  }

  if (Private != NULL) {
    *Private = LocalPrivate;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
GemRecycleRxDescriptor(IN GEM_DXE_PRIVATE_DATA *Private, IN UINTN Index) {
  Private->RxDesc[Index].Ctrl = 0;
  Private->RxDesc[Index].Addr =
      ((UINT32)Private->RxBuffers[Index].DeviceAddress &
       GEM_DMA_DESC_RX_ADDR_MASK);
  if (Index == (GEM_DMA_RX_DESC_COUNT - 1)) {
    Private->RxDesc[Index].Addr |= GEM_DMA_DESC_RX_WRAP;
  }

  MemoryFence();
}

STATIC
VOID
GemApplyReceiveFilters(IN GEM_DXE_PRIVATE_DATA *Private) {
  UINT32 Filters;
  UINT32 Ncfgr;

  Filters = Private->SnpMode.ReceiveFilterSetting;
  Ncfgr = GemMmioRead(Private, GEM_NCFGR);
  Ncfgr &= ~(GEM_NCFGR_CAF | GEM_NCFGR_MTI);

  if ((Filters & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS) != 0) {
    Ncfgr |= GEM_NCFGR_CAF;
  }

  if ((Filters & (EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST |
                  EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST)) != 0) {
    Ncfgr |= GEM_NCFGR_MTI;
    GemMmioWrite(Private, GEM_HRB, 0xFFFFFFFF);
    GemMmioWrite(Private, GEM_HRT, 0xFFFFFFFF);
  }

  GemMmioWrite(Private, GEM_NCFGR, Ncfgr);
}

STATIC
EFI_STATUS
EFIAPI
GemSnpStart(IN EFI_SIMPLE_NETWORK_PROTOCOL *This) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State != EfiSimpleNetworkStopped) {
    return EFI_ALREADY_STARTED;
  }

  Private->SnpMode.State = EfiSimpleNetworkStarted;
  DEBUG((DEBUG_INFO, "GEM: SNP Start\n"));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpStop(IN EFI_SIMPLE_NETWORK_PROTOCOL *This) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  GemMmioClearBits(Private, GEM_NCR, GEM_NCR_RXEN | GEM_NCR_TXEN);
  GemPciePostedWriteFlush(Private);
  Private->SnpMode.State = EfiSimpleNetworkStopped;
  DEBUG((DEBUG_INFO, "GEM: SNP Stop\n"));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpInitialize(IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
                 IN UINTN ExtraRxBufferSize OPTIONAL,
                 IN UINTN ExtraTxBufferSize OPTIONAL) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkInitialized) {
    return EFI_SUCCESS;
  }

  if (!Private->DmaRingsInitialized) {
    return EFI_DEVICE_ERROR;
  }

  // Ring base addresses were programmed in GemInitializeDmaRings at bind
  // time and remain valid; no re-write needed here.
  Private->RxIndex = 0;
  GemMmioWrite(Private, GEM_RSR, GEM_RSR_REC | GEM_RSR_BNA | GEM_RSR_RXOVR);
  GemMmioWrite(Private, GEM_TSR, 0xFFFFFFFF);
  GemApplyReceiveFilters(Private);
  GemMmioWrite(Private, GEM_NCR,
               GemMmioRead(Private, GEM_NCR) | GEM_NCR_RXEN | GEM_NCR_TXEN);
  GemPciePostedWriteFlush(Private);

  Private->SnpMode.MediaPresent = Private->MediaPresent;
  Private->SnpMode.State = EfiSimpleNetworkInitialized;
  DEBUG((DEBUG_INFO,
         "GEM: SNP Initialize media=%a RX/TX enabled RBQP=0x%08x TBQP=0x%08x "
         "NCR=0x%08x\n",
         Private->MediaPresent ? "present" : "absent",
         (UINT32)Private->RxRing.DeviceAddress,
         (UINT32)Private->TxRing.DeviceAddress, GemMmioRead(Private, GEM_NCR)));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpReset(IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
            IN BOOLEAN ExtendedVerification) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  Private->SnpMode.MediaPresent = Private->MediaPresent;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpShutdown(IN EFI_SIMPLE_NETWORK_PROTOCOL *This) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkInitialized) {
    GemMmioClearBits(Private, GEM_NCR, GEM_NCR_RXEN | GEM_NCR_TXEN);
    GemPciePostedWriteFlush(Private);
    Private->SnpMode.State = EfiSimpleNetworkStarted;
    DEBUG((DEBUG_INFO, "GEM: SNP Shutdown\n"));
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpReceiveFilters(IN EFI_SIMPLE_NETWORK_PROTOCOL *This, IN UINT32 Enable,
                     IN UINT32 Disable, IN BOOLEAN ResetMCastFilter,
                     IN UINTN MCastFilterCnt OPTIONAL,
                     IN EFI_MAC_ADDRESS *MCastFilter OPTIONAL) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;
  UINT32 Requested;
  STATIC UINT32 LastDebugFilterSetting = 0xFFFFFFFF;
  STATIC UINT32 LastDebugMCastFilterCount = 0xFFFFFFFF;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  Requested = (Private->SnpMode.ReceiveFilterSetting | Enable) & ~Disable;
  if ((Requested & ~Private->SnpMode.ReceiveFilterMask) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  if (MCastFilterCnt > Private->SnpMode.MaxMCastFilterCount) {
    return EFI_INVALID_PARAMETER;
  }

  Private->SnpMode.ReceiveFilterSetting = Requested;
  if (ResetMCastFilter) {
    Private->SnpMode.MCastFilterCount = 0;
    ZeroMem(&Private->SnpMode.MCastFilter,
            sizeof(Private->SnpMode.MCastFilter));
  } else if (MCastFilterCnt > 0) {
    if (MCastFilter == NULL) {
      return EFI_INVALID_PARAMETER;
    }

    Private->SnpMode.MCastFilterCount = (UINT32)MCastFilterCnt;
    CopyMem(&Private->SnpMode.MCastFilter[0], MCastFilter,
            MCastFilterCnt * sizeof(EFI_MAC_ADDRESS));
  }

  GemApplyReceiveFilters(Private);
  if ((Private->SnpMode.ReceiveFilterSetting != LastDebugFilterSetting) ||
      (Private->SnpMode.MCastFilterCount != LastDebugMCastFilterCount)) {
    DEBUG((DEBUG_VERBOSE,
           "GEM: SNP filters en=0x%08x dis=0x%08x set=0x%08x ncfgr=0x%08x "
           "mc=%u\n",
           Enable, Disable, Private->SnpMode.ReceiveFilterSetting,
           GemMmioRead(Private, GEM_NCFGR),
           (UINT32)Private->SnpMode.MCastFilterCount));
    LastDebugFilterSetting = Private->SnpMode.ReceiveFilterSetting;
    LastDebugMCastFilterCount = Private->SnpMode.MCastFilterCount;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpStationAddress(IN EFI_SIMPLE_NETWORK_PROTOCOL *This, IN BOOLEAN Reset,
                     IN EFI_MAC_ADDRESS *New OPTIONAL) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Reset) {
    CopyMem(&Private->SnpMode.CurrentAddress, &Private->SnpMode.PermanentAddress,
            sizeof(EFI_MAC_ADDRESS));
  } else if (New != NULL) {
    CopyMem(&Private->SnpMode.CurrentAddress, New, sizeof(EFI_MAC_ADDRESS));
  } else {
    return EFI_INVALID_PARAMETER;
  }

  GemProgramMacAddress(Private, &Private->SnpMode.CurrentAddress);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpStatistics(IN EFI_SIMPLE_NETWORK_PROTOCOL *This, IN BOOLEAN Reset,
                 IN OUT UINTN *StatisticsSize OPTIONAL,
                 OUT EFI_NETWORK_STATISTICS *StatisticsTable OPTIONAL) {
  EFI_STATUS Status;

  Status = GemSnpCheckState(This, NULL);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (StatisticsSize != NULL) {
    *StatisticsSize = 0;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpMCastIpToMac(IN EFI_SIMPLE_NETWORK_PROTOCOL *This, IN BOOLEAN IPv6,
                   IN EFI_IP_ADDRESS *IP, OUT EFI_MAC_ADDRESS *MAC) {
  EFI_STATUS Status;

  Status = GemSnpCheckState(This, NULL);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if ((IP == NULL) || (MAC == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem(MAC, sizeof(EFI_MAC_ADDRESS));
  if (IPv6) {
    MAC->Addr[0] = 0x33;
    MAC->Addr[1] = 0x33;
    MAC->Addr[2] = IP->v6.Addr[12];
    MAC->Addr[3] = IP->v6.Addr[13];
    MAC->Addr[4] = IP->v6.Addr[14];
    MAC->Addr[5] = IP->v6.Addr[15];
  } else {
    MAC->Addr[0] = 0x01;
    MAC->Addr[1] = 0x00;
    MAC->Addr[2] = 0x5E;
    MAC->Addr[3] = IP->v4.Addr[1] & 0x7F;
    MAC->Addr[4] = IP->v4.Addr[2];
    MAC->Addr[5] = IP->v4.Addr[3];
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpNvData(IN EFI_SIMPLE_NETWORK_PROTOCOL *This, IN BOOLEAN ReadWrite,
             IN UINTN Offset, IN UINTN BufferSize, IN OUT VOID *Buffer) {
  EFI_STATUS Status;

  Status = GemSnpCheckState(This, NULL);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpGetStatus(IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
                OUT UINT32 *IrqStat OPTIONAL, OUT VOID **TxBuf OPTIONAL) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;
  UINT32 Isr;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  //
  // Defensive RSR clear: MNP polls GetStatus more often than Receive
  // when idle, so this catches a latched RSR.BNA faster. See the
  // longer rationale on the same write in GemSnpReceive.
  //
  GemMmioWrite(Private, GEM_RSR, GEM_RSR_REC | GEM_RSR_BNA | GEM_RSR_RXOVR);

  if (IrqStat != NULL) {
    Isr = GemMmioRead(Private, GEM_ISR);
    *IrqStat = 0;
    if ((Isr & GEM_ISR_RCOMP) != 0) {
      *IrqStat |= EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT;
    }
    if ((Isr & GEM_ISR_TCOMP) != 0) {
      *IrqStat |= EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
    }
    if ((Isr & (GEM_ISR_TXERR | GEM_ISR_HRESP | GEM_ISR_ROVR |
                GEM_ISR_RXUBR | GEM_ISR_TXUBR | GEM_ISR_TUND)) != 0) {
      *IrqStat |= EFI_SIMPLE_NETWORK_COMMAND_INTERRUPT;
    }
  }

  if (TxBuf != NULL) {
    *TxBuf = NULL;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpTransmit(IN EFI_SIMPLE_NETWORK_PROTOCOL *This, IN UINTN HeaderSize,
               IN UINTN BufferSize, IN VOID *Buffer,
               IN EFI_MAC_ADDRESS *SrcAddr OPTIONAL,
               IN EFI_MAC_ADDRESS *DestAddr OPTIONAL,
               IN UINT16 *Protocol OPTIONAL) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;
  GEM_DMA_ALLOCATION PaddedTx;
  EFI_MAC_ADDRESS *Source;
  EFI_PHYSICAL_ADDRESS DeviceAddress;
  UINTN FrameLength;
  UINTN MapLength;
  UINT8 *Frame;
  UINT16 EtherType;
  VOID *Mapping;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->SnpMode.State != EfiSimpleNetworkInitialized) {
    return EFI_DEVICE_ERROR;
  }

  if ((Buffer == NULL) || (BufferSize == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Private->MediaPresent) {
    return EFI_NOT_READY;
  }

  if (HeaderSize != 0) {
    if (HeaderSize != Private->SnpMode.MediaHeaderSize) {
      return EFI_INVALID_PARAMETER;
    }

    if ((DestAddr == NULL) || (Protocol == NULL) ||
        (BufferSize < Private->SnpMode.MediaHeaderSize)) {
      return EFI_INVALID_PARAMETER;
    }

    Source = (SrcAddr == NULL) ? &Private->SnpMode.CurrentAddress : SrcAddr;
    Frame = (UINT8 *)Buffer;
    CopyMem(&Frame[0], DestAddr->Addr, GEM_ETHERNET_HW_ADDRESS_SIZE);
    CopyMem(&Frame[6], Source->Addr, GEM_ETHERNET_HW_ADDRESS_SIZE);
    Frame[12] = (UINT8)(*Protocol >> 8);
    Frame[13] = (UINT8)(*Protocol);
  }

  if (BufferSize > GEM_TX_MAX_FRAME_SIZE) {
    return EFI_INVALID_PARAMETER;
  }

  GemZeroAllocation(&PaddedTx);
  Mapping = NULL;
  DeviceAddress = 0;
  FrameLength = MAX(BufferSize, GEM_TX_MIN_FRAME_SIZE);

  if (FrameLength != BufferSize) {
    Status = GemDmaAllocateAndMap(Private, 1, &PaddedTx);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    CopyMem(PaddedTx.HostAddress, Buffer, BufferSize);
    DeviceAddress = PaddedTx.DeviceAddress;
  } else {
    MapLength = BufferSize;
    Status = Private->Rp1Bus->DmaMap(
        Private->Rp1Bus, EfiPciIoOperationBusMasterRead, Buffer, &MapLength,
        &DeviceAddress, &Mapping);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    FrameLength = MapLength;
  }

  Frame = (FrameLength != BufferSize) ? (UINT8 *)PaddedTx.HostAddress :
                                        (UINT8 *)Buffer;
  EtherType = 0;
  if (BufferSize >= GEM_ETHERNET_HEADER_SIZE) {
    EtherType = (UINT16)((Frame[12] << 8) | Frame[13]);
  }

  Status = GemTransmitFrameSync(Private, DeviceAddress, FrameLength);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN,
           "GEM: TX failed len=%u et=0x%04x st=%r TSR=0x%08x ISR=0x%08x\n",
           (UINT32)BufferSize, EtherType, Status,
           GemMmioRead(Private, GEM_TSR), GemMmioRead(Private, GEM_ISR)));
  }

  if (Mapping != NULL) {
    Private->Rp1Bus->DmaUnmap(Private->Rp1Bus, Mapping);
  }

  if (PaddedTx.HostAddress != NULL) {
    GemDmaUnmapAndFree(Private, &PaddedTx);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
GemSnpReceive(IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
              OUT UINTN *HeaderSize OPTIONAL, IN OUT UINTN *BufferSize,
              OUT VOID *Buffer, OUT EFI_MAC_ADDRESS *SrcAddr OPTIONAL,
              OUT EFI_MAC_ADDRESS *DestAddr OPTIONAL,
              OUT UINT16 *Protocol OPTIONAL) {
  EFI_STATUS Status;
  GEM_DXE_PRIVATE_DATA *Private;
  UINT32 Addr;
  UINT32 Ctrl;
  UINTN FrameLength;
  UINTN Index;
  UINT8 *Frame;

  Status = GemSnpCheckState(This, &Private);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (Private->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->SnpMode.State != EfiSimpleNetworkInitialized) {
    return EFI_DEVICE_ERROR;
  }

  if ((BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!Private->MediaPresent) {
    return EFI_NOT_READY;
  }

  //
  // RSR.BNA is a latched status bit (W1C). When the controller exhausts
  // its receive descriptors it sets BNA and stops writing further frames;
  // per Cadence GEM spec, freeing descriptors alone does not auto-clear
  // BNA -- only an explicit W1C to RSR.BNA (or an NCR.RXEN cycle) resumes
  // the receiver. Clear at the top of every poll so a latch picked up
  // during a brief 4-descriptor exhaustion never silently drops the next
  // unicast frame (DHCP OFFER, ARP reply, ICMP echo reply).
  //
  GemMmioWrite(Private, GEM_RSR, GEM_RSR_REC | GEM_RSR_BNA | GEM_RSR_RXOVR);

  Index = Private->RxIndex;
  MemoryFence();
  Addr = Private->RxDesc[Index].Addr;
  Ctrl = Private->RxDesc[Index].Ctrl;

  if ((Addr & GEM_DMA_DESC_RX_USED) == 0) {
    return EFI_NOT_READY;
  }

  FrameLength = Ctrl & GEM_DMA_DESC_RX_LENGTH_MASK;
  if ((FrameLength < GEM_ETHERNET_HEADER_SIZE) ||
      (FrameLength > GEM_DMA_RX_BUFFER_SIZE)) {
    DEBUG((DEBUG_WARN,
           "GEM: SNP RX dropping malformed desc idx=%u addr=0x%08x "
           "ctrl=0x%08x len=%u RSR=0x%08x ISR=0x%08x\n",
           (UINT32)Index, Addr, Ctrl, (UINT32)FrameLength,
           GemMmioRead(Private, GEM_RSR), GemMmioRead(Private, GEM_ISR)));
    GemRecycleRxDescriptor(Private, Index);
    Private->RxIndex = (Private->RxIndex + 1) % GEM_DMA_RX_DESC_COUNT;
    return EFI_NOT_READY;
  }

  if (*BufferSize < FrameLength) {
    *BufferSize = FrameLength;
    return EFI_BUFFER_TOO_SMALL;
  }

  Frame = (UINT8 *)Private->RxBuffers[Index].HostAddress;
  CopyMem(Buffer, Frame, FrameLength);
  *BufferSize = FrameLength;

  if (HeaderSize != NULL) {
    *HeaderSize = GEM_ETHERNET_HEADER_SIZE;
  }
  if (DestAddr != NULL) {
    CopyMem(DestAddr->Addr, &Frame[0], GEM_ETHERNET_HW_ADDRESS_SIZE);
  }
  if (SrcAddr != NULL) {
    CopyMem(SrcAddr->Addr, &Frame[6], GEM_ETHERNET_HW_ADDRESS_SIZE);
  }
  if (Protocol != NULL) {
    *Protocol = (UINT16)((Frame[12] << 8) | Frame[13]);
  }

  GemRecycleRxDescriptor(Private, Index);
  Private->RxIndex = (Private->RxIndex + 1) % GEM_DMA_RX_DESC_COUNT;
  GemMmioWrite(Private, GEM_RSR, GEM_RSR_REC | GEM_RSR_BNA | GEM_RSR_RXOVR);

  return EFI_SUCCESS;
}

STATIC
VOID
GemInitializeSnp(IN GEM_DXE_PRIVATE_DATA *Private) {
  EFI_SIMPLE_NETWORK_PROTOCOL *Snp;
  EFI_SIMPLE_NETWORK_MODE *Mode;

  Snp = &Private->Snp;
  Mode = &Private->SnpMode;

  Snp->Revision = EFI_SIMPLE_NETWORK_PROTOCOL_REVISION;
  Snp->Start = GemSnpStart;
  Snp->Stop = GemSnpStop;
  Snp->Initialize = GemSnpInitialize;
  Snp->Reset = GemSnpReset;
  Snp->Shutdown = GemSnpShutdown;
  Snp->ReceiveFilters = GemSnpReceiveFilters;
  Snp->StationAddress = GemSnpStationAddress;
  Snp->Statistics = GemSnpStatistics;
  Snp->MCastIpToMac = GemSnpMCastIpToMac;
  Snp->NvData = GemSnpNvData;
  Snp->GetStatus = GemSnpGetStatus;
  Snp->Transmit = GemSnpTransmit;
  Snp->Receive = GemSnpReceive;
  Snp->WaitForPacket = NULL;
  Snp->Mode = Mode;

  Mode->State = EfiSimpleNetworkStopped;
  Mode->HwAddressSize = GEM_ETHERNET_HW_ADDRESS_SIZE;
  Mode->MediaHeaderSize = GEM_ETHERNET_HEADER_SIZE;
  Mode->MaxPacketSize = GEM_ETHERNET_MAX_PACKET_SIZE;
  Mode->NvRamSize = 0;
  Mode->NvRamAccessSize = 0;
  Mode->ReceiveFilterMask = GEM_SNP_RX_FILTER_MASK;
  Mode->ReceiveFilterSetting =
      EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
      EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST |
      EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;
  Mode->MaxMCastFilterCount = GEM_SNP_MAX_MCAST_FILTER_COUNT;
  Mode->MCastFilterCount = 0;
  Mode->IfType = GEM_ETHERNET_IFTYPE;
  Mode->MacAddressChangeable = TRUE;
  Mode->MultipleTxSupported = FALSE;
  Mode->MediaPresentSupported = TRUE;
  Mode->MediaPresent = Private->MediaPresent;
  SetMem(&Mode->BroadcastAddress, sizeof(Mode->BroadcastAddress), 0xFF);

  GemLoadStationAddress(Private);
}

STATIC
VOID
GemFreeSnpDevicePath(IN GEM_DXE_PRIVATE_DATA *Private) {
  if (Private->SnpDevicePath != NULL) {
    FreePool(Private->SnpDevicePath);
    Private->SnpDevicePath = NULL;
  }
}

//
// ExitBootServices handler. The OS loader is about to reclaim our
// EfiBootServicesData pages (which back our RX/TX descriptor rings and RX
// buffers); if RXEN/TXEN are still set the GEM will keep DMA-ing into pages
// the OS has handed to its own allocators. Disable RX/TX and flush the
// PCIe posted-write queue so the engine quiesces before the handoff completes.
//
// Scope is intentionally narrow: do NOT touch NCFGR (speed/duplex), the PHY,
// RP1 clocks, DMACFG, AMP, USRIO, SA1B/SA1T, or RBQP/TBQP. The downstream
// Windows driver explicitly relies on EDK2 having established and preserved
// the link, MAC, and DMA config; clearing any of those here would force
// Windows into a cold-bring-up path it does not currently implement.
//
STATIC
VOID
EFIAPI
GemDxeExitBootServicesHandler(IN EFI_EVENT Event, IN VOID *Context) {
  GEM_DXE_PRIVATE_DATA *Private = (GEM_DXE_PRIVATE_DATA *)Context;

  if (Private == NULL) {
    return;
  }

  GemMmioClearBits(Private, GEM_NCR, GEM_NCR_RXEN | GEM_NCR_TXEN);
  GemPciePostedWriteFlush(Private);
}

//
// Driver Binding Protocol
//

/**
  Check if this driver supports the given controller.

  @param  This[in]                 Pointer to EFI_DRIVER_BINDING_PROTOCOL.
  @param  ControllerHandle[in]     Handle of controller to test.
  @param  RemainingDevicePath[in] Optional device path.

  @retval EFI_SUCCESS  Controller is supported.
  @retval other        Controller is not supported.

**/
EFI_STATUS
EFIAPI
GemDxeDriverBindingSupported(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                             IN EFI_HANDLE ControllerHandle,
                             IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath) {
  EFI_STATUS Status;
  RP1_BUS_PROTOCOL *Rp1Bus;

  //
  // Check if RP1 Bus Protocol is available
  // This driver runs as a child of RP1 Bus
  //
  Status = gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                             (VOID **)&Rp1Bus, This->DriverBindingHandle,
                             ControllerHandle, EFI_OPEN_PROTOCOL_BY_DRIVER);

  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }

  gBS->CloseProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                     This->DriverBindingHandle, ControllerHandle);

  return EFI_SUCCESS;
}

/**
  Start this driver on the given controller.

  @param  This[in]                 Pointer to EFI_DRIVER_BINDING_PROTOCOL.
  @param  ControllerHandle[in]     Handle of controller to start.
  @param  RemainingDevicePath[in]  Optional device path.

  @retval EFI_SUCCESS  Driver started successfully.
  @retval other        Failed to start driver.

**/
EFI_STATUS
EFIAPI
GemDxeDriverBindingStart(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                         IN EFI_HANDLE ControllerHandle,
                         IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath) {
  EFI_STATUS Status;
  RP1_BUS_PROTOCOL *Rp1Bus;
  GEM_DXE_PRIVATE_DATA *Private;
  BOOLEAN PrivateInstalled;
  BOOLEAN SnpInstalled;
  BOOLEAN ChildOpen;

  Private = NULL;
  PrivateInstalled = FALSE;
  SnpInstalled = FALSE;
  ChildOpen = FALSE;

  DEBUG((DEBUG_INFO, "GEM: DriverBindingStart called\n"));

  //
  // Open RP1 Bus Protocol
  //
  Status = gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                             (VOID **)&Rp1Bus, This->DriverBindingHandle,
                             ControllerHandle, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to open RP1 Bus Protocol. Status=%r\n",
           Status));
    return Status;
  }

  //
  // Allocate private data structure
  //
  Private = AllocateZeroPool(sizeof(GEM_DXE_PRIVATE_DATA));
  if (Private == NULL) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to allocate private data\n"));
    Status = EFI_OUT_OF_RESOURCES;
    goto Fail;
  }

  Private->Signature = GEM_DXE_SIGNATURE;
  Private->ControllerHandle = ControllerHandle;
  Private->Rp1Bus = Rp1Bus;

  //
  // Get peripheral base address from RP1 Bus
  //
  Private->PeripheralBase = Rp1Bus->GetPeripheralBase(Rp1Bus);
  Private->GemBase = Private->PeripheralBase + RP1_ETH_BASE;
  Private->GemCfgBase = Private->PeripheralBase + RP1_ETH_CFG_BASE;
  Private->ClockBase = Private->PeripheralBase + RP1_CLOCKS_MAIN_BASE;

  Status = GemProbeRp1DmaTranslation(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: DMA translation probe failed. Status=%r\n",
           Status));
    goto Fail;
  }

  // Pi 5 firmware brings up PLL_SYS, ETH, and ETH_TSU clocks to the values
  // we need before EDK2 runs; the previous Ensure/Check pass was a no-op on
  // every observed boot and has been removed.
  Status = GemHardwareInit(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Hardware initialization failed. Status=%r\n",
           Status));
    goto Fail;
  }

  //
  // Quiesce hardware
  //
  Status = GemResetHardware(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Hardware quiesce failed. Status=%r\n", Status));
    goto Fail;
  }

  GemInitializeSnp(Private);

  Status = GemBuildSnpDevicePath(Private);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to build SNP device path. Status=%r\n",
           Status));
    goto Fail;
  }

  Status = gBS->InstallMultipleProtocolInterfaces(&ControllerHandle,
                                                  &mGemDxePrivateGuid, Private,
                                                  NULL);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to install private protocol. Status=%r\n",
           Status));
    goto Fail;
  }
  PrivateInstalled = TRUE;

  Private->SnpHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces(&Private->SnpHandle,
                                                  &gEfiDevicePathProtocolGuid,
                                                  Private->SnpDevicePath,
                                                  &gEfiSimpleNetworkProtocolGuid,
                                                  &Private->Snp,
                                                  NULL);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to install SNP child protocols. Status=%r\n",
           Status));
    goto Fail;
  }
  SnpInstalled = TRUE;

  Status = gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                             (VOID **)&Rp1Bus, This->DriverBindingHandle,
                             Private->SnpHandle,
                             EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR,
           "GEM: Failed to link SNP child to RP1 controller. Status=%r\n",
           Status));
    goto Fail;
  }
  ChildOpen = TRUE;

  //
  // Register an ExitBootServices handler so the GEM is quiesced before the
  // OS loader reclaims our DMA pages. Non-fatal if it fails — log and
  // continue; the worst case is the same risk we have today.
  //
  if (mGemExitBootServicesEvent == NULL) {
    mGemExitBootServicesPrivate = Private;
    Status = gBS->CreateEventEx(
        EVT_NOTIFY_SIGNAL, TPL_NOTIFY, GemDxeExitBootServicesHandler, Private,
        &gEfiEventExitBootServicesGuid, &mGemExitBootServicesEvent);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_WARN,
             "GEM: ExitBootServices event registration failed. Status=%r\n",
             Status));
      mGemExitBootServicesEvent = NULL;
      mGemExitBootServicesPrivate = NULL;
    }
    Status = EFI_SUCCESS;
  }

  DEBUG((DEBUG_INFO, "GEM: Driver started successfully\n"));

  return EFI_SUCCESS;

Fail:
  if (Private != NULL) {
    if (ChildOpen) {
      gBS->CloseProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                         This->DriverBindingHandle, Private->SnpHandle);
    }

    if (SnpInstalled) {
      gBS->UninstallMultipleProtocolInterfaces(
          Private->SnpHandle, &gEfiDevicePathProtocolGuid,
          Private->SnpDevicePath, &gEfiSimpleNetworkProtocolGuid,
          &Private->Snp, NULL);
    }

    if (PrivateInstalled) {
      gBS->UninstallMultipleProtocolInterfaces(ControllerHandle,
                                               &mGemDxePrivateGuid, Private,
                                               NULL);
    }

    GemFreeSnpDevicePath(Private);
    GemReleaseDmaRings(Private);
    FreePool(Private);
  }

  gBS->CloseProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                     This->DriverBindingHandle, ControllerHandle);

  return Status;
}

/**
  Stop this driver on the given controller.

  @param  This[in]              Pointer to EFI_DRIVER_BINDING_PROTOCOL.
  @param  ControllerHandle[in]  Handle of controller to stop.
  @param  NumberOfChildren[in]  Number of child handles.
  @param  ChildHandleBuffer[in] Array of child handles.

  @retval EFI_SUCCESS  Driver stopped successfully.
  @retval other         Failed to stop driver.

**/
EFI_STATUS
EFIAPI
GemDxeDriverBindingStop(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                        IN EFI_HANDLE ControllerHandle,
                        IN UINTN NumberOfChildren,
                        IN EFI_HANDLE *ChildHandleBuffer) {
  EFI_STATUS Status;
  RP1_BUS_PROTOCOL *Rp1Bus;
  GEM_DXE_PRIVATE_DATA *Private;

  if (NumberOfChildren == 0) {
    DEBUG((DEBUG_INFO, "GEM: DriverBindingStop called\n"));

    //
    // Close the EBS event up front so it cannot fire after we've torn down
    // Private. Safe to call multiple times: only the first matching event is
    // closed; subsequent invocations no-op once the slot is cleared.
    //
    if (mGemExitBootServicesEvent != NULL) {
      gBS->CloseEvent(mGemExitBootServicesEvent);
      mGemExitBootServicesEvent = NULL;
      mGemExitBootServicesPrivate = NULL;
    }

    Status = gBS->HandleProtocol(ControllerHandle, &mGemDxePrivateGuid,
                                 (VOID **)&Private);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    Status =
        gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                          (VOID **)&Rp1Bus, This->DriverBindingHandle,
                          ControllerHandle, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    if (Private->SnpHandle != NULL) {
      Status = gBS->CloseProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                                  This->DriverBindingHandle,
                                  Private->SnpHandle);
      if (EFI_ERROR(Status)) {
        return Status;
      }

      Status = gBS->UninstallMultipleProtocolInterfaces(
          Private->SnpHandle, &gEfiDevicePathProtocolGuid,
          Private->SnpDevicePath, &gEfiSimpleNetworkProtocolGuid,
          &Private->Snp, NULL);
      if (EFI_ERROR(Status)) {
        gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                          (VOID **)&Rp1Bus, This->DriverBindingHandle,
                          Private->SnpHandle,
                          EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
        return Status;
      }
    }

    Status = gBS->UninstallMultipleProtocolInterfaces(
        ControllerHandle, &mGemDxePrivateGuid, Private, NULL);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    GemFreeSnpDevicePath(Private);
    GemReleaseDmaRings(Private);
    FreePool(Private);

    Status = gBS->CloseProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                                This->DriverBindingHandle, ControllerHandle);
    ASSERT_EFI_ERROR(Status);

    return EFI_SUCCESS;
  }

  return EFI_DEVICE_ERROR;
}

//
// Driver Binding Protocol Instance
//
EFI_DRIVER_BINDING_PROTOCOL mGemDxeDriverBinding = {
    GemDxeDriverBindingSupported,
    GemDxeDriverBindingStart,
    GemDxeDriverBindingStop,
    0x10,
    NULL,
    NULL};

//
// Driver Entry Point
//
EFI_STATUS
EFIAPI
GemDxeEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status;

  DEBUG((DEBUG_INFO, "GEM: Driver entry point\n"));

  //
  // Consumer-side PCD poll: reads PcdBoardType (the only board-identity PCD
  // GemDxe consumes) from a separate driver to confirm cross-module PCD
  // plumbing. Pair with the "RpiBoardId:" producer-side line.
  //
  DEBUG((DEBUG_INFO,
         "GEM: PCD poll -> PcdBoardType=0x%02x\n",
         PcdGet8(PcdBoardType)));

  mGemDxeDriverBinding.ImageHandle = ImageHandle;
  mGemDxeDriverBinding.DriverBindingHandle = ImageHandle;

  Status = gBS->InstallMultipleProtocolInterfaces(
      &mGemDxeDriverBinding.DriverBindingHandle, &gEfiDriverBindingProtocolGuid,
      &mGemDxeDriverBinding, NULL);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "GEM: Failed to install driver binding. Status=%r\n",
           Status));
  }

  return Status;
}
