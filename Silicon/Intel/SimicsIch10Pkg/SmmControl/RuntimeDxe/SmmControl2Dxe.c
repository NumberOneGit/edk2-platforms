/** @file
  A DXE_RUNTIME_DRIVER providing synchronous SMI activations via the
  EFI_SMM_CONTROL2_PROTOCOL.

  We expect the PEI phase to have covered the following:
  - ensure that the underlying QEMU machine type be X58
    (responsible: OvmfPkg/SmmAccess/SmmAccessPei.inf)
  - ensure that the ACPI PM IO space be configured
    (responsible: OvmfPkg/PlatformPei/PlatformPei.inf)

  Our own entry point is responsible for confirming the SMI feature and for
  configuring it.

  Copyright (C) 2013, 2015, Red Hat, Inc.<BR>
  Copyright (c) 2009 - 2019, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Register/X58Ich10.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/PciLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/S3SaveState.h>
#include <Protocol/SmmControl2.h>

//
// Forward declaration.
//
STATIC
VOID
EFIAPI
OnS3SaveStateInstalled (
  IN EFI_EVENT Event,
  IN VOID      *Context
  );

//
// The absolute IO port address of the SMI Control and Enable Register. It is
// only used to carry information from the entry point function to the
// S3SaveState protocol installation callback, strictly before the runtime
// phase.
//
STATIC UINTN mSmiEnable;

//
// Event signaled when an S3SaveState protocol interface is installed.
//
STATIC EFI_EVENT mS3SaveStateInstalled;

/**
  Clear the SMI status

  @retval EFI_SUCCESS             The function completes successfully
  @retval EFI_DEVICE_ERROR        Something error occurred

**/
EFI_STATUS
EFIAPI
SmmClear(
  VOID
)
{
  EFI_STATUS  Status;
  UINT32      OutputData;
  UINT32      OutputPort;
  UINT32     PmBase;

  Status = EFI_SUCCESS;
  PmBase = ICH10_PMBASE_IO;

  OutputPort = PmBase + ICH10_PMBASE_OFS_SMI_STS;
  OutputData = ICH10_SMI_STS_APM;
  IoWrite32(
    (UINTN)OutputPort,
    (UINT32)(OutputData)
  );

  ///
  /// Set the EOS Bit
  ///
  OutputPort = PmBase + ICH10_PMBASE_OFS_SMI_EN;
  OutputData = IoRead32((UINTN)OutputPort);
  OutputData |= ICH10_SMI_EN_EOS;
  IoWrite32(
    (UINTN)OutputPort,
    (UINT32)(OutputData)
  );

  ///
  /// There is no need to read EOS back and check if it is set.
  /// This can lead to a reading of zero if an SMI occurs right after the SMI_EN port read
  /// but before the data is returned to the CPU.
  /// SMM Dispatcher should make sure that EOS is set after all SMI sources are processed.
  ///
  return Status;
}

/**
  Invokes SMI activation from either the preboot or runtime environment.

  This function generates an SMI.

  @param[in]     This                The EFI_SMM_CONTROL2_PROTOCOL instance.
  @param[in,out] CommandPort         The value written to the command port.
  @param[in,out] DataPort            The value written to the data port.
  @param[in]     Periodic            Optional mechanism to engender a periodic
                                     stream.
  @param[in]     ActivationInterval  Optional parameter to repeat at this
                                     period one time or, if the Periodic
                                     Boolean is set, periodically.

  @retval EFI_SUCCESS            The SMI/PMI has been engendered.
  @retval EFI_DEVICE_ERROR       The timing is unsupported.
  @retval EFI_INVALID_PARAMETER  The activation period is unsupported.
  @retval EFI_INVALID_PARAMETER  The last periodic activation has not been
                                 cleared.
  @retval EFI_NOT_STARTED        The SMM base service has not been initialized.
**/
STATIC
EFI_STATUS
EFIAPI
SmmControl2DxeTrigger (
  IN CONST EFI_SMM_CONTROL2_PROTOCOL  *This,
  IN OUT UINT8                        *CommandPort       OPTIONAL,
  IN OUT UINT8                        *DataPort          OPTIONAL,
  IN BOOLEAN                          Periodic           OPTIONAL,
  IN UINTN                            ActivationInterval OPTIONAL
  )
{
  //
  // No support for queued or periodic activation.
  //
  if (Periodic || ActivationInterval > 0) {
    return EFI_DEVICE_ERROR;
  }
  ///
  /// Clear any pending the APM SMI
  ///
  SmmClear();
  //
  // The so-called "Advanced Power Management Status Port Register" is in fact
  // a generic data passing register, between the caller and the SMI
  // dispatcher. The ICH9 spec calls it "scratchpad register" --  calling it
  // "status" elsewhere seems quite the misnomer. Status registers usually
  // report about hardware status, while this register is fully governed by
  // software.
  //
  // Write to the status register first, as this won't trigger the SMI just
  // yet. Then write to the control register.
  //
  IoWrite8 (ICH10_APM_STS, DataPort    == NULL ? 0 : *DataPort);
  IoWrite8 (ICH10_APM_CNT, CommandPort == NULL ? 0 : *CommandPort);
  return EFI_SUCCESS;
}

