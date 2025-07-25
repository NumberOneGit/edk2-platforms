/** @file
  This driver effectuates QSP platform configuration settings and exposes
  them via HII.

  Copyright (C) 2014, Red Hat, Inc.
  Copyright (c) 2009 - 2019, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiHiiServicesLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/HiiConfigAccess.h>
#include <Guid/MdeModuleHii.h>
#include <Guid/SimicsBoardConfig.h>

#include "Platform.h"
#include "PlatformConfig.h"
#include <Library/DxeServicesTableLib.h>
//
// The HiiAddPackages() library function requires that any controller (or
// image) handle, to be associated with the HII packages under installation, be
// "decorated" with a device path. The tradition seems to be a vendor device
// path.
//
// We'd like to associate our HII packages with the driver's image handle. The
// first idea is to use the driver image's device path. Unfortunately, loaded
// images only come with an EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL (not the
// usual EFI_DEVICE_PATH_PROTOCOL), ie. a different GUID. In addition, even the
// EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL interface may be NULL, if the image
// has been loaded from an "unnamed" memory source buffer.
//
// Hence let's just stick with the tradition -- use a dedicated vendor device
// path, with the driver's FILE_GUID.
//
#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH       VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL End;
} PKG_DEVICE_PATH;
#pragma pack()

STATIC PKG_DEVICE_PATH mPkgDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)     ),
        (UINT8) (sizeof (VENDOR_DEVICE_PATH) >> 8)
      }
    },
    EFI_CALLER_ID_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8) (END_DEVICE_PATH_LENGTH     ),
      (UINT8) (END_DEVICE_PATH_LENGTH >> 8)
    }
  }
};

//
// The configuration interface between the HII engine (form display etc) and
// this driver.
//
STATIC EFI_HII_CONFIG_ACCESS_PROTOCOL mConfigAccess;

//
// The handle representing our list of packages after installation.
//
STATIC EFI_HII_HANDLE mInstalledPackages;

//
// The arrays below constitute our HII package list. They are auto-generated by
// the VFR compiler and linked into the driver image during the build.
//
// - The strings package receives its C identifier from the driver's BASE_NAME,
//   plus "Strings".
//
// - The forms package receives its C identifier from the VFR file's basename,
//   plus "Bin".
//
//
extern UINT8 SimicsDxeStrings[];
extern UINT8 PlatformFormsBin[];

//
// We want to be notified about GOP installations until we find one GOP
// interface that lets us populate the form.
//
STATIC EFI_EVENT mGopEvent;

//
// The registration record underneath this pointer allows us to iterate through
// the GOP instances one by one.
//
STATIC VOID *mGopTracker;

//
// Cache the resolutions we get from the GOP.
//
typedef struct {
  UINT32 X;
  UINT32 Y;
} GOP_MODE;

STATIC UINTN    mNumGopModes;
STATIC GOP_MODE *mGopModes;


/**
  Load the persistent platform configuration and translate it to binary form
  state.

  If the platform configuration is missing, then the function fills in a
  default state.

  @param[out] MainFormState  Binary form/widget state after translation.

  @retval EFI_SUCCESS  Form/widget state ready.
  @return              Error codes from underlying functions.
**/
STATIC
EFI_STATUS
EFIAPI
PlatformConfigToFormState (
  OUT MAIN_FORM_STATE *MainFormState
  )
{
  EFI_STATUS      Status;
  PLATFORM_CONFIG PlatformConfig;
  UINT64          OptionalElements;
  UINTN           ModeNumber;

  ZeroMem (MainFormState, sizeof *MainFormState);

  Status = PlatformConfigLoad (&PlatformConfig, &OptionalElements);
  switch (Status) {
  case EFI_SUCCESS:
    if (OptionalElements & PLATFORM_CONFIG_F_GRAPHICS_RESOLUTION) {
      //
      // Format the preferred resolution as text.
      //
      UnicodeSPrintAsciiFormat (
        (CHAR16 *) MainFormState->CurrentPreferredResolution,
        sizeof MainFormState->CurrentPreferredResolution,
        "%Ldx%Ld",
        (INT64) PlatformConfig.HorizontalResolution,
        (INT64) PlatformConfig.VerticalResolution);

      //
      // Try to locate it in the drop-down list too. This may not succeed, but
      // that's fine.
      //
      for (ModeNumber = 0; ModeNumber < mNumGopModes; ++ModeNumber) {
        if (mGopModes[ModeNumber].X == PlatformConfig.HorizontalResolution &&
            mGopModes[ModeNumber].Y == PlatformConfig.VerticalResolution) {
          MainFormState->NextPreferredResolution = (UINT32) ModeNumber;
          break;
        }
      }

      break;
    }
    //
    // fall through otherwise
    //

  case EFI_NOT_FOUND:
    UnicodeSPrintAsciiFormat (
      (CHAR16 *) MainFormState->CurrentPreferredResolution,
      sizeof MainFormState->CurrentPreferredResolution,
      "Unset");
    break;

  default:
    return Status;
  }

  return EFI_SUCCESS;
}


