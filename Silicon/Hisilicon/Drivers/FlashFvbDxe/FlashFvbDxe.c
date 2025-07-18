/** @file
*
*  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
*  Copyright (c) 2015, Hisilicon Limited. All rights reserved.
*  Copyright (c) 2015, Linaro Limited. All rights reserved.
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
* Based on files under ArmPlatformPkg/Drivers/NorFlashDxe/
**/

#include "FlashFvbDxe.h"
STATIC EFI_EVENT mFlashFvbVirtualAddrChangeEvent;
STATIC UINTN     mFlashNvStorageVariableBase;


//
// Global variable declarations
//

FLASH_DESCRIPTION mFlashDevices[FLASH_DEVICE_COUNT] =
{
    {
        // UEFI Variable Services non-volatile storage
        FixedPcdGet64 (PcdSFCMEM0BaseAddress),
        FixedPcdGet64 (PcdFlashNvStorageVariableBase64),
        0x20000,
        SIZE_64KB,
        {0xCC2CBF29, 0x1498, 0x4CDD, {0x81, 0x71, 0xF8, 0xB6, 0xB4, 0x1D, 0x09, 0x09}}
    }

};

FLASH_INSTANCE** mFlashInstances;

FLASH_INSTANCE  mFlashInstanceTemplate =
{
    FLASH_SIGNATURE, // Signature
    NULL, // Handle ... NEED TO BE FILLED

    FALSE, // Initialized
    NULL, // Initialize

    0, // DeviceBaseAddress ... NEED TO BE FILLED
    0, // RegionBaseAddress ... NEED TO BE FILLED
    0, // Size ... NEED TO BE FILLED
    0, // StartLba

    {
        EFI_BLOCK_IO_PROTOCOL_REVISION2, // Revision
        NULL, // Media ... NEED TO BE FILLED
        NULL, //NorFlashBlockIoReset
        FlashBlockIoReadBlocks,
        FlashBlockIoWriteBlocks,
        FlashBlockIoFlushBlocks
    }, // BlockIoProtocol

    {
        0, // MediaId ... NEED TO BE FILLED
        FALSE, // RemovableMedia
        TRUE, // MediaPresent
        FALSE, // LogicalPartition
        FALSE, // ReadOnly
        FALSE, // WriteCaching;
        SIZE_64KB, // BlockSize ... NEED TO BE FILLED
        4, //  IoAlign
        0, // LastBlock ... NEED TO BE FILLED
        0, // LowestAlignedLba
        1, // LogicalBlocksPerPhysicalBlock
    }, //Media;

    FALSE, // SupportFvb ... NEED TO BE FILLED
    {
        FvbGetAttributes,
        FvbSetAttributes,
        FvbGetPhysicalAddress,
        FvbGetBlockSize,
        FvbRead,
        FvbWrite,
        FvbEraseBlocks,
        NULL, //ParentHandle
    }, //  FvbProtoccol;

    {
        {
            {
                HARDWARE_DEVICE_PATH,
                HW_VENDOR_DP,
                {(UINT8)(sizeof(VENDOR_DEVICE_PATH)),
                (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)},
            },
            { 0x0, 0x0, 0x0, {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }}, // GUID ... NEED TO BE FILLED
        },
        {
            END_DEVICE_PATH_TYPE,
            END_ENTIRE_DEVICE_PATH_SUBTYPE,
            {sizeof (EFI_DEVICE_PATH_PROTOCOL),
            0}
        }
    } // DevicePath
};

HISI_SPI_FLASH_PROTOCOL* mFlash;

///
/// The Firmware Volume Block Protocol is the low-level interface
/// to a firmware volume. File-level access to a firmware volume
/// should not be done using the Firmware Volume Block Protocol.
/// Normal access to a firmware volume must use the Firmware
/// Volume Protocol. Typically, only the file system driver that
/// produces the Firmware Volume Protocol will bind to the
/// Firmware Volume Block Protocol.
///