/**
  Clears any system state that was created in response to the Trigger() call.

  This function acknowledges and causes the deassertion of the SMI activation
  source.

  @param[in] This                The EFI_SMM_CONTROL2_PROTOCOL instance.
  @param[in] Periodic            Optional parameter to repeat at this period
                                 one time

  @retval EFI_SUCCESS            The SMI/PMI has been engendered.
  @retval EFI_DEVICE_ERROR       The source could not be cleared.
  @retval EFI_INVALID_PARAMETER  The service did not support the Periodic input
                                 argument.
**/
STATIC
EFI_STATUS
EFIAPI
SmmControl2DxeClear (
  IN CONST EFI_SMM_CONTROL2_PROTOCOL  *This,
  IN BOOLEAN                          Periodic OPTIONAL
  )
{
  if (Periodic) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The PI spec v1.4 explains that Clear() is only supposed to clear software
  // status; it is not in fact responsible for deasserting the SMI. It gives
  // two reasons for this: (a) many boards clear the SMI automatically when
  // entering SMM, (b) if Clear() actually deasserted the SMI, then it could
  // incorrectly suppress an SMI that was asynchronously asserted between the
  // last return of the SMI handler and the call made to Clear().
  //
  // In fact QEMU automatically deasserts CPU_INTERRUPT_SMI in:
  // - x86_cpu_exec_interrupt() [target-i386/seg_helper.c], and
  // - kvm_arch_pre_run() [target-i386/kvm.c].
  //
  // So, nothing to do here.
  //
  SmmClear();

  return EFI_SUCCESS;
}

STATIC EFI_SMM_CONTROL2_PROTOCOL mControl2 = {
  &SmmControl2DxeTrigger,
  &SmmControl2DxeClear,
  MAX_UINTN // MinimumTriggerPeriod -- we don't support periodic SMIs
};

//
// Entry point of this driver.
//
EFI_STATUS
EFIAPI
SmmControl2DxeEntryPoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  UINT32     PmBase;
  UINT32     SmiEnableVal;
  EFI_STATUS Status;

  //
  // This module should only be included if SMRAM support is required.
  //
  ASSERT (FeaturePcdGet (PcdSmmSmramRequire));

  //
  // Calculate the absolute IO port address of the SMI Control and Enable
  // Register. (As noted at the top, the PEI phase has left us with a working
  // ACPI PM IO space.)
  //
  PmBase = PciRead32 (POWER_MGMT_REGISTER_ICH10 (ICH10_PMBASE)) &
             ICH10_PMBASE_MASK;
  mSmiEnable = PmBase + ICH10_PMBASE_OFS_SMI_EN;

  //
  // If APMC_EN is pre-set in SMI_EN, that's QEMU's way to tell us that SMI
  // support is not available. (For example due to KVM lacking it.) Otherwise,
  // this bit is clear after each reset.
  //
  SmiEnableVal = IoRead32 (mSmiEnable);
  if ((SmiEnableVal & ICH10_SMI_EN_APMC_EN) != 0) {
    DEBUG ((EFI_D_ERROR, "%a: this X58 implementation lacks SMI\n",
      __func__));
  }

  //
  // Otherwise, configure the board to inject an SMI when ICH10_APM_CNT is
  // written to. (See the Trigger() method above.)
  //
  SmiEnableVal |= ICH10_SMI_EN_APMC_EN | ICH10_SMI_EN_GBL_SMI_EN;
  IoWrite32 (mSmiEnable, SmiEnableVal);

  //
  // Prevent software from undoing the above (until platform reset).
  //
  PciOr16 (POWER_MGMT_REGISTER_ICH10 (ICH10_GEN_PMCON_1),
            ICH10_GEN_PMCON_1_SMI_LOCK);

  //
  // If we can clear GBL_SMI_EN now, that means QEMU's SMI support is not
  // appropriate.
  //
  IoWrite32 (mSmiEnable, SmiEnableVal & ~(UINT32)ICH10_SMI_EN_GBL_SMI_EN);
  if (IoRead32 (mSmiEnable) != SmiEnableVal) {
    DEBUG ((EFI_D_ERROR, "%a: failed to lock down GBL_SMI_EN\n",
      __func__));
    goto FatalError;
  }

  VOID *Registration;

  //
  // On S3 resume the above register settings have to be repeated. Register a
  // protocol notify callback that, when boot script saving becomes
  // available, saves operations equivalent to the above to the boot script.
  //
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  OnS3SaveStateInstalled, NULL /* Context */,
                  &mS3SaveStateInstalled);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: CreateEvent: %r\n", __func__, Status));
    goto FatalError;
  }

  Status = gBS->RegisterProtocolNotify (&gEfiS3SaveStateProtocolGuid,
                  mS3SaveStateInstalled, &Registration);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: RegisterProtocolNotify: %r\n", __func__,
      Status));
    goto ReleaseEvent;
  }

  //
  // Kick the event right now -- maybe the boot script is already saveable.
  //
  Status = gBS->SignalEvent (mS3SaveStateInstalled);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: SignalEvent: %r\n", __func__, Status));
    goto ReleaseEvent;
  }

  //
  // We have no pointers to convert to virtual addresses. The handle itself
  // doesn't matter, as protocol services are not accessible at runtime.
  //
  Status = gBS->InstallMultipleProtocolInterfaces (&ImageHandle,
                  &gEfiSmmControl2ProtocolGuid, &mControl2,
                  NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: InstallMultipleProtocolInterfaces: %r\n",
      __func__, Status));
    goto ReleaseEvent;
  }

  return EFI_SUCCESS;