/**
  This function is called by the HII machinery when it fetches the form state.

  See the precise documentation in the UEFI spec.

  @param[in]  This      The Config Access Protocol instance.

  @param[in]  Request   A <ConfigRequest> format UCS-2 string describing the
                        query.

  @param[out] Progress  A pointer into Request on output, identifying the query
                        element where processing failed.

  @param[out] Results   A <MultiConfigAltResp> format UCS-2 string that has
                        all values filled in for the names in the Request
                        string.

  @retval EFI_SUCCESS  Extraction of form state in <MultiConfigAltResp>
                       encoding successful.
  @return              Status codes from underlying functions.

**/
STATIC
EFI_STATUS
EFIAPI
ExtractConfig (
  IN CONST  EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN CONST  EFI_STRING                      Request,
  OUT       EFI_STRING                      *Progress,
  OUT       EFI_STRING                      *Results
)
{
  MAIN_FORM_STATE MainFormState;
  EFI_STATUS      Status;

  DEBUG ((EFI_D_VERBOSE, "%a: Request=\"%s\"\n", __func__, Request));

  Status = PlatformConfigToFormState (&MainFormState);
  if (EFI_ERROR (Status)) {
    *Progress = Request;
    return Status;
  }

  //
  // Answer the textual request keying off the binary form state.
  //
  Status = gHiiConfigRouting->BlockToConfig (gHiiConfigRouting, Request,
                                (VOID *) &MainFormState, sizeof MainFormState,
                                Results, Progress);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: BlockToConfig(): %r, Progress=\"%s\"\n",
      __func__, Status, (Status == EFI_DEVICE_ERROR) ? NULL : *Progress));
  } else {
    DEBUG ((EFI_D_VERBOSE, "%a: Results=\"%s\"\n", __func__, *Results));
  }
  return Status;
}


/**
  Interpret the binary form state and save it as persistent platform
  configuration.

  @param[in] MainFormState  Binary form/widget state to verify and save.

  @retval EFI_SUCCESS  Platform configuration saved.
  @return              Error codes from underlying functions.
**/
STATIC
EFI_STATUS
EFIAPI
FormStateToPlatformConfig (
  IN CONST MAIN_FORM_STATE *MainFormState
  )
{
  EFI_STATUS      Status;
  PLATFORM_CONFIG PlatformConfig;
  CONST GOP_MODE  *GopMode;

  //
  // There's nothing to do with the textual CurrentPreferredResolution field.
  // We verify and translate the selection in the drop-down list.
  //
  if (MainFormState->NextPreferredResolution >= mNumGopModes) {
    return EFI_INVALID_PARAMETER;
  }
  GopMode = mGopModes + MainFormState->NextPreferredResolution;

  ZeroMem (&PlatformConfig, sizeof PlatformConfig);
  PlatformConfig.HorizontalResolution = GopMode->X;
  PlatformConfig.VerticalResolution   = GopMode->Y;

  Status = PlatformConfigSave (&PlatformConfig);
  return Status;
}