/**
  Initialises the FV Header and Variable Store Header
  to support variable operations.

  @param[in]  Ptr - Location to initialise the headers

**/
EFI_STATUS
InitializeFvAndVariableStoreHeaders (
    IN FLASH_INSTANCE* Instance
)
{
    EFI_STATUS                          Status;
    VOID*                               Headers;
    UINTN                               HeadersLength;
    EFI_FIRMWARE_VOLUME_HEADER*          FirmwareVolumeHeader;
    VARIABLE_STORE_HEADER*               VariableStoreHeader;

    if (!Instance->Initialized && Instance->Initialize)
    {
        Instance->Initialize (Instance);
    }

    HeadersLength = sizeof(EFI_FIRMWARE_VOLUME_HEADER) + sizeof(EFI_FV_BLOCK_MAP_ENTRY) + sizeof(VARIABLE_STORE_HEADER);
    Headers = AllocateZeroPool(HeadersLength);

    // FirmwareVolumeHeader->FvLength is declared to have the Variable area AND the FTW working area AND the FTW Spare contiguous.
    ASSERT(PcdGet64(PcdFlashNvStorageVariableBase64) + PcdGet32(PcdFlashNvStorageVariableSize) == PcdGet64(PcdFlashNvStorageFtwWorkingBase64));
    ASSERT(PcdGet64(PcdFlashNvStorageFtwWorkingBase64) + PcdGet32(PcdFlashNvStorageFtwWorkingSize) == PcdGet64(PcdFlashNvStorageFtwSpareBase64));

    // Check if the size of the area is at least one block size
    ASSERT((PcdGet32(PcdFlashNvStorageVariableSize) > 0) && ((UINT32)PcdGet32(PcdFlashNvStorageVariableSize) / Instance->Media.BlockSize > 0));
    ASSERT((PcdGet32(PcdFlashNvStorageFtwWorkingSize) > 0) && ((UINT32)PcdGet32(PcdFlashNvStorageFtwWorkingSize) / Instance->Media.BlockSize > 0));
    ASSERT((PcdGet32(PcdFlashNvStorageFtwSpareSize) > 0) && ((UINT32)PcdGet32(PcdFlashNvStorageFtwSpareSize) / Instance->Media.BlockSize > 0));

    // Ensure the Variable area Base Addresses are aligned on a block size boundaries
    ASSERT((UINT32)PcdGet64(PcdFlashNvStorageVariableBase64) % Instance->Media.BlockSize == 0);
    ASSERT((UINT32)PcdGet64(PcdFlashNvStorageFtwWorkingBase64) % Instance->Media.BlockSize == 0);
    ASSERT((UINT32)PcdGet64(PcdFlashNvStorageFtwSpareBase64) % Instance->Media.BlockSize == 0);

    //
    // EFI_FIRMWARE_VOLUME_HEADER
    //
    FirmwareVolumeHeader = (EFI_FIRMWARE_VOLUME_HEADER*)Headers;
    CopyGuid (&FirmwareVolumeHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid);
    FirmwareVolumeHeader->FvLength =
        PcdGet32(PcdFlashNvStorageVariableSize) +
        PcdGet32(PcdFlashNvStorageFtwWorkingSize) +
        PcdGet32(PcdFlashNvStorageFtwSpareSize);
    FirmwareVolumeHeader->Signature = EFI_FVH_SIGNATURE;
    FirmwareVolumeHeader->Attributes = (EFI_FVB_ATTRIBUTES_2) (
                                           EFI_FVB2_READ_ENABLED_CAP   | // Reads may be enabled
                                           EFI_FVB2_READ_STATUS        | // Reads are currently enabled
                                           EFI_FVB2_STICKY_WRITE       | // A block erase is required to flip bits into EFI_FVB2_ERASE_POLARITY
                                           EFI_FVB2_MEMORY_MAPPED      | // It is memory mapped
                                           EFI_FVB2_ERASE_POLARITY     | // After erasure all bits take this value (i.e. '1')
                                           EFI_FVB2_WRITE_STATUS       | // Writes are currently enabled
                                           EFI_FVB2_WRITE_ENABLED_CAP    // Writes may be enabled
                                       );
    FirmwareVolumeHeader->HeaderLength = sizeof(EFI_FIRMWARE_VOLUME_HEADER) + sizeof(EFI_FV_BLOCK_MAP_ENTRY);
    FirmwareVolumeHeader->Revision = EFI_FVH_REVISION;
    FirmwareVolumeHeader->BlockMap[0].NumBlocks = Instance->Media.LastBlock + 1;
    FirmwareVolumeHeader->BlockMap[0].Length      = Instance->Media.BlockSize;
    FirmwareVolumeHeader->BlockMap[1].NumBlocks = 0;
    FirmwareVolumeHeader->BlockMap[1].Length      = 0;
    FirmwareVolumeHeader->Checksum = CalculateCheckSum16 ((UINT16*)FirmwareVolumeHeader, FirmwareVolumeHeader->HeaderLength);

    //
    // VARIABLE_STORE_HEADER
    //
    VariableStoreHeader = (VARIABLE_STORE_HEADER*)((UINTN)Headers + (UINTN)FirmwareVolumeHeader->HeaderLength);
    CopyGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid);
    VariableStoreHeader->Size = PcdGet32(PcdFlashNvStorageVariableSize) - FirmwareVolumeHeader->HeaderLength;
    VariableStoreHeader->Format            = VARIABLE_STORE_FORMATTED;
    VariableStoreHeader->State             = VARIABLE_STORE_HEALTHY;

    // Install the combined super-header in the NorFlash
    Status = FvbWrite (&Instance->FvbProtocol, 0, 0, &HeadersLength, Headers);

    FreePool (Headers);
    return Status;
}

/**
  Check the integrity of firmware volume header.

  @param[in] FwVolHeader - A pointer to a firmware volume header

  @retval  EFI_SUCCESS   - The firmware volume is consistent
  @retval  EFI_NOT_FOUND - The firmware volume has been corrupted.

**/
EFI_STATUS
ValidateFvHeader (
    IN  FLASH_INSTANCE* Instance
)
{
    UINT16                      Checksum;
    EFI_FIRMWARE_VOLUME_HEADER* FwVolHeader;
    VARIABLE_STORE_HEADER*      VariableStoreHeader;
    UINTN                       VariableStoreLength;
    UINTN                       FvLength;

    FwVolHeader = (EFI_FIRMWARE_VOLUME_HEADER*)Instance->RegionBaseAddress;

    FvLength = PcdGet32(PcdFlashNvStorageVariableSize) + PcdGet32(PcdFlashNvStorageFtwWorkingSize) +
               PcdGet32(PcdFlashNvStorageFtwSpareSize);

    //
    // Verify the header revision, header signature, length
    // Length of FvBlock cannot be 2**64-1
    // HeaderLength cannot be an odd number
    //
    if (   (FwVolHeader->Revision  != EFI_FVH_REVISION)
           || (FwVolHeader->Signature != EFI_FVH_SIGNATURE)
           || (FwVolHeader->FvLength  != FvLength)
       )
    {
        DEBUG ((EFI_D_ERROR, "ValidateFvHeader: No Firmware Volume header present\n"));
        return EFI_NOT_FOUND;
    }

    // Check the Firmware Volume Guid
    if ( CompareGuid (&FwVolHeader->FileSystemGuid, &gEfiSystemNvDataFvGuid) == FALSE )
    {
        DEBUG ((EFI_D_ERROR, "ValidateFvHeader: Firmware Volume Guid non-compatible\n"));
        return EFI_NOT_FOUND;
    }

    // Verify the header checksum
    Checksum = CalculateSum16((UINT16*)FwVolHeader, FwVolHeader->HeaderLength);
    if (Checksum != 0)
    {
        DEBUG ((EFI_D_ERROR, "ValidateFvHeader: FV checksum is invalid (Checksum:0x%X)\n", Checksum));
        return EFI_NOT_FOUND;
    }

    VariableStoreHeader = (VARIABLE_STORE_HEADER*)((UINTN)FwVolHeader + (UINTN)FwVolHeader->HeaderLength);

    // Check the Variable Store Guid
    if ( CompareGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid) == FALSE )
    {
        DEBUG ((EFI_D_ERROR, "ValidateFvHeader: Variable Store Guid non-compatible\n"));
        return EFI_NOT_FOUND;
    }

    VariableStoreLength = PcdGet32 (PcdFlashNvStorageVariableSize) - FwVolHeader->HeaderLength;
    if (VariableStoreHeader->Size != VariableStoreLength)
    {
        DEBUG ((EFI_D_ERROR, "ValidateFvHeader: Variable Store Length does not match\n"));
        return EFI_NOT_FOUND;
    }

    return EFI_SUCCESS;
}