ReleaseEvent:
  if (mS3SaveStateInstalled != NULL) {
    gBS->CloseEvent (mS3SaveStateInstalled);
  }

FatalError:
  //
  // We really don't want to continue in this case.
  //
  ASSERT (FALSE);
  CpuDeadLoop ();
  return EFI_UNSUPPORTED;
}

/**
  Notification callback for S3SaveState installation.

  @param[in] Event    Event whose notification function is being invoked.

  @param[in] Context  The pointer to the notification function's context, which
                      is implementation-dependent.
**/
STATIC
VOID
EFIAPI
OnS3SaveStateInstalled (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  EFI_STATUS                 Status;
  EFI_S3_SAVE_STATE_PROTOCOL *S3SaveState;
  UINT32                     SmiEnOrMask, SmiEnAndMask;
  UINT16                     GenPmCon1OrMask, GenPmCon1AndMask;

  ASSERT (Event == mS3SaveStateInstalled);

  Status = gBS->LocateProtocol (&gEfiS3SaveStateProtocolGuid,
                  NULL /* Registration */, (VOID **)&S3SaveState);
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // These operations were originally done, verified and explained in the entry
  // point function of the driver.
  //
  SmiEnOrMask  = ICH10_SMI_EN_APMC_EN | ICH10_SMI_EN_GBL_SMI_EN;
  SmiEnAndMask = MAX_UINT32;
  Status = S3SaveState->Write (
                          S3SaveState,
                          EFI_BOOT_SCRIPT_IO_READ_WRITE_OPCODE,
                          EfiBootScriptWidthUint32,
                          (UINT64)mSmiEnable,
                          &SmiEnOrMask,
                          &SmiEnAndMask
                          );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: EFI_BOOT_SCRIPT_IO_READ_WRITE_OPCODE: %r\n",
      __func__, Status));
    ASSERT (FALSE);
    CpuDeadLoop ();
  }

  GenPmCon1OrMask  = ICH10_GEN_PMCON_1_SMI_LOCK;
  GenPmCon1AndMask = MAX_UINT16;
  Status = S3SaveState->Write (
                          S3SaveState,
                          EFI_BOOT_SCRIPT_PCI_CONFIG_READ_WRITE_OPCODE,
                          EfiBootScriptWidthUint16,
                          (UINT64)POWER_MGMT_REGISTER_ICH10 (ICH10_GEN_PMCON_1),
                          &GenPmCon1OrMask,
                          &GenPmCon1AndMask
                          );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
      "%a: EFI_BOOT_SCRIPT_PCI_CONFIG_READ_WRITE_OPCODE: %r\n", __func__,
      Status));
    ASSERT (FALSE);
    CpuDeadLoop ();
  }

  DEBUG ((EFI_D_VERBOSE, "%a: boot script fragment saved\n", __func__));
  gBS->CloseEvent (Event);
  mS3SaveStateInstalled = NULL;
}