/**
  This function is called by the HII machinery when it wants the driver to
  interpret and persist the form state.

  See the precise documentation in the UEFI spec.

  @param[in]  This           The Config Access Protocol instance.

  @param[in]  Configuration  A <ConfigResp> format UCS-2 string describing the
                             form state.

  @param[out] Progress       A pointer into Configuration on output,
                             identifying the element where processing failed.

  @retval EFI_SUCCESS  Configuration verified, state permanent.

  @return              Status codes from underlying functions.
**/
STATIC
EFI_STATUS
EFIAPI
RouteConfig (
  IN CONST  EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN CONST  EFI_STRING                      Configuration,
  OUT       EFI_STRING                      *Progress
)
{
  MAIN_FORM_STATE MainFormState;
  UINTN           BlockSize;
  EFI_STATUS      Status;

  DEBUG ((EFI_D_VERBOSE, "%a: Configuration=\"%s\"\n", __func__,
    Configuration));

  //
  // the "read" step in RMW
  //
  Status = PlatformConfigToFormState (&MainFormState);
  if (EFI_ERROR (Status)) {
    *Progress = Configuration;
    return Status;
  }

  //
  // the "modify" step in RMW
  //
  // (Update the binary form state. This update may be partial, which is why in
  // general we must pre-load the form state from the platform config.)
  //
  BlockSize = sizeof MainFormState;
  Status = gHiiConfigRouting->ConfigToBlock (gHiiConfigRouting, Configuration,
                                (VOID *) &MainFormState, &BlockSize, Progress);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: ConfigToBlock(): %r, Progress=\"%s\"\n",
      __func__, Status,
      (Status == EFI_BUFFER_TOO_SMALL) ? NULL : *Progress));
    return Status;
  }

  //
  // the "write" step in RMW
  //
  Status = FormStateToPlatformConfig (&MainFormState);
  if (EFI_ERROR (Status)) {
    *Progress = Configuration;
  }
  return Status;
}


STATIC
EFI_STATUS
EFIAPI
Callback (
  IN     CONST EFI_HII_CONFIG_ACCESS_PROTOCOL   *This,
  IN     EFI_BROWSER_ACTION                     Action,
  IN     EFI_QUESTION_ID                        QuestionId,
  IN     UINT8                                  Type,
  IN OUT EFI_IFR_TYPE_VALUE                     *Value,
  OUT    EFI_BROWSER_ACTION_REQUEST             *ActionRequest
  )
{
  DEBUG ((EFI_D_VERBOSE, "%a: Action=0x%Lx QuestionId=%d Type=%d\n",
    __func__, (UINT64) Action, QuestionId, Type));

  if (Action != EFI_BROWSER_ACTION_CHANGED) {
    return EFI_UNSUPPORTED;
  }

  switch (QuestionId) {
  case QUESTION_SAVE_EXIT:
    *ActionRequest = EFI_BROWSER_ACTION_REQUEST_FORM_SUBMIT_EXIT;
    break;

  case QUESTION_DISCARD_EXIT:
    *ActionRequest = EFI_BROWSER_ACTION_REQUEST_FORM_DISCARD_EXIT;
    break;

  default:
    break;
  }

  return EFI_SUCCESS;
}


/**
  Query and save all resolutions supported by the GOP.

  @param[in]  Gop          The Graphics Output Protocol instance to query.

  @param[out] NumGopModes  The number of modes supported by the GOP. On output,
                           this parameter will be positive.

  @param[out] GopModes     On output, a dynamically allocated array containing
                           the resolutions returned by the GOP. The caller is
                           responsible for freeing the array after use.

  @retval EFI_UNSUPPORTED       No modes found.
  @retval EFI_OUT_OF_RESOURCES  Failed to allocate GopModes.
  @return                       Error codes from Gop->QueryMode().

**/
STATIC
EFI_STATUS
EFIAPI
QueryGopModes (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
  OUT UINTN                        *NumGopModes,
  OUT GOP_MODE                     **GopModes
  )
{
  EFI_STATUS Status;
  UINT32     ModeNumber;

  if (Gop->Mode->MaxMode == 0) {
    return EFI_UNSUPPORTED;
  }
  *NumGopModes = Gop->Mode->MaxMode;

  *GopModes = AllocatePool (Gop->Mode->MaxMode * sizeof **GopModes);
  if (*GopModes == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (ModeNumber = 0; ModeNumber < Gop->Mode->MaxMode; ++ModeNumber) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN                                SizeOfInfo;

    Status = Gop->QueryMode (Gop, ModeNumber, &SizeOfInfo, &Info);
    if (EFI_ERROR (Status)) {
      goto FreeGopModes;
    }

    (*GopModes)[ModeNumber].X = Info->HorizontalResolution;
    (*GopModes)[ModeNumber].Y = Info->VerticalResolution;
    FreePool (Info);
  }

  return EFI_SUCCESS;

FreeGopModes:
  FreePool (*GopModes);

  return Status;
}