/**
 The FvbGetAttributes() function retrieves the attributes and
 current settings of the block.

 @param This         Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

 @param Attributes   Pointer to EFI_FVB_ATTRIBUTES_2 in which the attributes and
                     current settings are returned.
                     Type EFI_FVB_ATTRIBUTES_2 is defined in EFI_FIRMWARE_VOLUME_HEADER.

 @retval EFI_SUCCESS The firmware volume attributes were returned.

 **/
EFI_STATUS
EFIAPI
FvbGetAttributes(
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL*    This,
    OUT       EFI_FVB_ATTRIBUTES_2*                   Attributes
)
{
    EFI_FVB_ATTRIBUTES_2  FlashFvbAttributes;
    FLASH_INSTANCE*                 Instance;

    Instance = INSTANCE_FROM_FVB_THIS(This);

    FlashFvbAttributes = (EFI_FVB_ATTRIBUTES_2) (

                             EFI_FVB2_READ_ENABLED_CAP | // Reads may be enabled
                             EFI_FVB2_READ_STATUS      | // Reads are currently enabled
                             EFI_FVB2_STICKY_WRITE     | // A block erase is required to flip bits into EFI_FVB2_ERASE_POLARITY
                             EFI_FVB2_MEMORY_MAPPED    | // It is memory mapped
                             EFI_FVB2_ERASE_POLARITY     // After erasure all bits take this value (i.e. '1')

                         );

    // Check if it is write protected
    if (Instance->Media.ReadOnly != TRUE)
    {

        FlashFvbAttributes = FlashFvbAttributes         |
                             EFI_FVB2_WRITE_STATUS      | // Writes are currently enabled
                             EFI_FVB2_WRITE_ENABLED_CAP;  // Writes may be enabled
    }

    *Attributes = FlashFvbAttributes;

    return EFI_SUCCESS;
}

/**
 The FvbSetAttributes() function sets configurable firmware volume attributes
 and returns the new settings of the firmware volume.


 @param This                     Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

 @param Attributes               On input, Attributes is a pointer to EFI_FVB_ATTRIBUTES_2
                                 that contains the desired firmware volume settings.
                                 On successful return, it contains the new settings of
                                 the firmware volume.
                                 Type EFI_FVB_ATTRIBUTES_2 is defined in EFI_FIRMWARE_VOLUME_HEADER.

 @retval EFI_SUCCESS             The firmware volume attributes were returned.

 @retval EFI_INVALID_PARAMETER   The attributes requested are in conflict with the capabilities
                                 as declared in the firmware volume header.

 **/
EFI_STATUS
EFIAPI
FvbSetAttributes(
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL*  This,
    IN OUT    EFI_FVB_ATTRIBUTES_2*                 Attributes
)
{
    DEBUG ((EFI_D_ERROR, "FvbSetAttributes(0x%X) is not supported\n", *Attributes));
    return EFI_UNSUPPORTED;
}

/**
 The GetPhysicalAddress() function retrieves the base address of
 a memory-mapped firmware volume. This function should be called
 only for memory-mapped firmware volumes.

 @param This               Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

 @param Address            Pointer to a caller-allocated
                           EFI_PHYSICAL_ADDRESS that, on successful
                           return from GetPhysicalAddress(), contains the
                           base address of the firmware volume.

 @retval EFI_SUCCESS       The firmware volume base address was returned.

 @retval EFI_NOT_SUPPORTED The firmware volume is not memory mapped.

 **/
EFI_STATUS
EFIAPI
FvbGetPhysicalAddress (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL*  This,
    OUT       EFI_PHYSICAL_ADDRESS*                 Address
)
{

    if(NULL == Address)
    {
        return EFI_UNSUPPORTED;
    };

    *Address = mFlashNvStorageVariableBase;
    return EFI_SUCCESS;
}

/**
 The GetBlockSize() function retrieves the size of the requested
 block. It also returns the number of additional blocks with
 the identical size. The GetBlockSize() function is used to
 retrieve the block map (see EFI_FIRMWARE_VOLUME_HEADER).


 @param This                     Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

 @param Lba                      Indicates the block for which to return the size.

 @param BlockSize                Pointer to a caller-allocated UINTN in which
                                 the size of the block is returned.

 @param NumberOfBlocks           Pointer to a caller-allocated UINTN in
                                 which the number of consecutive blocks,
                                 starting with Lba, is returned. All
                                 blocks in this range have a size of
                                 BlockSize.


 @retval EFI_SUCCESS             The firmware volume base address was returned.

 @retval EFI_INVALID_PARAMETER   The requested LBA is out of range.

 **/
