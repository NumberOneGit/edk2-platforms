/** @file
 *
 *  Copyright (c) 2026
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __GEM_DXE_H__
#define __GEM_DXE_H__

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/RpiFirmware.h>
#include <Protocol/Rp1Bus.h>
#include <Protocol/SimpleNetwork.h>
#include <Rp1.h>

//
// Cadence MACB/GEM register offsets.
// Keep this list close to U-Boot's drivers/net/macb.h; RP1 uses GEM
// extensions, while the low control/status registers keep MACB offsets.
//

#define GEM_NCR 0x0000
#define GEM_NCR_LB BIT0
#define GEM_NCR_LLB BIT1
#define GEM_NCR_RXEN BIT2
#define GEM_NCR_TXEN BIT3
#define GEM_NCR_MPE BIT4
#define GEM_NCR_CLRSTAT BIT5
#define GEM_NCR_INCSTAT BIT6
#define GEM_NCR_WESTAT BIT7
#define GEM_NCR_BP BIT8
#define GEM_NCR_TSTART BIT9
#define GEM_NCR_TXHALT BIT10
#define GEM_NCR_TPF BIT11
#define GEM_NCR_TZQ BIT12
#define GEM_NCR_SRTSM BIT15
#define GEM_NCR_OSSMODE BIT24

#define GEM_NCFGR 0x0004
#define GEM_NCFGR_SPD BIT0
#define GEM_NCFGR_FD BIT1
#define GEM_NCFGR_BIT_RATE BIT2
#define GEM_NCFGR_JFRAME BIT3
#define GEM_NCFGR_CAF BIT4
#define GEM_NCFGR_NBC BIT5
#define GEM_NCFGR_MTI BIT6
#define GEM_NCFGR_UNI BIT7
#define GEM_NCFGR_BIG BIT8
#define GEM_NCFGR_EAE BIT9
#define GEM_NCFGR_GBE BIT10
#define GEM_NCFGR_PCSSEL BIT11
#define GEM_NCFGR_PAE BIT13
#define GEM_NCFGR_RBOF_OFFSET 14
#define GEM_NCFGR_RBOF_MASK 0x3
#define GEM_NCFGR_RLCE BIT16
#define GEM_NCFGR_DRFCS BIT17
#define GEM_NCFGR_CLK_OFFSET 18
#define GEM_NCFGR_CLK_MASK 0x7
#define GEM_NCFGR_DBW_OFFSET 21
#define GEM_NCFGR_DBW_MASK 0x3
#define GEM_NCFGR_DBW_32 (0x0 << GEM_NCFGR_DBW_OFFSET)
#define GEM_NCFGR_DBW_64 (0x1 << GEM_NCFGR_DBW_OFFSET)
#define GEM_NCFGR_DBW_128 (0x2 << GEM_NCFGR_DBW_OFFSET)
#define GEM_NCFGR_RXCSUMEN BIT24
#define GEM_NCFGR_SGMIIEN BIT27

#define GEM_NSR 0x0008
#define GEM_NSR_MDIO BIT1
#define GEM_NSR_IDLE BIT2

#define GEM_USRIO 0x000C
#define GEM_USRIO_RGMII BIT0

#define GEM_DMACFG 0x0010
#define GEM_DMACFG_FBLDO_OFFSET 0
#define GEM_DMACFG_FBLDO_MASK 0x1F
#define GEM_DMACFG_ENDIA_DESC BIT6
#define GEM_DMACFG_ENDIA_PKT BIT7
#define GEM_DMACFG_RXBMS_OFFSET 8
#define GEM_DMACFG_RXBMS_MASK 0x3
#define GEM_DMACFG_TXPBMS BIT10
#define GEM_DMACFG_TXCSUMEN BIT11
#define GEM_DMACFG_RXBS_OFFSET 16
#define GEM_DMACFG_RXBS_MASK 0xFF
#define GEM_DMACFG_DDRP BIT24
#define GEM_DMACFG_RXEXT BIT28
#define GEM_DMACFG_TXEXT BIT29
#define GEM_DMACFG_ADDR64 BIT30

#define GEM_TSR 0x0014
#define GEM_TSR_HRESP BIT11

#define GEM_RBQP 0x0018
#define GEM_TBQP 0x001C

#define GEM_RSR 0x0020
#define GEM_RSR_HRESP BIT11
#define GEM_RSR_RXOVR BIT2
#define GEM_RSR_REC BIT1
#define GEM_RSR_BNA BIT0

#define GEM_ISR 0x0024
#define GEM_ISR_MFD BIT0
#define GEM_ISR_RCOMP BIT1
#define GEM_ISR_RXUBR BIT2
#define GEM_ISR_TXUBR BIT3
#define GEM_ISR_TUND BIT4
#define GEM_ISR_RLE BIT5
#define GEM_ISR_TXERR BIT6
#define GEM_ISR_TCOMP BIT7
#define GEM_ISR_LINK BIT9
#define GEM_ISR_ROVR BIT10
#define GEM_ISR_HRESP BIT11
#define GEM_ISR_WOL BIT14

#define GEM_IER 0x0028
#define GEM_IDR 0x002C
#define GEM_IMR 0x0030

#define GEM_MAN 0x0034
#define GEM_MAN_DATA_OFFSET 0
#define GEM_MAN_DATA_MASK 0xFFFF
#define GEM_MAN_CODE_OFFSET 16
#define GEM_MAN_CODE_MASK 0x3
#define GEM_MAN_REG_OFFSET 18
#define GEM_MAN_REG_MASK 0x1F
#define GEM_MAN_PHY_OFFSET 23
#define GEM_MAN_PHY_MASK 0x1F
#define GEM_MAN_RW_OFFSET 28
#define GEM_MAN_RW_MASK 0x3
#define GEM_MAN_SOF_OFFSET 30
#define GEM_MAN_SOF_MASK 0x3
#define GEM_MAN_C22_SOF 1
#define GEM_MAN_C22_WRITE 1
#define GEM_MAN_C22_READ 2
#define GEM_MAN_C22_CODE 2

#define GEM_JML 0x0048

// AXI Max Pipeline. RP1 describes these fields through Cadence DT properties.
#define GEM_AMP 0x0054
#define GEM_AMP_AR2R_MAX_PIPE_OFFSET 0
#define GEM_AMP_AR2R_MAX_PIPE_MASK 0xFF
#define GEM_AMP_AW2W_MAX_PIPE_OFFSET 8
#define GEM_AMP_AW2W_MAX_PIPE_MASK 0xFF
#define GEM_AMP_AW2B_FILL BIT16

#define GEM_HRB 0x0080
#define GEM_HRT 0x0084
#define GEM_SA1B 0x0088
#define GEM_SA1T 0x008C

#define GEM_TSH 0x01C0
#define GEM_TSL 0x01D0
#define GEM_TN 0x01D4
#define GEM_TA 0x01D8
#define GEM_TI 0x01DC

#define GEM_DCFG1 0x0280
#define GEM_DCFG1_DBWDEF_OFFSET 25
#define GEM_DCFG1_DBWDEF_MASK 0x7
#define GEM_DCFG2 0x0284
#define GEM_DCFG3 0x0288
#define GEM_DCFG4 0x028C
#define GEM_DCFG5 0x0290
#define GEM_DCFG6 0x0294
#define GEM_DCFG7 0x0298
#define GEM_DCFG8 0x029C
#define GEM_DCFG10 0x02A4

#define GEM_QUEUE_ISR(q) (0x0400 + ((q) << 2))
#define GEM_QUEUE_TBQP(q) (0x0440 + ((q) << 2))
#define GEM_QUEUE_RBQP(q) (0x0480 + ((q) << 2))
#define GEM_QUEUE_RBQS(q) (0x04A0 + ((q) << 2))
#define GEM_TBQPH 0x04C8
#define GEM_TXBDCTRL 0x04CC
#define GEM_RXBDCTRL 0x04D0
#define GEM_RBQPH 0x04D4
#define GEM_QUEUE_IER(q) (0x0600 + ((q) << 2))
#define GEM_QUEUE_IDR(q) (0x0620 + ((q) << 2))
#define GEM_QUEUE_IMR(q) (0x0640 + ((q) << 2))

#define GEM_DMA_RX_DESC_COUNT 16
#define GEM_DMA_TX_DESC_COUNT 16
#define GEM_DMA_RX_BUFFER_SIZE 2048
#define GEM_DMA_RX_BUFFER_PAGES EFI_SIZE_TO_PAGES(GEM_DMA_RX_BUFFER_SIZE)
#define GEM_DMA_RING_PAGES 1
#define GEM_ETHERNET_HW_ADDRESS_SIZE 6
#define GEM_ETHERNET_HEADER_SIZE 14
#define GEM_ETHERNET_MAX_PACKET_SIZE 1500
#define GEM_ETHERNET_IFTYPE 1

typedef struct {
  UINT32 Addr;
  UINT32 Ctrl;
} GEM_DMA_DESC;

typedef struct {
  VOID *HostAddress;
  VOID *Mapping;
  EFI_PHYSICAL_ADDRESS DeviceAddress;
  UINTN NumberOfBytes;
  UINTN Pages;
} GEM_DMA_ALLOCATION;

#pragma pack(1)
typedef struct {
  MAC_ADDR_DEVICE_PATH Mac;
  EFI_DEVICE_PATH_PROTOCOL End;
} GEM_DEVICE_PATH;
#pragma pack()

//
// Driver Signature
//
#define GEM_DXE_SIGNATURE SIGNATURE_32('G', 'E', 'M', 'D')

//
// Private Data Structure
//
typedef struct {
  UINT32 Signature;
  EFI_HANDLE ControllerHandle;
  EFI_HANDLE SnpHandle;
  RP1_BUS_PROTOCOL *Rp1Bus;
  EFI_PHYSICAL_ADDRESS PeripheralBase;
  EFI_PHYSICAL_ADDRESS GemBase;
  EFI_PHYSICAL_ADDRESS GemCfgBase;
  EFI_PHYSICAL_ADDRESS ClockBase;
  GEM_DEVICE_PATH *SnpDevicePath;
  GEM_DMA_ALLOCATION RxRing;
  GEM_DMA_ALLOCATION TxRing;
  GEM_DMA_ALLOCATION RxBuffers[GEM_DMA_RX_DESC_COUNT];
  GEM_DMA_DESC *RxDesc;
  GEM_DMA_DESC *TxDesc;
  UINTN RxIndex;
  UINTN TxIndex;
  BOOLEAN DmaRingsInitialized;
  BOOLEAN MediaPresent;
  EFI_SIMPLE_NETWORK_PROTOCOL Snp;
  EFI_SIMPLE_NETWORK_MODE SnpMode;
} GEM_DXE_PRIVATE_DATA;

#define GEM_DXE_PRIVATE_DATA_FROM_THIS(a)                                      \
  CR(a, GEM_DXE_PRIVATE_DATA, Rp1Bus, GEM_DXE_SIGNATURE)

#define GEM_DXE_PRIVATE_DATA_FROM_SNP(a)                                       \
  CR(a, GEM_DXE_PRIVATE_DATA, Snp, GEM_DXE_SIGNATURE)

//
// Function Prototypes
//
EFI_STATUS
EFIAPI
GemDxeDriverBindingSupported(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                             IN EFI_HANDLE ControllerHandle,
                             IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath);

EFI_STATUS
EFIAPI
GemDxeDriverBindingStart(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                         IN EFI_HANDLE ControllerHandle,
                         IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath);

EFI_STATUS
EFIAPI
GemDxeDriverBindingStop(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                        IN EFI_HANDLE ControllerHandle,
                        IN UINTN NumberOfChildren,
                        IN EFI_HANDLE *ChildHandleBuffer);

//
// Register Access Functions
//
UINT32
GemMmioRead(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset);

VOID GemMmioWrite(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset,
                  IN UINT32 Value);

UINT32
GemMmioSetBits(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset,
               IN UINT32 SetBits);

UINT32
GemMmioClearBits(IN GEM_DXE_PRIVATE_DATA *Private, IN UINT32 Offset,
                 IN UINT32 ClearBits);

VOID GemDebugPrintClockBlock(IN GEM_DXE_PRIVATE_DATA *Private,
                             IN UINT32 BlockOffset, IN CONST CHAR8 *BlockName);

VOID
GemDumpState(IN GEM_DXE_PRIVATE_DATA *Private, IN CONST CHAR8 *Label);

//
// Clock State Check Functions
//
EFI_STATUS
GemCheckClockState(IN GEM_DXE_PRIVATE_DATA *Private);

EFI_STATUS
GemPrintClockRegisters(IN GEM_DXE_PRIVATE_DATA *Private);

//
// Hardware Initialization Functions
//
EFI_STATUS
GemHardwareInit(IN GEM_DXE_PRIVATE_DATA *Private);

EFI_STATUS
GemResetHardware(IN GEM_DXE_PRIVATE_DATA *Private);

EFI_STATUS
GemEnsurePrerequisites(IN GEM_DXE_PRIVATE_DATA *Private);

#endif // __GEM_DXE_H__
