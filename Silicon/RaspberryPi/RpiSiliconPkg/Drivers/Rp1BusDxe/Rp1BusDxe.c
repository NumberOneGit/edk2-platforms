/** @file
 *
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <IndustryStandard/Pci.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Rp1Clock.h>
#include <Rp1Gpio.h>
#include <Rp1.h>
#include <Rp1Pwm.h>
#include <Uefi.h>

#include "Rp1BusDxe.h"

#define RP1_FAN_PWM_GPIO      45
#define RP1_FAN_PWM_FUNCTION  Rp1GpioFunctionAlt0
#define RP1_FAN_PWM_CHANNEL   3
#define RP1_FAN_PWM_RANGE     2000
#define RP1_FAN_PWM_DUTY      500
#define RP1_FAN_PWM_INVERTED  TRUE

STATIC
VOID EFIAPI Rp1BusRegisterDwc3Controllers(IN RP1_BUS_DATA *Rp1Data) {
  EFI_STATUS Status;
  UINTN Index;
  EFI_PHYSICAL_ADDRESS FullBase;
  EFI_HANDLE DeviceHandle;
  RP1_BUS_PROTOCOL *Rp1Bus;

  EFI_PHYSICAL_ADDRESS Dwc3Addresses[] = {RP1_USBHOST0_BASE, RP1_USBHOST1_BASE};

  for (Index = 0; Index < ARRAY_SIZE(Dwc3Addresses); Index++) {
    DeviceHandle = NULL;
    FullBase = Rp1Data->PeripheralBase + Dwc3Addresses[Index];

    Status = RegisterNonDiscoverableMmioDevice(
        NonDiscoverableDeviceTypeXhci, NonDiscoverableDeviceDmaTypeNonCoherent,
        NULL, &DeviceHandle, 1, FullBase, RP1_USBHOST_SIZE);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR,
             "RP1: Failed to register DWC3 controller at 0x%lx. Status=%r\n",
             FullBase, Status));
      continue;
    }

    Status = gBS->OpenProtocol(
        Rp1Data->ControllerHandle, &gRp1BusProtocolGuid, (VOID **)&Rp1Bus,
        Rp1Data->DriverBinding->DriverBindingHandle, DeviceHandle,
        EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
    if (EFI_ERROR(Status)) {
      DEBUG((DEBUG_ERROR, "RP1: Failed to open DWC3 by controller. Status=%r\n",
             Status));
      continue;
    }
  }
}

STATIC
VOID EFIAPI Rp1BusRegisterDevices(IN RP1_BUS_DATA *Rp1Data) {
  Rp1BusRegisterDwc3Controllers(Rp1Data);
}

STATIC
VOID EFIAPI Rp1BusEnableInterrupts(IN RP1_BUS_DATA *Rp1Data) {
  MmioWrite32(Rp1Data->PeripheralBase + RP1_PCIE_REG_SET +
                  RP1_PCIE_MSIX_CFG(RP1_INT_USBHOST0_0),
              RP1_PCIE_MSIX_CFG_ENABLE);
  MmioWrite32(Rp1Data->PeripheralBase + RP1_PCIE_REG_SET +
                  RP1_PCIE_MSIX_CFG(RP1_INT_USBHOST1_0),
              RP1_PCIE_MSIX_CFG_ENABLE);
}

STATIC
EFI_PHYSICAL_ADDRESS
EFIAPI
Rp1BusGetPeripheralBase(IN RP1_BUS_PROTOCOL *This) {
  ASSERT(This != NULL);

  return (RP1_BUS_DATA_FROM_THIS(This))->PeripheralBase;
}

STATIC
EFI_STATUS
EFIAPI
Rp1BusDmaMap(IN RP1_BUS_PROTOCOL *This,
             IN EFI_PCI_IO_PROTOCOL_OPERATION Operation, IN VOID *HostAddress,
             IN OUT UINTN *NumberOfBytes,
             OUT EFI_PHYSICAL_ADDRESS *DeviceAddress, OUT VOID **Mapping) {
  RP1_BUS_DATA *Rp1Data;

  ASSERT(This != NULL);

  Rp1Data = RP1_BUS_DATA_FROM_THIS(This);
  return Rp1Data->PciIo->Map(Rp1Data->PciIo, Operation, HostAddress,
                             NumberOfBytes, DeviceAddress, Mapping);
}

STATIC
EFI_STATUS
EFIAPI
Rp1BusDmaUnmap(IN RP1_BUS_PROTOCOL *This, IN VOID *Mapping) {
  RP1_BUS_DATA *Rp1Data;

  ASSERT(This != NULL);

  Rp1Data = RP1_BUS_DATA_FROM_THIS(This);
  return Rp1Data->PciIo->Unmap(Rp1Data->PciIo, Mapping);
}

STATIC
EFI_STATUS
EFIAPI
Rp1BusDmaAllocateBuffer(IN RP1_BUS_PROTOCOL *This,
                        IN EFI_MEMORY_TYPE MemoryType, IN UINTN Pages,
                        OUT VOID **HostAddress, IN UINT64 Attributes) {
  RP1_BUS_DATA *Rp1Data;

  ASSERT(This != NULL);

  Rp1Data = RP1_BUS_DATA_FROM_THIS(This);
  return Rp1Data->PciIo->AllocateBuffer(
      Rp1Data->PciIo, AllocateAnyPages, MemoryType, Pages, HostAddress,
      Attributes);
}

STATIC
EFI_STATUS
EFIAPI
Rp1BusDmaFreeBuffer(IN RP1_BUS_PROTOCOL *This, IN UINTN Pages,
                    IN VOID *HostAddress) {
  RP1_BUS_DATA *Rp1Data;

  ASSERT(This != NULL);

  Rp1Data = RP1_BUS_DATA_FROM_THIS(This);
  return Rp1Data->PciIo->FreeBuffer(Rp1Data->PciIo, Pages, HostAddress);
}

STATIC
EFI_STATUS
EFIAPI
Rp1BusConfigureFanPrereqs (
  IN RP1_BUS_DATA  *Rp1Data
  )
{
  RP1_GPIO_PIN  FanPin;
  UINT32        Ctrl;
  EFI_STATUS    Status;

  Status = Rp1GpioGetPin (Rp1Data->PeripheralBase, RP1_FAN_PWM_GPIO, &FanPin);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RP1: Failed to resolve fan GPIO. Status=%r\n", Status));
    return Status;
  }

  Rp1ClockWrite32 (
    Rp1Data->PeripheralBase + RP1_CLOCKS_MAIN_BASE,
    RP1_CLK_BLOCK_PWM1,
    RP1_CLK_REG_DIV_INT,
    1
    );
  Rp1ClockWrite32 (
    Rp1Data->PeripheralBase + RP1_CLOCKS_MAIN_BASE,
    RP1_CLK_BLOCK_PWM1,
    RP1_CLK_FRAC_REG_DIV_FRAC,
    0
    );

  Ctrl = Rp1ClockRead32 (
           Rp1Data->PeripheralBase + RP1_CLOCKS_MAIN_BASE,
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
    Rp1Data->PeripheralBase + RP1_CLOCKS_MAIN_BASE,
    RP1_CLK_BLOCK_PWM1,
    RP1_CLK_REG_CTRL,
    Ctrl
    );

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
Rp1BusConfigurePwm1Ch3 (
  IN RP1_BUS_DATA  *Rp1Data
  )
{
  return Rp1PwmConfigure (
           Rp1Data->PeripheralBase + RP1_PWM1_BASE,
           RP1_FAN_PWM_CHANNEL,
           RP1_FAN_PWM_RANGE,
           RP1_FAN_PWM_DUTY,
           RP1_FAN_PWM_INVERTED,
           TRUE
           );
}

EFI_STATUS
EFIAPI
Rp1BusDriverBindingSupported(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                             IN EFI_HANDLE ControllerHandle,
                             IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath) {
  EFI_STATUS Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT32 PciId;

  Status = gBS->OpenProtocol(ControllerHandle, &gEfiPciIoProtocolGuid,
                             (VOID **)&PciIo, This->DriverBindingHandle,
                             ControllerHandle, EFI_OPEN_PROTOCOL_BY_DRIVER);

  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, PCI_VENDOR_ID_OFFSET, 1,
                           &PciId);

  if (EFI_ERROR(Status)) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  if (((PciId & 0xffff) != PCI_VENDOR_ID_RPILTD) ||
      ((PciId >> 16) != PCI_DEVICE_ID_RP1)) {
    Status = EFI_UNSUPPORTED;
  }

Exit:
  gBS->CloseProtocol(ControllerHandle, &gEfiPciIoProtocolGuid,
                     This->DriverBindingHandle, ControllerHandle);

  return Status;
}

EFI_STATUS
EFIAPI
Rp1BusDriverBindingStart(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                         IN EFI_HANDLE ControllerHandle,
                         IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath) {
  EFI_STATUS Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT64 Supports;
  RP1_BUS_DATA *Rp1Data;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *PeripheralDesc;

  Rp1Data = NULL;

  Status = gBS->OpenProtocol(ControllerHandle, &gEfiPciIoProtocolGuid,
                             (VOID **)&PciIo, This->DriverBindingHandle,
                             ControllerHandle, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  Status = PciIo->Attributes(PciIo, EfiPciIoAttributeOperationSupported, 0,
                             &Supports);
  if (!EFI_ERROR(Status)) {
    Supports &= (UINT64)EFI_PCI_DEVICE_ENABLE;
    Status = PciIo->Attributes(PciIo, EfiPciIoAttributeOperationEnable,
                               Supports, NULL);
  }

  if (EFI_ERROR(Status)) {
    DEBUG(
        (DEBUG_ERROR, "RP1: Failed to enable PCI device. Status=%r\n", Status));
    goto Fail;
  }

  Rp1Data = AllocateZeroPool(sizeof(RP1_BUS_DATA));
  if (Rp1Data == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DEBUG((DEBUG_ERROR, "RP1: Failed to allocate device context. Status=%r\n",
           Status));
    goto Fail;
  }

  Rp1Data->Signature = RP1_BUS_DATA_SIGNATURE;
  Rp1Data->ControllerHandle = ControllerHandle;
  Rp1Data->DriverBinding = This;
  Rp1Data->PciIo = PciIo;
  Rp1Data->Rp1Bus.GetPeripheralBase = Rp1BusGetPeripheralBase;
  Rp1Data->Rp1Bus.DmaMap = Rp1BusDmaMap;
  Rp1Data->Rp1Bus.DmaUnmap = Rp1BusDmaUnmap;
  Rp1Data->Rp1Bus.DmaAllocateBuffer = Rp1BusDmaAllocateBuffer;
  Rp1Data->Rp1Bus.DmaFreeBuffer = Rp1BusDmaFreeBuffer;

  Status = PciIo->GetBarAttributes(PciIo, RP1_PERIPHERAL_BAR_INDEX, NULL,
                                   (VOID **)&PeripheralDesc);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "RP1: Failed to get BAR attributes. Status=%r\n",
           Status));
    goto Fail;
  }

  Rp1Data->PeripheralBase = PeripheralDesc->AddrRangeMin;
  FreePool(PeripheralDesc);

  Rp1Data->ChipId = MmioRead32(Rp1Data->PeripheralBase + RP1_SYSINFO_BASE);

  Status = gBS->InstallMultipleProtocolInterfaces(
      &ControllerHandle, &gRp1BusProtocolGuid, &Rp1Data->Rp1Bus, NULL);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "RP1: Failed to install bus protocol. Status=%r\n",
           Status));
    goto Fail;
  }

  DEBUG((DEBUG_INFO, "RP1: chip id %x, peripheral base at CPU address 0x%lx\n",
         Rp1Data->ChipId, Rp1Data->PeripheralBase));

  Rp1BusRegisterDevices(Rp1Data);
  Rp1BusEnableInterrupts(Rp1Data);

  Status = Rp1BusConfigureFanPrereqs (Rp1Data);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RP1: Failed to configure fan prerequisites. Status=%r\n", Status));
  }

  Status = Rp1BusConfigurePwm1Ch3 (Rp1Data);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RP1: Failed to configure PWM1 channel 3. Status=%r\n", Status));
  }

  return EFI_SUCCESS;

Fail:
  gBS->CloseProtocol(ControllerHandle, &gEfiPciIoProtocolGuid,
                     This->DriverBindingHandle, ControllerHandle);

  gBS->UninstallMultipleProtocolInterfaces(
      ControllerHandle, &gRp1BusProtocolGuid, This->DriverBindingHandle,
      ControllerHandle);

  if (Rp1Data != NULL) {
    FreePool(Rp1Data);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Rp1BusUnregisterNonDiscoverableDevice(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                                      IN EFI_HANDLE ControllerHandle,
                                      IN EFI_HANDLE DeviceHandle) {
  EFI_STATUS Status;
  NON_DISCOVERABLE_DEVICE *NonDiscoverableDevice;
  EFI_DEVICE_PATH_PROTOCOL *NonDiscoverableDevicePath;
  RP1_BUS_PROTOCOL *Rp1Bus;

  Status = gBS->OpenProtocol(
      DeviceHandle, &gEdkiiNonDiscoverableDeviceProtocolGuid,
      (VOID **)&NonDiscoverableDevice, This->DriverBindingHandle,
      ControllerHandle, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (EFI_ERROR(Status)) {
    ASSERT_EFI_ERROR(Status);
    return Status;
  }

  Status = gBS->OpenProtocol(DeviceHandle, &gEfiDevicePathProtocolGuid,
                             (VOID **)&NonDiscoverableDevicePath,
                             This->DriverBindingHandle, ControllerHandle,
                             EFI_OPEN_PROTOCOL_GET_PROTOCOL);
  if (EFI_ERROR(Status)) {
    ASSERT_EFI_ERROR(Status);
    return Status;
  }

  Status = gBS->CloseProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                              This->DriverBindingHandle, DeviceHandle);
  ASSERT_EFI_ERROR(Status);

  Status = gBS->UninstallMultipleProtocolInterfaces(
      DeviceHandle, &gEdkiiNonDiscoverableDeviceProtocolGuid,
      NonDiscoverableDevice, &gEfiDevicePathProtocolGuid,
      NonDiscoverableDevicePath, NULL);
  if (EFI_ERROR(Status)) {
    gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid, (VOID **)&Rp1Bus,
                      This->DriverBindingHandle, DeviceHandle,
                      EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
    return Status;
  }

  FreePool(NonDiscoverableDevice);
  FreePool(NonDiscoverableDevicePath);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1BusDriverBindingStop(IN EFI_DRIVER_BINDING_PROTOCOL *This,
                        IN EFI_HANDLE ControllerHandle,
                        IN UINTN NumberOfChildren,
                        IN EFI_HANDLE *DeviceHandleBuffer) {
  EFI_STATUS Status;
  UINTN Index;
  RP1_BUS_PROTOCOL *Rp1Bus;
  BOOLEAN AllChildrenStopped;

  if (NumberOfChildren == 0) {
    DEBUG((DEBUG_INFO, "RP1: Stop bus at %p\n", ControllerHandle));

    Status =
        gBS->OpenProtocol(ControllerHandle, &gRp1BusProtocolGuid,
                          (VOID **)&Rp1Bus, This->DriverBindingHandle,
                          ControllerHandle, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status)) {
      return Status;
    }

    Status = gBS->UninstallMultipleProtocolInterfaces(
        ControllerHandle, &gRp1BusProtocolGuid, Rp1Bus, NULL);
    ASSERT_EFI_ERROR(Status);

    FreePool(RP1_BUS_DATA_FROM_THIS(Rp1Bus));

    Status = gBS->CloseProtocol(ControllerHandle, &gEfiPciIoProtocolGuid,
                                This->DriverBindingHandle, ControllerHandle);
    ASSERT_EFI_ERROR(Status);

    return EFI_SUCCESS;
  }

  AllChildrenStopped = TRUE;

  for (Index = 0; Index < NumberOfChildren; Index++) {
    //
    // We only register non-discoverable PCI devices so far.
    //
    Status = Rp1BusUnregisterNonDiscoverableDevice(This, ControllerHandle,
                                                   DeviceHandleBuffer[Index]);
    if (EFI_ERROR(Status)) {
      AllChildrenStopped = FALSE;
      continue;
    }
  }

  if (!AllChildrenStopped) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL mRp1BusDriverBinding = {
    Rp1BusDriverBindingSupported,
    Rp1BusDriverBindingStart,
    Rp1BusDriverBindingStop,
    0x10,
    NULL,
    NULL};

//
// Component Name Protocol implementation
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE
    mRp1BusDriverNameTable[] = {{"eng;en", L"RP1 Bus Driver"}, {NULL, NULL}};

EFI_STATUS
EFIAPI
Rp1BusComponentNameGetDriverName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                                 IN CHAR8 *Language, OUT CHAR16 **DriverName) {
  return LookupUnicodeString2(Language, This->SupportedLanguages,
                              mRp1BusDriverNameTable, DriverName,
                              (BOOLEAN)(This == &mRp1BusComponentName));
}

EFI_STATUS
EFIAPI
Rp1BusComponentNameGetControllerName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                                     IN EFI_HANDLE ControllerHandle,
                                     IN EFI_HANDLE ChildHandle OPTIONAL,
                                     IN CHAR8 *Language,
                                     OUT CHAR16 **ControllerName) {
  return EFI_UNSUPPORTED;
}

//
// Component Name 2 Protocol implementation
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE
    mRp1BusDriverNameTable2[] = {{"en", L"RP1 Bus Driver"}, {NULL, NULL}};

EFI_STATUS
EFIAPI
Rp1BusComponentName2GetDriverName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                                  IN CHAR8 *Language, OUT CHAR16 **DriverName) {
  return LookupUnicodeString2(Language, This->SupportedLanguages,
                              mRp1BusDriverNameTable2, DriverName,
                              (BOOLEAN)(This == &mRp1BusComponentName2));
}

EFI_STATUS
EFIAPI
Rp1BusComponentName2GetControllerName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                                      IN EFI_HANDLE ControllerHandle,
                                      IN EFI_HANDLE ChildHandle OPTIONAL,
                                      IN CHAR8 *Language,
                                      OUT CHAR16 **ControllerName) {
  return EFI_UNSUPPORTED;
}

EFI_COMPONENT_NAME_PROTOCOL mRp1BusComponentName = {
    Rp1BusComponentNameGetDriverName, Rp1BusComponentNameGetControllerName,
    "eng"};

EFI_COMPONENT_NAME2_PROTOCOL mRp1BusComponentName2 = {
    Rp1BusComponentName2GetDriverName, Rp1BusComponentName2GetControllerName,
    "en"};

EFI_STATUS
EFIAPI
Rp1BusDxeEntryPoint(IN EFI_HANDLE ImageHandle,
                    IN EFI_SYSTEM_TABLE *SystemTable) {
  return EfiLibInstallDriverBindingComponentName2(
      ImageHandle, SystemTable, &mRp1BusDriverBinding, ImageHandle,
      &mRp1BusComponentName, &mRp1BusComponentName2);
}