EFI_STATUS
EFIAPI
FvbGetBlockSize (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL*  This,
    IN        EFI_LBA                              Lba,
    OUT       UINTN*                                BlockSize,
    OUT       UINTN*                                NumberOfBlocks
)
{
    EFI_STATUS Status;
    FLASH_INSTANCE* Instance;

    Instance = INSTANCE_FROM_FVB_THIS(This);

    if (Lba > Instance->Media.LastBlock)
    {
        Status = EFI_INVALID_PARAMETER;
    }
    else
    {
        // This is easy because in this platform each NorFlash device has equal sized blocks.
        *BlockSize = (UINTN) Instance->Media.BlockSize;
        *NumberOfBlocks = (UINTN) (Instance->Media.LastBlock - Lba + 1);


        Status = EFI_SUCCESS;
    }

    return Status;
}

STATIC
EFI_STATUS
EFIAPI
FvbReset(
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN BOOLEAN                ExtendedVerification
)
{
  return EFI_SUCCESS;
}


/**
 Reads the specified number of bytes into a buffer from the specified block.

 The Read() function reads the requested number of bytes from the
 requested block and stores them in the provided buffer.
 Implementations should be mindful that the firmware volume
 might be in the ReadDisabled state. If it is in this state,
 the Read() function must return the status code
 EFI_ACCESS_DENIED without modifying the contents of the
 buffer. The Read() function must also prevent spanning block
 boundaries. If a read is requested that would span a block
 boundary, the read must read up to the boundary but not
 beyond. The output parameter NumBytes must be set to correctly
 indicate the number of bytes actually read. The caller must be
 aware that a read may be partially completed.

 @param This                 Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

 @param Lba                  The starting logical block index from which to read.

 @param Offset               Offset into the block at which to begin reading.

 @param NumBytes             Pointer to a UINTN.
                             At entry, *NumBytes contains the total size of the buffer.
                             At exit, *NumBytes contains the total number of bytes read.

 @param Buffer               Pointer to a caller-allocated buffer that will be used
                             to hold the data that is read.

 @retval EFI_SUCCESS         The firmware volume was read successfully,  and contents are
                             in Buffer.

 @retval EFI_BAD_BUFFER_SIZE Read attempted across an LBA boundary.
                             On output, NumBytes contains the total number of bytes
                             returned in Buffer.

 @retval EFI_ACCESS_DENIED   The firmware volume is in the ReadDisabled state.

 @retval EFI_DEVICE_ERROR    The block device is not functioning correctly and could not be read.

 **/
EFI_STATUS
EFIAPI
FvbRead (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL*   This,
    IN        EFI_LBA                               Lba,
    IN        UINTN                                 Offset,
    IN OUT    UINTN*                                 NumBytes,
    IN OUT    UINT8*                                 Buffer
)
{
    EFI_STATUS                    Status;
    UINTN                      BlockSize;
    FLASH_INSTANCE*             Instance;

    UINTN                   StartAddress;
    UINTN                    ReadAddress;

    Instance = INSTANCE_FROM_FVB_THIS(This);

    if (!Instance->Initialized && Instance->Initialize)
    {
        if (EfiAtRuntime ()) {
            DEBUG ((EFI_D_ERROR, "[%a]:[%dL] Initialize at runtime is not supported!\n", __func__, __LINE__));
            return EFI_UNSUPPORTED;
        }

        Instance->Initialize(Instance);
    }

    Status = EFI_SUCCESS;

    // Cache the block size to avoid de-referencing pointers all the time
    BlockSize = Instance->Media.BlockSize;

    // The read must not span block boundaries.
    // We need to check each variable individually because adding two large values together overflows.
    if ((Offset               >= BlockSize) ||
        (*NumBytes            >  BlockSize) ||
        ((Offset + *NumBytes) >  BlockSize))
    {
        DEBUG ((EFI_D_ERROR, "[%a]:[%dL] ERROR - EFI_BAD_BUFFER_SIZE: (Offset=0x%x + NumBytes=0x%x) > BlockSize=0x%x\n", __func__, __LINE__, Offset, *NumBytes, BlockSize ));
        return EFI_BAD_BUFFER_SIZE;
    }

    // We must have some bytes to read
    if (*NumBytes == 0)
    {
        return EFI_BAD_BUFFER_SIZE;
    }

    // Get the address to start reading from
    StartAddress = GET_BLOCK_ADDRESS (Instance->RegionBaseAddress,
                                      Lba,
                                      BlockSize
                                     );
    ReadAddress = StartAddress - Instance->DeviceBaseAddress + Offset;

    Status = mFlash->Read(mFlash, (UINT32)ReadAddress, Buffer, *NumBytes);
    if (EFI_SUCCESS != Status)
    {
        // Return one of the pre-approved error statuses
        Status = EFI_DEVICE_ERROR;
        return Status;
    }


    return Status;
}