/**
  Create a set of "one-of-many" (ie. "drop down list") option IFR opcodes,
  based on available GOP resolutions, to be placed under a "one-of-many" (ie.
  "drop down list") opcode.

  @param[in]  PackageList   The package list with the formset and form for
                            which the drop down options are produced. Option
                            names are added as new strings to PackageList.

  @param[out] OpCodeBuffer  On output, a dynamically allocated opcode buffer
                            with drop down list options corresponding to GOP
                            resolutions. The caller is responsible for freeing
                            OpCodeBuffer with HiiFreeOpCodeHandle() after use.

  @param[in]  NumGopModes   Number of entries in GopModes.

  @param[in]  GopModes      Array of resolutions retrieved from the GOP.

  @retval EFI_SUCESS  Opcodes have been successfully produced.

  @return             Status codes from underlying functions. PackageList may
                      have been extended with new strings. OpCodeBuffer is
                      unchanged.
**/
STATIC
EFI_STATUS
EFIAPI
CreateResolutionOptions (
  IN  EFI_HII_HANDLE  *PackageList,
  OUT VOID            **OpCodeBuffer,
  IN  UINTN           NumGopModes,
  IN  GOP_MODE        *GopModes
  )
{
  EFI_STATUS Status;
  VOID       *OutputBuffer;
  UINTN      ModeNumber;

  OutputBuffer = HiiAllocateOpCodeHandle ();
  if (OutputBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (ModeNumber = 0; ModeNumber < NumGopModes; ++ModeNumber) {
    CHAR16        Desc[MAXSIZE_RES_CUR];
    EFI_STRING_ID NewString;
    VOID          *OpCode;

    UnicodeSPrintAsciiFormat (Desc, sizeof Desc, "%Ldx%Ld",
      (INT64) GopModes[ModeNumber].X, (INT64) GopModes[ModeNumber].Y);
    NewString = HiiSetString (PackageList, 0 /* new string */, Desc,
                  NULL /* for all languages */);
    if (NewString == 0) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FreeOutputBuffer;
    }
    OpCode = HiiCreateOneOfOptionOpCode (OutputBuffer, NewString,
               0 /* Flags */, EFI_IFR_NUMERIC_SIZE_4, ModeNumber);
    if (OpCode == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FreeOutputBuffer;
    }
  }

  *OpCodeBuffer = OutputBuffer;
  return EFI_SUCCESS;

FreeOutputBuffer:
  HiiFreeOpCodeHandle (OutputBuffer);

  return Status;
}


