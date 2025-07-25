/** @file
  Install a callback when necessary for setting the Feature Control MSR on all
  processors.

  Copyright (C) 2016, Red Hat, Inc.
  Copyright (c) 2019 Intel Corporation. All rights reserved. <BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/DebugLib.h>
#include <Library/PeiServicesLib.h>
#include <Ppi/MpServices.h>
#include <Register/Intel/Msr/Core2Msr.h>

#include "Platform.h"

//
// The value to be written to the Feature Control MSR, retrieved from fw_cfg.
//
STATIC UINT64 mFeatureControlValue = 0x00000005;

/**
  Write the Feature Control MSR on an Application Processor or the Boot
  Processor.

  All APs execute this function in parallel. The BSP executes the function
  separately.

  @param[in,out] WorkSpace  Pointer to the input/output argument workspace
                            shared by all processors.
**/
STATIC
VOID
EFIAPI
WriteFeatureControl (
  IN OUT VOID *WorkSpace
  )
{
  AsmWriteMsr64 (MSR_CORE2_FEATURE_CONTROL, mFeatureControlValue);
}

/**
  Notification function called when EFI_PEI_MP_SERVICES_PPI becomes available.

  @param[in] PeiServices      Indirect reference to the PEI Services Table.
  @param[in] NotifyDescriptor Address of the notification descriptor data
                              structure.
  @param[in] Ppi              Address of the PPI that was installed.

  @return  Status of the notification. The status code returned from this
           function is ignored.
**/
STATIC
EFI_STATUS
EFIAPI
OnMpServicesAvailable (
  IN EFI_PEI_SERVICES           **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
  )
{
  EFI_PEI_MP_SERVICES_PPI *MpServices;
  EFI_STATUS              Status;

  DEBUG ((EFI_D_VERBOSE, "%a: %a\n", gEfiCallerBaseName, __func__));
  //
  // Write the MSR on all the APs in parallel.
  //
  MpServices = Ppi;
  Status = MpServices->StartupAllAPs (
                         (CONST EFI_PEI_SERVICES **)PeiServices,
                         MpServices,
                         WriteFeatureControl, // Procedure
                         FALSE,               // SingleThread
                         0,                   // TimeoutInMicroSeconds: inf.
                         NULL                 // ProcedureArgument
                         );
  if (EFI_ERROR (Status) && Status != EFI_NOT_STARTED) {
    DEBUG ((EFI_D_ERROR, "%a: StartupAllAps(): %r\n", __func__, Status));
    return Status;
  }

  //
  // Now write the MSR on the BSP too.
  //
  WriteFeatureControl (NULL);

  return EFI_SUCCESS;
}

//
// Notification object for registering the callback, for when
// EFI_PEI_MP_SERVICES_PPI becomes available.
//
STATIC CONST EFI_PEI_NOTIFY_DESCRIPTOR mMpServicesNotify = {
  EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | // Flags
  EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gEfiPeiMpServicesPpiGuid,               // Guid
  OnMpServicesAvailable                    // Notify
};

VOID
InstallFeatureControlCallback (
  VOID
  )
{
  EFI_STATUS           Status;

  Status = PeiServicesNotifyPpi (&mMpServicesNotify);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: failed to set up MP Services callback: %r\n",
      __func__, Status));
  }
}