/**
 Writes the specified number of bytes from the input buffer to the block.

 The Write() function writes the specified number of bytes from
 the provided buffer to the specified block and offset. If the
 firmware volume is sticky write, the caller must ensure that
 all the bits of the specified range to write are in the
 EFI_FVB_ERASE_POLARITY state before calling the Write()
 function, or else the result will be unpredictable. This
 unpredictability arises because, for a sticky-write firmware
 volume, a write may negate a bit in the EFI_FVB_ERASE_POLARITY
 state but cannot flip it back again.  Before calling the
 Write() function,  it is recommended for the caller to first call
 the EraseBlocks() function to erase the specified block to
 write. A block erase cycle will transition bits from the
 (NOT)EFI_FVB_ERASE_POLARITY state back to the
 EFI_FVB_ERASE_POLARITY state. Implementations should be
 mindful that the firmware volume might be in the WriteDisabled
 state. If it is in this state, the Write() function must
 return the status code EFI_ACCESS_DENIED without modifying the
 contents of the firmware volume. The Write() function must
 also prevent spanning block boundaries. If a write is
 requested that spans a block boundary, the write must store up
 to the boundary but not beyond. The output parameter NumBytes
 must be set to correctly indicate the number of bytes actually
 written. The caller must be aware that a write may be
 partially completed. All writes, partial or otherwise, must be
 fully flushed to the hardware before the Write() service
 returns.

 @param This                 Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL instance.

 @param Lba                  The starting logical block index to write to.

 @param Offset               Offset into the block at which to begin writing.

 @param NumBytes             The pointer to a UINTN.
                             At entry, *NumBytes contains the total size of the buffer.
                             At exit, *NumBytes contains the total number of bytes actually written.

 @param Buffer               The pointer to a caller-allocated buffer that contains the source for the write.

 @retval EFI_SUCCESS         The firmware volume was written successfully.

 @retval EFI_BAD_BUFFER_SIZE The write was attempted across an LBA boundary.
                             On output, NumBytes contains the total number of bytes
                             actually written.

 @retval EFI_ACCESS_DENIED   The firmware volume is in the WriteDisabled state.

 @retval EFI_DEVICE_ERROR    The block device is malfunctioning and could not be written.


 **/
EFI_STATUS
EFIAPI
FvbWrite (
    IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL*   This,
    IN        EFI_LBA                               Lba,
    IN        UINTN                                 Offset,
    IN OUT    UINTN*                                 NumBytes,
    IN        UINT8*                                 Buffer
)
{
    EFI_STATUS                     Status;
    UINTN                       BlockSize;
    FLASH_INSTANCE*              Instance;
    UINTN                    BlockAddress;
    UINTN                    WriteAddress;

    Instance = INSTANCE_FROM_FVB_THIS(This);
    if (NULL == Instance)
    {
        return EFI_INVALID_PARAMETER;

    }

    if (!Instance->Initialized && Instance->Initialize)
    {
        if (EfiAtRuntime ()) {
            DEBUG ((EFI_D_ERROR, "[%a]:[%dL] Initialize at runtime is not supported!\n", __func__, __LINE__));
            return EFI_UNSUPPORTED;
        }

        Instance->Initialize(Instance);
    }

    Status = EFI_SUCCESS;

    // Detect WriteDisabled state
    if (Instance->Media.ReadOnly == TRUE)
    {
        DEBUG ((EFI_D_ERROR, "FvbWrite: ERROR - Can not write: Device is in WriteDisabled state.\n"));
        // It is in WriteDisabled state, return an error right away
        return EFI_ACCESS_DENIED;
    }

    // Cache the block size to avoid de-referencing pointers all the time
    BlockSize = Instance->Media.BlockSize;

    // The write must not span block boundaries.
    // We need to check each variable individually because adding two large values together overflows.
    if ( ( Offset               >= BlockSize ) ||
         ( *NumBytes            >  BlockSize ) ||
         ( (Offset + *NumBytes) >  BlockSize )    )
    {
        DEBUG ((EFI_D_ERROR, "FvbWrite: ERROR - EFI_BAD_BUFFER_SIZE: (Offset=0x%x + NumBytes=0x%x) > BlockSize=0x%x\n", Offset, *NumBytes, BlockSize ));
        return EFI_BAD_BUFFER_SIZE;
    }

    // We must have some bytes to write
    if (*NumBytes == 0)
    {
        DEBUG ((EFI_D_ERROR, "FvbWrite: ERROR - EFI_BAD_BUFFER_SIZE: (Offset=0x%x + NumBytes=0x%x) > BlockSize=0x%x\n", Offset, *NumBytes, BlockSize ));
        return EFI_BAD_BUFFER_SIZE;
    }

    BlockAddress = GET_BLOCK_ADDRESS (Instance->RegionBaseAddress, Lba, BlockSize);
    WriteAddress = BlockAddress - Instance->DeviceBaseAddress + Offset;

    Status = mFlash->Write(mFlash, (UINT32)WriteAddress, (UINT8*)Buffer, *NumBytes);
    if (EFI_SUCCESS != Status)
    {
        DEBUG((EFI_D_ERROR, "%s - %d Status=%r\n", __FILE__, __LINE__, Status));
        return Status;
    }

    return Status;

}