/**
  Populate the form identified by the (PackageList, FormSetGuid, FormId)
  triplet.

  The drop down list of video resolutions is generated from (NumGopModes,
  GopModes).

  @retval EFI_SUCESS  Form successfully updated.
  @return             Status codes from underlying functions.

**/
STATIC
EFI_STATUS
EFIAPI
PopulateForm (
  IN  EFI_HII_HANDLE  *PackageList,
  IN  EFI_GUID        *FormSetGuid,
  IN  EFI_FORM_ID     FormId,
  IN  UINTN           NumGopModes,
  IN  GOP_MODE        *GopModes
  )
{
  EFI_STATUS         Status;
  VOID               *OpCodeBuffer;
  VOID               *OpCode;
  EFI_IFR_GUID_LABEL *Anchor;
  VOID               *OpCodeBuffer2;

  OpCodeBuffer2 = NULL;

  //
  // 1. Allocate an empty opcode buffer.
  //
  OpCodeBuffer = HiiAllocateOpCodeHandle ();
  if (OpCodeBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // 2. Create a label opcode (which is a Tiano extension) inside the buffer.
  // The label's number must match the "anchor" label in the form.
  //
  OpCode = HiiCreateGuidOpCode (OpCodeBuffer, &gEfiIfrTianoGuid,
             NULL /* optional copy origin */, sizeof *Anchor);
  if (OpCode == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeOpCodeBuffer;
  }
  Anchor               = OpCode;
  Anchor->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  Anchor->Number       = LABEL_RES_NEXT;

  //
  // 3. Create the opcodes inside the buffer that are to be inserted into the
  // form.
  //
  // 3.1. Get a list of resolutions.
  //
  Status = CreateResolutionOptions (PackageList, &OpCodeBuffer2,
             NumGopModes, GopModes);
  if (EFI_ERROR (Status)) {
    goto FreeOpCodeBuffer;
  }

  //
  // 3.2. Create a one-of-many question with the above options.
  //
  OpCode = HiiCreateOneOfOpCode (
             OpCodeBuffer,                        // create opcode inside this
                                                  //   opcode buffer,
             QUESTION_RES_NEXT,                   // ID of question,
             FORMSTATEID_MAIN_FORM,               // identifies form state
                                                  //   storage,
             (UINT16) OFFSET_OF (MAIN_FORM_STATE, // value of question stored
                        NextPreferredResolution), //   at this offset,
             STRING_TOKEN (STR_RES_NEXT),         // Prompt,
             STRING_TOKEN (STR_RES_NEXT_HELP),    // Help,
             0,                                   // QuestionFlags,
             EFI_IFR_NUMERIC_SIZE_4,              // see sizeof
                                                  //   NextPreferredResolution,
             OpCodeBuffer2,                       // buffer with possible
                                                  //   choices,
             NULL                                 // DEFAULT opcodes
             );
  if (OpCode == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeOpCodeBuffer2;
  }

  //
  // 4. Update the form with the opcode buffer.
  //
  Status = HiiUpdateForm (PackageList, FormSetGuid, FormId,
             OpCodeBuffer, // buffer with head anchor, and new contents to be
                           // inserted at it
             NULL          // buffer with tail anchor, for deleting old
                           // contents up to it
             );

FreeOpCodeBuffer2:
  HiiFreeOpCodeHandle (OpCodeBuffer2);

FreeOpCodeBuffer:
  HiiFreeOpCodeHandle (OpCodeBuffer);

  return Status;
}


/**
  Load and execute the platform configuration.

  @retval EFI_SUCCESS            Configuration loaded and executed.
  @return                        Status codes from PlatformConfigLoad().
**/
STATIC
EFI_STATUS
EFIAPI
ExecutePlatformConfig (
  VOID
  )
{
  EFI_STATUS      Status;
  PLATFORM_CONFIG PlatformConfig;
  UINT64          OptionalElements;

  Status = PlatformConfigLoad (&PlatformConfig, &OptionalElements);
  if (EFI_ERROR (Status)) {
    DEBUG (((Status == EFI_NOT_FOUND) ? EFI_D_VERBOSE : EFI_D_ERROR,
      "%a: failed to load platform config: %r\n", __func__, Status));
    return Status;
  }

  if (OptionalElements & PLATFORM_CONFIG_F_GRAPHICS_RESOLUTION) {
    //
    // Pass the preferred resolution to GraphicsConsoleDxe via dynamic PCDs.
    //
    PcdSet32S (PcdVideoHorizontalResolution,
      PlatformConfig.HorizontalResolution);
    PcdSet32S (PcdVideoVerticalResolution,
      PlatformConfig.VerticalResolution);
  }

  return EFI_SUCCESS;
}


/**
  Notification callback for GOP interface installation.

  @param[in] Event    Event whose notification function is being invoked.

  @param[in] Context  The pointer to the notification function's context, which
                      is implementation-dependent.
**/
STATIC
VOID
EFIAPI
GopInstalled (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  EFI_STATUS                   Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

  ASSERT (Event == mGopEvent);

  //
  // Check further GOPs.
  //
  for (;;) {
    mNumGopModes = 0;
    mGopModes = NULL;

    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, mGopTracker,
                    (VOID **) &Gop);
    if (EFI_ERROR (Status)) {
      return;
    }

    Status = QueryGopModes (Gop, &mNumGopModes, &mGopModes);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = PopulateForm (mInstalledPackages, &gSimicsBoardConfigGuid,
               FORMID_MAIN_FORM, mNumGopModes, mGopModes);
    if (EFI_ERROR (Status)) {
      FreePool (mGopModes);
      continue;
    }

    break;
  }

  //
  // Success -- so uninstall this callback. Closing the event removes all
  // pending notifications and all protocol registrations.
  //
  Status = gBS->CloseEvent (mGopEvent);
  ASSERT_EFI_ERROR (Status);
  mGopEvent = NULL;
  mGopTracker = NULL;
}


/**
  Entry point for this driver.

  @param[in] ImageHandle  Image handle of this driver.
  @param[in] SystemTable  Pointer to SystemTable.

  @retval EFI_SUCESS            Driver has loaded successfully.
  @retval EFI_OUT_OF_RESOURCES  Failed to install HII packages.
  @return                       Error codes from lower level functions.

**/
EFI_STATUS
EFIAPI
PlatformInit (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;

  ExecutePlatformConfig ();

  mConfigAccess.ExtractConfig = &ExtractConfig;
  mConfigAccess.RouteConfig   = &RouteConfig;
  mConfigAccess.Callback      = &Callback;

  //
  // Declare ourselves suitable for HII communication.
  //
  Status = gBS->InstallMultipleProtocolInterfaces (&ImageHandle,
                  &gEfiDevicePathProtocolGuid,      &mPkgDevicePath,
                  &gEfiHiiConfigAccessProtocolGuid, &mConfigAccess,
                  NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Publish the HII package list to HII Database.
  //
  mInstalledPackages = HiiAddPackages (
                         &gEfiCallerIdGuid,  // PackageListGuid
                         ImageHandle,        // associated DeviceHandle
                         SimicsDxeStrings,   // 1st package
                         PlatformFormsBin,   // 2nd package
                         NULL                // terminator
                         );
  if (mInstalledPackages == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto UninstallProtocols;
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, &GopInstalled,
                  NULL /* Context */, &mGopEvent);
  if (EFI_ERROR (Status)) {
    goto RemovePackages;
  }

  Status = gBS->RegisterProtocolNotify (&gEfiGraphicsOutputProtocolGuid,
                  mGopEvent, &mGopTracker);
  if (EFI_ERROR (Status)) {
    goto CloseGopEvent;
  }

  //
  // Check already installed GOPs.
  //
  Status = gBS->SignalEvent (mGopEvent);
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;

CloseGopEvent:
  gBS->CloseEvent (mGopEvent);

RemovePackages:
  HiiRemovePackages (mInstalledPackages);

UninstallProtocols:
  gBS->UninstallMultipleProtocolInterfaces (ImageHandle,
         &gEfiDevicePathProtocolGuid,      &mPkgDevicePath,
         &gEfiHiiConfigAccessProtocolGuid, &mConfigAccess,
         NULL);
  return Status;
}

/**
  Unload the driver.

  @param[in]  ImageHandle  Handle that identifies the image to evict.

  @retval EFI_SUCCESS  The image has been unloaded.
**/
EFI_STATUS
EFIAPI
PlatformUnload (
  IN  EFI_HANDLE  ImageHandle
  )
{
  if (mGopEvent == NULL) {
    //
    // The GOP callback ran successfully and unregistered itself. Release the
    // resources allocated there.
    //
    ASSERT (mGopModes != NULL);
    FreePool (mGopModes);
  } else {
    //
    // Otherwise we need to unregister the callback.
    //
    ASSERT (mGopModes == NULL);
    gBS->CloseEvent (mGopEvent);
  }

  //
  // Release resources allocated by the entry point.
  //
  HiiRemovePackages (mInstalledPackages);
  gBS->UninstallMultipleProtocolInterfaces (ImageHandle,
         &gEfiDevicePathProtocolGuid,      &mPkgDevicePath,
         &gEfiHiiConfigAccessProtocolGuid, &mConfigAccess,
         NULL);
  return EFI_SUCCESS;
}