/**
 Erases and initialises a firmware volume block.

 The EraseBlocks() function erases one or more blocks as denoted
 by the variable argument list. The entire parameter list of
 blocks must be verified before erasing any blocks. If a block is
 requested that does not exist within the associated firmware
 volume (it has a larger index than the last block of the
 firmware volume), the EraseBlocks() function must return the
 status code EFI_INVALID_PARAMETER without modifying the contents
 of the firmware volume. Implementations should be mindful that
 the firmware volume might be in the WriteDisabled state. If it
 is in this state, the EraseBlocks() function must return the
 status code EFI_ACCESS_DENIED without modifying the contents of
 the firmware volume. All calls to EraseBlocks() must be fully
 flushed to the hardware before the EraseBlocks() service
 returns.

 @param This                     Indicates the EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL
 instance.

 @param ...                      The variable argument list is a list of tuples.
                                 Each tuple describes a range of LBAs to erase
                                 and consists of the following:
                                 - An EFI_LBA that indicates the starting LBA
                                 - A UINTN that indicates the number of blocks to erase.

                                 The list is terminated with an EFI_LBA_LIST_TERMINATOR.
                                 For example, the following indicates that two ranges of blocks
                                 (5-7 and 10-11) are to be erased:
                                 EraseBlocks (This, 5, 3, 10, 2, EFI_LBA_LIST_TERMINATOR);

 @retval EFI_SUCCESS             The erase request successfully completed.

 @retval EFI_ACCESS_DENIED       The firmware volume is in the WriteDisabled state.

 @retval EFI_DEVICE_ERROR        The block device is not functioning correctly and could not be written.
                                 The firmware device may have been partially erased.

 @retval EFI_INVALID_PARAMETER   One or more of the LBAs listed in the variable argument list do
                                 not exist in the firmware volume.

 **/
EFI_STATUS
EFIAPI
FvbEraseBlocks (
    IN CONST EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL* This,
    ...
)
{
    EFI_STATUS  Status;
    VA_LIST     Args;
    UINTN       BlockAddress; // Physical address of Lba to erase
    EFI_LBA     StartingLba; // Lba from which we start erasing
    UINTN       NumOfLba; // Number of Lba blocks to erase
    FLASH_INSTANCE* Instance;

    Instance = INSTANCE_FROM_FVB_THIS(This);

    Status = EFI_SUCCESS;

    // Detect WriteDisabled state
    if (Instance->Media.ReadOnly == TRUE)
    {
        // Firmware volume is in WriteDisabled state
        return EFI_ACCESS_DENIED;
    }

    // Before erasing, check the entire list of parameters to ensure all specified blocks are valid
    VA_START (Args, This);
    do
    {
        // Get the Lba from which we start erasing
        StartingLba = VA_ARG (Args, EFI_LBA);

        // Have we reached the end of the list?
        if (StartingLba == EFI_LBA_LIST_TERMINATOR)
        {
            //Exit the while loop
            break;
        }

        // How many Lba blocks are we requested to erase?
        NumOfLba = VA_ARG (Args, UINT32);

        // All blocks must be within range
        if ((NumOfLba == 0) || ((Instance->StartLba + StartingLba + NumOfLba - 1) > Instance->Media.LastBlock))
        {
            VA_END (Args);
            Status = EFI_INVALID_PARAMETER;
            goto EXIT;
        }
    }
    while (TRUE);
    VA_END (Args);

    //
    // To get here, all must be ok, so start erasing
    //
    VA_START (Args, This);
    do
    {
        // Get the Lba from which we start erasing
        StartingLba = VA_ARG (Args, EFI_LBA);

        // Have we reached the end of the list?
        if (StartingLba == EFI_LBA_LIST_TERMINATOR)
        {
            // Exit the while loop
            break;
        }

        // How many Lba blocks are we requested to erase?
        NumOfLba = VA_ARG (Args, UINT32);

        // Go through each one and erase it
        while (NumOfLba > 0)
        {

            // Get the physical address of Lba to erase
            BlockAddress = GET_BLOCK_ADDRESS (
                               Instance->RegionBaseAddress,
                               Instance->StartLba + StartingLba,
                               Instance->Media.BlockSize
                           );

            // Erase it

            Status = FlashUnlockAndEraseSingleBlock (Instance, BlockAddress);
            if (EFI_ERROR(Status))
            {
                VA_END (Args);
                Status = EFI_DEVICE_ERROR;
                goto EXIT;
            }

            // Move to the next Lba
            StartingLba++;
            NumOfLba--;
        }
    }
    while (TRUE);
    VA_END (Args);

EXIT:
    return Status;
}

EFI_STATUS
EFIAPI
FvbInitialize (
    IN FLASH_INSTANCE* Instance
)
{
    EFI_STATUS  Status;
    UINT32      FvbNumLba;

    Instance->Initialized = TRUE;
    mFlashNvStorageVariableBase = FixedPcdGet64 (PcdFlashNvStorageVariableBase64);

    // Set the index of the first LBA for the FVB
    Instance->StartLba = (PcdGet64 (PcdFlashNvStorageVariableBase64) - Instance->RegionBaseAddress) / Instance->Media.BlockSize;

    // Determine if there is a valid header at the beginning of the Flash
    Status = ValidateFvHeader (Instance);
    if (EFI_ERROR(Status))
    {
        // There is no valid header, so time to install one.
        // Erase all the Flash that is reserved for variable storage
        FvbNumLba = (PcdGet32(PcdFlashNvStorageVariableSize) + PcdGet32(PcdFlashNvStorageFtwWorkingSize) + (UINT32)PcdGet32(PcdFlashNvStorageFtwSpareSize)) / Instance->Media.BlockSize;
        Status = FvbEraseBlocks (&Instance->FvbProtocol, (EFI_LBA)0, FvbNumLba, EFI_LBA_LIST_TERMINATOR);
        if (EFI_ERROR(Status))
        {
            return Status;
        }

        // Install all appropriate headers
        Status = InitializeFvAndVariableStoreHeaders (Instance);
        if (EFI_ERROR(Status))
        {
            return Status;
        }
    }
    return Status;
}


EFI_STATUS
FlashPlatformGetDevices (
    OUT FLASH_DESCRIPTION**   FlashDevices,
    OUT UINT32*                  Count
)
{
    if ((FlashDevices == NULL) || (Count == NULL))
    {
        return EFI_INVALID_PARAMETER;
    }

    *FlashDevices = mFlashDevices;
    *Count = FLASH_DEVICE_COUNT;

    return EFI_SUCCESS;
}


EFI_STATUS
FlashCreateInstance (
    IN UINTN                  FlashDeviceBase,
    IN UINTN                  FlashRegionBase,
    IN UINTN                  FlashSize,
    IN UINT32                 MediaId,
    IN UINT32                 BlockSize,
    IN BOOLEAN                SupportFvb,
    IN CONST GUID*             FlashGuid,
    OUT FLASH_INSTANCE**  FlashInstance
)
{
    EFI_STATUS Status;
    FLASH_INSTANCE* Instance;

    if (FlashInstance == NULL)
    {
        return EFI_INVALID_PARAMETER;
    }

    Instance = AllocateRuntimeCopyPool (sizeof(FLASH_INSTANCE), &mFlashInstanceTemplate);
    if (Instance == NULL)
    {
        return EFI_INVALID_PARAMETER;
    }

    Instance->DeviceBaseAddress = FlashDeviceBase;
    Instance->RegionBaseAddress = FlashRegionBase;
    Instance->Size = FlashSize;

    Instance->BlockIoProtocol.Media = &Instance->Media;
    Instance->BlockIoProtocol.Reset = FvbReset;
    Instance->Media.MediaId = MediaId;
    Instance->Media.BlockSize = BlockSize;
    Instance->Media.LastBlock = (FlashSize / BlockSize) - 1;

    CopyGuid (&Instance->DevicePath.Vendor.Guid, FlashGuid);

    if (SupportFvb)
    {
        Instance->SupportFvb = TRUE;
        Instance->Initialize = FvbInitialize;

        Status = gBS->InstallMultipleProtocolInterfaces (
                     &Instance->Handle,
                     &gEfiDevicePathProtocolGuid, &Instance->DevicePath,
                     &gEfiBlockIoProtocolGuid,  &Instance->BlockIoProtocol,
                     &gEfiFirmwareVolumeBlockProtocolGuid, &Instance->FvbProtocol,
                     NULL
                 );

        if (EFI_ERROR(Status))
        {
            FreePool(Instance);
            return Status;
        }
    }
    else
    {
        Instance->Initialized = TRUE;

        Status = gBS->InstallMultipleProtocolInterfaces (
                     &Instance->Handle,
                     &gEfiDevicePathProtocolGuid, &Instance->DevicePath,
                     &gEfiBlockIoProtocolGuid,  &Instance->BlockIoProtocol,
                     NULL
                 );
        if (EFI_ERROR(Status))
        {
            FreePool(Instance);
            return Status;
        }
    }

    *FlashInstance = Instance;
    return Status;
}

EFI_STATUS
FlashUnlockSingleBlockIfNecessary (
    IN FLASH_INSTANCE*           Instance,
    IN UINTN                  BlockAddress
)
{
    return EFI_SUCCESS;
}


EFI_STATUS
FlashEraseSingleBlock (
    IN FLASH_INSTANCE*           Instance,
    IN UINTN                  BlockAddress
)
{
    EFI_STATUS            Status;
    UINTN                 EraseAddress;

    Status = EFI_SUCCESS;
    EraseAddress = BlockAddress - Instance->DeviceBaseAddress;

    Status = mFlash->Erase(mFlash, (UINT32)EraseAddress, Instance->Media.BlockSize);
    if (EFI_SUCCESS != Status)
    {
        DEBUG((EFI_D_ERROR, "%s - %d Status=%r\n", __FILE__, __LINE__, Status));
        return Status;
    }

    return EFI_SUCCESS;
}

/**
 * The following function presumes that the block has already been unlocked.
 **/
EFI_STATUS
FlashUnlockAndEraseSingleBlock (
    IN FLASH_INSTANCE*     Instance,
    IN UINTN                  BlockAddress
)
{
    EFI_STATUS      Status;
    UINTN           Index;

    Index = 0;
    // The block erase might fail a first time (SW bug ?). Retry it ...
    do
    {
        // Unlock the block if we have to
        Status = FlashUnlockSingleBlockIfNecessary (Instance, BlockAddress);
        if (!EFI_ERROR(Status))
        {
            Status = FlashEraseSingleBlock (Instance, BlockAddress);
        }
        Index++;
    }
    while ((Index < FLASH_ERASE_RETRY) && (Status == EFI_WRITE_PROTECTED));

    if (Index == FLASH_ERASE_RETRY)
    {
        DEBUG((EFI_D_ERROR, "EraseSingleBlock(BlockAddress=0x%08x: Block Locked Error (try to erase %d times)\n", BlockAddress, Index));
    }

    return Status;
}

EFI_STATUS
FlashWriteBlocks (
    IN FLASH_INSTANCE*        Instance,
    IN EFI_LBA                Lba,
    IN UINTN                  BufferSizeInBytes,
    IN VOID*                   Buffer
)
{
    EFI_STATUS      Status = EFI_SUCCESS;
    UINTN                   BlockAddress;
    UINT32                     NumBlocks;
    UINTN                   WriteAddress;

    // The buffer must be valid
    if (Buffer == NULL)
    {
        return EFI_INVALID_PARAMETER;
    }

    if (Instance->Media.ReadOnly == TRUE)
    {
        return EFI_WRITE_PROTECTED;
    }

    // We must have some bytes to read
    if (BufferSizeInBytes == 0)
    {
        return EFI_BAD_BUFFER_SIZE;
    }

    // The size of the buffer must be a multiple of the block size
    if ((BufferSizeInBytes % Instance->Media.BlockSize) != 0)
    {
        return EFI_BAD_BUFFER_SIZE;
    }

    // All blocks must be within the device
    NumBlocks = ((UINT32)BufferSizeInBytes) / Instance->Media.BlockSize ;
    if ((Lba + NumBlocks) > (Instance->Media.LastBlock + 1))
    {
        DEBUG((EFI_D_ERROR, "[%a]:[%dL]ERROR - Write will exceed last block.\n", __func__, __LINE__ ));
        return EFI_INVALID_PARAMETER;
    }

    BlockAddress = GET_BLOCK_ADDRESS (Instance->RegionBaseAddress, Lba, Instance->Media.BlockSize);

    WriteAddress = BlockAddress - Instance->DeviceBaseAddress;

    Status = mFlash->Write(mFlash, (UINT32)WriteAddress, (UINT8*)Buffer, BufferSizeInBytes);
    if (EFI_SUCCESS != Status)
    {
        DEBUG((EFI_D_ERROR, "%s - %d Status=%r\n", __FILE__, __LINE__, Status));
        return Status;
    }

    return Status;
}

EFI_STATUS
FlashReadBlocks (
    IN FLASH_INSTANCE*   Instance,
    IN EFI_LBA              Lba,
    IN UINTN                BufferSizeInBytes,
    OUT VOID*                Buffer
)
{
    UINT32                     NumBlocks;
    UINTN                   StartAddress;
    UINTN                    ReadAddress;
    EFI_STATUS                    Status;

    // The buffer must be valid
    if (Buffer == NULL)
    {
        return EFI_INVALID_PARAMETER;
    }

    // We must have some bytes to read
    if (BufferSizeInBytes == 0)
    {
        return EFI_BAD_BUFFER_SIZE;
    }

    // The size of the buffer must be a multiple of the block size
    if ((BufferSizeInBytes % Instance->Media.BlockSize) != 0)
    {
        return EFI_BAD_BUFFER_SIZE;
    }

    // All blocks must be within the device
    NumBlocks = ((UINT32)BufferSizeInBytes) / Instance->Media.BlockSize ;
    if ((Lba + NumBlocks) > (Instance->Media.LastBlock + 1))
    {
        DEBUG((EFI_D_ERROR, "FlashReadBlocks: ERROR - Read will exceed last block\n"));
        return EFI_INVALID_PARAMETER;
    }

    // Get the address to start reading from
    StartAddress = GET_BLOCK_ADDRESS (Instance->RegionBaseAddress,
                                      Lba,
                                      Instance->Media.BlockSize
                                     );


    ReadAddress = StartAddress - Instance->DeviceBaseAddress;

    Status = mFlash->Read(mFlash, (UINT32)ReadAddress, Buffer, BufferSizeInBytes);
    if (EFI_SUCCESS != Status)
    {
        DEBUG((EFI_D_ERROR, "%s - %d Status=%r\n", __FILE__, __LINE__, Status));
        return Status;
    }

    return EFI_SUCCESS;
}

VOID
EFIAPI
FlashFvbVirtualNotifyEvent (
  IN EFI_EVENT        Event,
  IN VOID             *Context
  )
{
  EfiConvertPointer (0x0, (VOID**)&mFlash);
  EfiConvertPointer (0x0, (VOID**)&mFlashNvStorageVariableBase);
  return;
}

EFI_STATUS
EFIAPI
FlashFvbInitialize (
    IN EFI_HANDLE         ImageHandle,
    IN EFI_SYSTEM_TABLE*   SystemTable
)
{
    EFI_STATUS              Status;
    UINT32                  Index;
    FLASH_DESCRIPTION*      FlashDevices;
    UINT32                  FlashDeviceCount;
    BOOLEAN                 ContainVariableStorage;


    Status = FlashPlatformGetDevices (&FlashDevices, &FlashDeviceCount);
    if (EFI_ERROR(Status))
    {
        DEBUG((EFI_D_ERROR, "[%a]:[%dL] Fail to get Flash devices\n", __func__, __LINE__));
        return Status;
    }

    mFlashInstances = AllocatePool ((UINT32)(sizeof(FLASH_INSTANCE*) * FlashDeviceCount));

    Status = gBS->LocateProtocol (&gHisiSpiFlashProtocolGuid, NULL, (VOID*) &mFlash);
    if (EFI_ERROR(Status))
    {
        DEBUG((EFI_D_ERROR, "[%a]:[%dL] Status=%r\n", __func__, __LINE__, Status));
        return Status;
    }

    for (Index = 0; Index < FlashDeviceCount; Index++)
    {
        // Check if this Flash device contain the variable storage region
        ContainVariableStorage =
             (FlashDevices[Index].RegionBaseAddress <= PcdGet64 (PcdFlashNvStorageVariableBase64)) &&
             ((PcdGet64 (PcdFlashNvStorageVariableBase64) + PcdGet32 (PcdFlashNvStorageVariableSize)) <= FlashDevices[Index].RegionBaseAddress + FlashDevices[Index].Size);

        Status = FlashCreateInstance (
                     FlashDevices[Index].DeviceBaseAddress,
                     FlashDevices[Index].RegionBaseAddress,
                     FlashDevices[Index].Size,
                     Index,
                     FlashDevices[Index].BlockSize,
                     ContainVariableStorage,
                     &FlashDevices[Index].Guid,
                     &mFlashInstances[Index]
                 );
        if (EFI_ERROR(Status))
        {
            DEBUG((EFI_D_ERROR, "[%a]:[%dL] Fail to create instance for Flash[%d]\n", __func__, __LINE__, Index));
        }
    }
    //
    // Register for the virtual address change event
    //
    Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  FlashFvbVirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mFlashFvbVirtualAddrChangeEvent
                  );
    ASSERT_EFI_ERROR (Status);

    return Status;
}
