/******************************************************************************
  MEMORY Driver Interface Implementation

  Company:
    Microchip Technology Inc.

  File Name:
    drv_memory.c

  Summary:
    MEMORY Driver Interface Definition

  Description:
    The MEMORY Driver provides a interface to access the MEMORY on the PIC32
    microcontroller. This file implements the MEMORY Driver interface. This file
    should be included in the project if MEMORY driver functionality is needed.
*******************************************************************************/

//DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/
//DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Include Files
// *****************************************************************************
// *****************************************************************************

#include "driver/memory/src/drv_memory_local.h"
#include "system/time/sys_time.h"
#include "system/debug/sys_debug.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global objects
// *****************************************************************************
// *****************************************************************************

/*************************************************
 * Hardware instance objects
 *************************************************/

static DRV_MEMORY_OBJECT gDrvMemoryObj[DRV_MEMORY_INSTANCES_NUMBER];


/************************************************
 * This token is incremented for every request added to the queue and is used
 * to generate a different buffer handle for every request.
 ***********************************************/

static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleRead
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
);

static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleWrite
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
);

static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleErase
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
);
static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleEraseWrite
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
);

static const DRV_MEMORY_TransferOperation gMemoryXferFuncPtr[4] =
{
    DRV_MEMORY_HandleRead,
    DRV_MEMORY_HandleWrite,
    DRV_MEMORY_HandleErase,
    DRV_MEMORY_HandleEraseWrite,
};

// *****************************************************************************
// *****************************************************************************
// Section: MEMORY Driver Local Functions
// *****************************************************************************
// *****************************************************************************

static void DRV_MEMORY_EventHandler( MEMORY_DEVICE_TRANSFER_STATUS status, uintptr_t context )
{
    DRV_MEMORY_OBJECT *dObj = (DRV_MEMORY_OBJECT *)context;
    dObj->isTransferDone = true;
    (void) OSAL_SEM_PostISR(&dObj->transferDone);
}

static void DRV_MEMORY_TimerHandler( uintptr_t context )
{
    DRV_MEMORY_OBJECT *dObj = (DRV_MEMORY_OBJECT *)context;
    (void) OSAL_SEM_PostISR(&dObj->transferDone);
}

static inline uint16_t DRV_MEMORY_UPDATE_TOKEN(uint16_t token)
{
    token++;
    if (token >= DRV_MEMORY_TOKEN_MAX)
    {
        token = 1;
    }

    return token;
}


/* This function populates buffer object with the transfer
 * parameters. It also generates a new command handle for the request.
 */
static void DRV_MEMORY_AllocateBufferObject
(
    DRV_MEMORY_CLIENT_OBJECT *clientObj,
    DRV_MEMORY_COMMAND_HANDLE *handle,
    void *buffer,
    uint32_t blockStart,
    uint32_t nBlocks,
    DRV_MEMORY_OPERATION_TYPE opType
)
{
    DRV_MEMORY_OBJECT *dObj = &gDrvMemoryObj[clientObj->drvIndex];
    DRV_MEMORY_BUFFER_OBJECT *bufferObj = NULL;

    bufferObj = &dObj->currentBufObj;

    bufferObj->commandHandle = DRV_MEMORY_MAKE_HANDLE((uint32_t)dObj->bufferToken, (uint32_t)clientObj->drvIndex, 0U);
    bufferObj->hClient       = clientObj;
    bufferObj->buffer        = buffer;
    bufferObj->blockStart    = blockStart;
    bufferObj->nBlocks       = nBlocks;
    bufferObj->opType        = opType;
    bufferObj->status        = DRV_MEMORY_COMMAND_QUEUED;

    /* Update the token number. */
    dObj->bufferToken = DRV_MEMORY_UPDATE_TOKEN(dObj->bufferToken);

    if (handle != NULL)
    {
        *handle = bufferObj->commandHandle;
    }
}

/* This function validates the driver handle and returns the client object
 * pointer associated with the driver handle if the handle is valid. If the
 * driver handle is not valid or if the driver is in a not ready state then
 * NULL is returned. */
static DRV_MEMORY_CLIENT_OBJECT * DRV_MEMORY_DriverHandleValidate
(
    DRV_HANDLE handle
)
{
    uint8_t instance = 0;
    uint8_t clientIndex = 0;
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;

    /* Validate the handle */
    if ((handle != DRV_HANDLE_INVALID) && (handle != 0U))
    {
        instance = (uint8_t)((handle & DRV_MEMORY_INSTANCE_INDEX_MASK) >> 8);
        clientIndex = (uint8_t)(handle & DRV_MEMORY_INDEX_MASK);

        if (instance >= DRV_MEMORY_INSTANCES_NUMBER)
        {
            return (NULL);
        }

        if (clientIndex >= gDrvMemoryObj[instance].nClientsMax)
        {
            return (NULL);
        }

        /* See if the client has been opened */
        clientObj = &((DRV_MEMORY_CLIENT_OBJECT *)gDrvMemoryObj[instance].clientObjPool)[clientIndex];

        if ((clientObj->clientHandle != handle) || (clientObj->inUse == false))
        {
            return (NULL);
        }

        /* Check if the driver is ready for operation */
        dObj = &gDrvMemoryObj[clientObj->drvIndex];

        if (dObj->status != SYS_STATUS_READY)
        {
            return (NULL);
        }
    }

    return (clientObj);
}

/* This function updates the driver object's geometry information for the memory
 * device. */
static bool DRV_MEMORY_UpdateGeometry( DRV_MEMORY_OBJECT *dObj )
{
    MEMORY_DEVICE_GEOMETRY  memoryDeviceGeometry = { 0 };

    if (dObj->memoryDevice->GeometryGet(dObj->memDevHandle, &memoryDeviceGeometry) == false)
    {
        return false;
    }

    /* Read block size and number of blocks */
    dObj->mediaGeometryTable[SYS_MEDIA_GEOMETRY_TABLE_READ_ENTRY].blockSize = memoryDeviceGeometry.read_blockSize;
    dObj->mediaGeometryTable[SYS_MEDIA_GEOMETRY_TABLE_READ_ENTRY].numBlocks = memoryDeviceGeometry.read_numBlocks;

    /* Write block size and number of blocks */
    dObj->mediaGeometryTable[SYS_MEDIA_GEOMETRY_TABLE_WRITE_ENTRY].blockSize = memoryDeviceGeometry.write_blockSize;
    dObj->mediaGeometryTable[SYS_MEDIA_GEOMETRY_TABLE_WRITE_ENTRY].numBlocks = memoryDeviceGeometry.write_numBlocks;
    dObj->writeBlockSize = memoryDeviceGeometry.write_blockSize;

    /* Erase block size and number of blocks */
    dObj->mediaGeometryTable[SYS_MEDIA_GEOMETRY_TABLE_ERASE_ENTRY].blockSize = memoryDeviceGeometry.erase_blockSize;
    dObj->mediaGeometryTable[SYS_MEDIA_GEOMETRY_TABLE_ERASE_ENTRY].numBlocks = memoryDeviceGeometry.erase_numBlocks;
    dObj->eraseBlockSize = memoryDeviceGeometry.erase_blockSize;

    /* Update the Media Geometry Main Structure */
    dObj->mediaGeometryObj.mediaProperty = (SYS_MEDIA_PROPERTY)((uint32_t)SYS_MEDIA_READ_IS_BLOCKING | (uint32_t)SYS_MEDIA_WRITE_IS_BLOCKING);

    /* Number of read, write and erase entries in the table */
    dObj->mediaGeometryObj.numReadRegions = memoryDeviceGeometry.numReadRegions;
    dObj->mediaGeometryObj.numWriteRegions = memoryDeviceGeometry.numWriteRegions;
    dObj->mediaGeometryObj.numEraseRegions = memoryDeviceGeometry.numEraseRegions;
    dObj->mediaGeometryObj.geometryTable = (SYS_MEDIA_REGION_GEOMETRY *)&dObj->mediaGeometryTable;

    dObj->blockStartAddress = memoryDeviceGeometry.blockStartAddress;

    return true;
}
/* MISRA C-2012 Rule 16.1, 16.3, 16.5 and 16.6 deviated below.
   Deviation record ID -  H3_MISRAC_2012_R_11_3_DR_1 & H3_MISRAC_2012_R_11_8_DR_1*/
static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleRead
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
)
{
    MEMORY_DEVICE_TRANSFER_STATUS transferStatus;
    uint32_t address = 0;

    switch (dObj->readState)
    {
        case DRV_MEMORY_READ_INIT:
        default:
        {
            address = (blockStart * dObj->mediaGeometryTable[0].blockSize) + dObj->blockStartAddress;
            dObj->readState = DRV_MEMORY_READ_MEM;
            /* Fall through */
        }

        case DRV_MEMORY_READ_MEM:
        {
            if (dObj->memoryDevice->Read(dObj->memDevHandle, (void *)data, nBlocks, address) == true)
            {
                dObj->readState = DRV_MEMORY_READ_MEM_STATUS;
                /* Fall through For immediate check */
            }
            else
            {
                /* Break in case of failure */
                transferStatus = MEMORY_DEVICE_TRANSFER_ERROR_UNKNOWN;
                break;
            }
        }

        case DRV_MEMORY_READ_MEM_STATUS:
        {
            transferStatus = (MEMORY_DEVICE_TRANSFER_STATUS)dObj->memoryDevice->TransferStatusGet(dObj->memDevHandle);
            break;
        }
    }

    return transferStatus;
}

static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleWrite
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
)
{
    MEMORY_DEVICE_TRANSFER_STATUS transferStatus;

    switch (dObj->writeState)
    {
        case DRV_MEMORY_WRITE_INIT:
        default:
        {
            dObj->blockAddress = ((blockStart * dObj->writeBlockSize) + dObj->blockStartAddress);
            dObj->nBlocks = nBlocks;
            dObj->writePtr = data;

            dObj->writeState = DRV_MEMORY_WRITE_MEM;
            /* Fall through */
        }

        case DRV_MEMORY_WRITE_MEM:
        {
            dObj->isTransferDone = false;
            if (dObj->memoryDevice->PageWrite(dObj->memDevHandle, (void *)dObj->writePtr, dObj->blockAddress) == true)
            {
                dObj->writeState = DRV_MEMORY_WRITE_MEM_STATUS;
                /* Fall through For immediate check */
            }
            else
            {
                /* Break in case of failure */
                transferStatus = MEMORY_DEVICE_TRANSFER_ERROR_UNKNOWN;
                break;
            }
        }

        case DRV_MEMORY_WRITE_MEM_STATUS:
        {
            transferStatus = (MEMORY_DEVICE_TRANSFER_STATUS)dObj->memoryDevice->TransferStatusGet(dObj->memDevHandle);

            if (transferStatus == MEMORY_DEVICE_TRANSFER_COMPLETED)
            {
                dObj->nBlocks--;

                if (dObj->nBlocks != 0U)
                {
                    /* There is still data to be programmed. */
                    dObj->blockAddress += dObj->writeBlockSize;
                    dObj->writePtr += dObj->writeBlockSize;

                    dObj->writeState = DRV_MEMORY_WRITE_MEM;
                    transferStatus = MEMORY_DEVICE_TRANSFER_BUSY;
                }
            }

            break;
        }
    }

    return transferStatus;
}

static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleErase
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
)
{
    MEMORY_DEVICE_TRANSFER_STATUS transferStatus;

    switch (dObj->eraseState)
    {
        case DRV_MEMORY_ERASE_INIT:
        default:
        {
            dObj->blockAddress = ((blockStart * dObj->eraseBlockSize) + dObj->blockStartAddress);
            dObj->nBlocks = nBlocks;
            dObj->eraseState = DRV_MEMORY_ERASE_CMD;
            /* Fall through */
        }

        case DRV_MEMORY_ERASE_CMD:
        {
            dObj->isTransferDone = false;
            if (dObj->memoryDevice->SectorErase(dObj->memDevHandle, dObj->blockAddress) == true)
            {
                dObj->eraseState = DRV_MEMORY_ERASE_CMD_STATUS;
                /* Fall through For immediate check */
            }
            else
            {
                /* Break in case of failure */
                transferStatus = MEMORY_DEVICE_TRANSFER_ERROR_UNKNOWN;
                break;
            }
        }

        case DRV_MEMORY_ERASE_CMD_STATUS:
        {
            transferStatus = (MEMORY_DEVICE_TRANSFER_STATUS)dObj->memoryDevice->TransferStatusGet(dObj->memDevHandle);

            if (transferStatus == MEMORY_DEVICE_TRANSFER_COMPLETED)
            {
                dObj->nBlocks--;

                if (dObj->nBlocks != 0U)
                {
                    /* There is still data to be programmed. */
                    dObj->blockAddress += dObj->eraseBlockSize;

                    dObj->eraseState = DRV_MEMORY_ERASE_CMD;
                    transferStatus = MEMORY_DEVICE_TRANSFER_BUSY;
                }
            }

            break;
        }
    }

    return transferStatus;
}

static MEMORY_DEVICE_TRANSFER_STATUS DRV_MEMORY_HandleEraseWrite
(
    DRV_MEMORY_OBJECT *dObj,
    uint8_t *data,
    uint32_t blockStart,
    uint32_t nBlocks
)
{
    DRV_MEMORY_BUFFER_OBJECT *bufferObj = &dObj->currentBufObj;
    uint8_t pagesPerSector = (uint8_t)(dObj->eraseBlockSize / dObj->writeBlockSize);
    uint32_t readBlockStart = 0;

    MEMORY_DEVICE_TRANSFER_STATUS transferStatus;

    switch (dObj->ewState)
    {
        case DRV_MEMORY_EW_INIT:
        default:
        {
            dObj->readState  = DRV_MEMORY_READ_INIT;
            dObj->eraseState = DRV_MEMORY_ERASE_INIT;
            dObj->writeState = DRV_MEMORY_WRITE_INIT;

            /* Find the sector for the starting page */
            dObj->sectorNumber = bufferObj->blockStart / pagesPerSector;

            /* Find the number of sectors to be updated in this block. */
            dObj->blockOffsetInSector = (bufferObj->blockStart % pagesPerSector);
            dObj->nBlocksToWrite = (pagesPerSector - dObj->blockOffsetInSector);

            if (bufferObj->nBlocks < dObj->nBlocksToWrite)
            {
                dObj->nBlocksToWrite = bufferObj->nBlocks;
            }

            if (dObj->nBlocksToWrite != pagesPerSector)
            {
                dObj->writePtr = dObj->ewBuffer;
                dObj->ewState = DRV_MEMORY_EW_READ_SECTOR;
            }
            else
            {
                dObj->writePtr = bufferObj->buffer;
                dObj->ewState = DRV_MEMORY_EW_ERASE_SECTOR;

                transferStatus = MEMORY_DEVICE_TRANSFER_BUSY;

                break;
            }

            /* Fall through for read operation. */
        }

        case DRV_MEMORY_EW_READ_SECTOR:
        {
            readBlockStart = (dObj->sectorNumber * dObj->eraseBlockSize);

            transferStatus = DRV_MEMORY_HandleRead (dObj, dObj->ewBuffer, readBlockStart, dObj->eraseBlockSize);

            if (transferStatus == MEMORY_DEVICE_TRANSFER_COMPLETED)
            {
                /* Find the offset from which the data is to be overlaid. */
                dObj->blockOffsetInSector *= dObj->writeBlockSize;

                (void)memcpy((void *)&dObj->ewBuffer[dObj->blockOffsetInSector], (const void *)bufferObj->buffer, dObj->nBlocksToWrite * dObj->writeBlockSize);

                dObj->ewState = DRV_MEMORY_EW_ERASE_SECTOR;
                /* Fall through for Erase operation. */
            }
            else
            {
                break;
            }
        }

        case DRV_MEMORY_EW_ERASE_SECTOR:
        {
            transferStatus = DRV_MEMORY_HandleErase(dObj, NULL, dObj->sectorNumber, 1);
            if (transferStatus == MEMORY_DEVICE_TRANSFER_COMPLETED)
            {
                dObj->ewState = DRV_MEMORY_EW_WRITE_SECTOR;

                transferStatus = MEMORY_DEVICE_TRANSFER_BUSY;
            }
            break;
        }

        case DRV_MEMORY_EW_WRITE_SECTOR:
        {
            transferStatus = DRV_MEMORY_HandleWrite (dObj, dObj->writePtr, dObj->sectorNumber * pagesPerSector, pagesPerSector);

            if (transferStatus == MEMORY_DEVICE_TRANSFER_COMPLETED)
            {
                if ((bufferObj->nBlocks - dObj->nBlocksToWrite) == 0U)
                {
                    /* This is the last write operation. */
                    break;
                }

                /* Update the number of block still to be written, sector address
                 * and the buffer pointer */
                bufferObj->nBlocks -= dObj->nBlocksToWrite;
                bufferObj->blockStart += dObj->nBlocksToWrite;
                bufferObj->buffer += (dObj->nBlocksToWrite * dObj->writeBlockSize);
                dObj->ewState = DRV_MEMORY_EW_INIT;

                transferStatus = MEMORY_DEVICE_TRANSFER_BUSY;
            }

            break;
        }
    }

    return transferStatus;
}
/* MISRAC 2012 deviation block end */

static bool DRV_MEMORY_StartXfer( DRV_MEMORY_OBJECT *dObj )
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_BUFFER_OBJECT *bufferObj = NULL;
    DRV_MEMORY_EVENT event = DRV_MEMORY_EVENT_COMMAND_ERROR;
    MEMORY_DEVICE_TRANSFER_STATUS transferStatus = MEMORY_DEVICE_TRANSFER_ERROR_UNKNOWN;
    bool isSuccess = false;
    SYS_TIME_HANDLE handle = SYS_TIME_HANDLE_INVALID;

    bufferObj = &dObj->currentBufObj;

    /* Init the various sub state machines. */
    dObj->readState  = DRV_MEMORY_READ_INIT;
    dObj->writeState = DRV_MEMORY_WRITE_INIT;
    dObj->eraseState = DRV_MEMORY_ERASE_INIT;
    dObj->ewState = DRV_MEMORY_EW_INIT;

    bufferObj->status = DRV_MEMORY_COMMAND_IN_PROGRESS;

    transferStatus = gMemoryXferFuncPtr[bufferObj->opType](dObj, &bufferObj->buffer[0], bufferObj->blockStart, bufferObj->nBlocks);

    while (transferStatus == MEMORY_DEVICE_TRANSFER_BUSY)
    {
        if ((dObj->isMemDevInterruptEnabled == false) && (dObj->memDevStatusPollUs > 0U))
        {
            handle = SYS_TIME_CallbackRegisterUS(DRV_MEMORY_TimerHandler, (uintptr_t)dObj, dObj->memDevStatusPollUs, SYS_TIME_SINGLE);

            if (handle == SYS_TIME_HANDLE_INVALID)
            {
                return false;
            }
            /* Wait for the request to process before checking status. This semaphore is released from the
             * system timer handler
            */
            if (OSAL_RESULT_SUCCESS == OSAL_SEM_Pend( &dObj->transferDone, OSAL_WAIT_FOREVER ))
            {
                    (void) SYS_TIME_TimerDestroy(handle);
            }
            else
            {
                return false;
            }
        }
        else if ((dObj->isMemDevInterruptEnabled == true) && (dObj->memDevStatusPollUs == 0U))
        {
            if (dObj->isTransferDone == false)
            {
                /* Wait for the request to process before checking status. This semaphore is released from the
                 * event handler called from attached memory device.
                */
                if (OSAL_RESULT_SUCCESS != OSAL_SEM_Pend( &dObj->transferDone, OSAL_WAIT_FOREVER ))
                {
                    return false;
                }
            }
        }
        else
        {
            /* Nothing to do */
        }

        transferStatus = gMemoryXferFuncPtr[bufferObj->opType](dObj, &bufferObj->buffer[0], bufferObj->blockStart, bufferObj->nBlocks);
    }

    if (transferStatus == MEMORY_DEVICE_TRANSFER_COMPLETED)
    {
        bufferObj->status = DRV_MEMORY_COMMAND_COMPLETED;
        event = DRV_MEMORY_EVENT_COMMAND_COMPLETE;
        isSuccess = true;
    }
    else if (transferStatus >= MEMORY_DEVICE_TRANSFER_ERROR_UNKNOWN)
    {
        /* The operation has failed. */
        bufferObj->status = DRV_MEMORY_COMMAND_ERROR_UNKNOWN;
        event = DRV_MEMORY_EVENT_COMMAND_ERROR;
    }
    else
    {
        /* Nothing to do */
    }

    clientObj = (DRV_MEMORY_CLIENT_OBJECT *)bufferObj->hClient;

    if(clientObj->transferHandler != NULL)
    {
        /* Call the event handler */
        clientObj->transferHandler((SYS_MEDIA_BLOCK_EVENT)event, (DRV_MEMORY_COMMAND_HANDLE)bufferObj->commandHandle, clientObj->context);
    }

    return isSuccess;
}

static bool DRV_MEMORY_SetupXfer
(
    const DRV_HANDLE handle,
    DRV_MEMORY_COMMAND_HANDLE *commandHandle,
    void *buffer,
    uint32_t blockStart,
    uint32_t nBlock,
    uint8_t  geometry_type,
    DRV_MEMORY_OPERATION_TYPE opType,
    DRV_IO_INTENT io_intent
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;
    bool isSuccess = false;

    if (commandHandle != NULL)
    {
        *commandHandle = DRV_MEMORY_COMMAND_HANDLE_INVALID;
    }

    /* Validate the driver handle */
    clientObj = DRV_MEMORY_DriverHandleValidate(handle);

    if (clientObj == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "Invalid Memory driver handle.\n");
        return isSuccess;
    }

    /* Check if the driver was opened with read intent */
    if (((uint32_t)clientObj->intent & (uint32_t)io_intent) == 0U)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "Memory Driver Opened with invalid intent.\n");
        return isSuccess;
    }

    dObj = &gDrvMemoryObj[clientObj->drvIndex];

    if ((buffer == NULL) && (opType != DRV_MEMORY_OPERATION_TYPE_ERASE))
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "Memory Driver Invalid Buffer.\n");
        return isSuccess;
    }

    if ((nBlock == 0U) || (((uint64_t)blockStart + nBlock) > dObj->mediaGeometryTable[geometry_type].numBlocks))
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "Memory Driver Invalid Block parameters.\n");
        return isSuccess;
    }

    if (OSAL_MUTEX_Lock(&dObj->transferMutex, OSAL_WAIT_FOREVER ) == OSAL_RESULT_SUCCESS)
    {
        /* For Memory Device which do not support Erase */
        if (dObj->memoryDevice->SectorErase == NULL)
        {
            if (opType == DRV_MEMORY_OPERATION_TYPE_ERASE)
            {
                (void) OSAL_MUTEX_Unlock(&dObj->transferMutex);
                return isSuccess;
            }
            else if (opType == DRV_MEMORY_OPERATION_TYPE_ERASE_WRITE)
            {
                opType = DRV_MEMORY_OPERATION_TYPE_WRITE;
            }
            else
            {
                /* Nothing to do */
            }

        }

        DRV_MEMORY_AllocateBufferObject (clientObj, commandHandle, buffer, blockStart, nBlock, opType);

        isSuccess = DRV_MEMORY_StartXfer(dObj);

        (void) OSAL_MUTEX_Unlock(&dObj->transferMutex);
    }

    return isSuccess;
}

// *****************************************************************************
// *****************************************************************************
// Section: MEMORY Driver System Routines
// *****************************************************************************
// *****************************************************************************

/*
 * MISRA C-2012 Rule 11.1,11.3, and 11.8 deviated below. Deviation record ID -
 * H3_MISRAC_2012_R_11_1_DR_1, H3_MISRAC_2012_R_11_3_DR_1 & H3_MISRAC_2012_R_11_8_DR_1
 */

SYS_MODULE_OBJ DRV_MEMORY_Initialize
(
    const SYS_MODULE_INDEX drvIndex,
    const SYS_MODULE_INIT *const init
)
{
    DRV_MEMORY_OBJECT *dObj = (DRV_MEMORY_OBJECT*) NULL;
    DRV_MEMORY_INIT *memoryInit = NULL;

    /* Validate the driver index */
    if (drvIndex >= DRV_MEMORY_INSTANCES_NUMBER)
    {
        return SYS_MODULE_OBJ_INVALID;
    }

    dObj = &gDrvMemoryObj[drvIndex];

    /* Check if the instance has already been initialized. */
    if (dObj->inUse)
    {
        return SYS_MODULE_OBJ_INVALID;
    }

    dObj->status = SYS_STATUS_UNINITIALIZED;

    dObj->isTransferDone = false;

    /* Indicate that this object is in use */
    dObj->inUse = true;

    /* Assign to the local pointer the init data passed */
    memoryInit = (DRV_MEMORY_INIT *)init;

    /* Initialize number of clients */
    dObj->numClients = 0;

    dObj->clientObjPool       = (DRV_MEMORY_CLIENT_OBJECT *)memoryInit->clientObjPool;
    dObj->nClientsMax         = memoryInit->nClientsMax;
    dObj->bufferToken         = 1;
    dObj->clientToken         = 1;

    /* Initialize the attached memory device functions */
    dObj->memoryDevice = memoryInit->memoryDevice;

    dObj->memDevIndex = memoryInit->memDevIndex;
    dObj->memDevHandle = DRV_HANDLE_INVALID;

    dObj->isMemDevInterruptEnabled = memoryInit->isMemDevInterruptEnabled;
    dObj->memDevStatusPollUs = memoryInit->memDevStatusPollUs;

    /* Set the erase buffer */
    dObj->ewBuffer = memoryInit->ewBuffer;

    if (OSAL_MUTEX_Create(&dObj->clientMutex) == OSAL_RESULT_FAIL)
    {
        /* There was insufficient memory available for the mutex to be created */
        return SYS_MODULE_OBJ_INVALID;
    }

    if (OSAL_MUTEX_Create(&dObj->transferMutex) == OSAL_RESULT_FAIL)
    {
        /* There was insufficient memory available for the mutex to be created */
        return SYS_MODULE_OBJ_INVALID;
    }

    if (OSAL_RESULT_FAIL == OSAL_SEM_Create(&dObj->transferDone,OSAL_SEM_TYPE_BINARY, 0, 0))
    {
        /* There was insufficient memory available for the semaphore to be created */
        return SYS_MODULE_OBJ_INVALID;
    }
    /* Set the driver state as busy as the attached memory device needs to be opened and
     * queried for the geometry data. */
    dObj->status  = SYS_STATUS_BUSY;

    /* Return the driver index */
    return drvIndex;
}

SYS_STATUS DRV_MEMORY_Status
(
    SYS_MODULE_OBJ object
)
{
    /* Validate the object */
    if ((object == SYS_MODULE_OBJ_INVALID) || (object >= DRV_MEMORY_INSTANCES_NUMBER))
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO,"DRV_MEMORY_Status(): Invalid parameter.\n");
        return SYS_STATUS_UNINITIALIZED;
    }

    /* Return the driver status */
    return (gDrvMemoryObj[object].status);
}

static SYS_STATUS DRV_MEMORY_IsReady(DRV_MEMORY_OBJECT *dObj)
{
    SYS_STATUS status = SYS_STATUS_BUSY;

    if (dObj->memoryDevice->Status != NULL)
    {
        if (dObj->memoryDevice->Status(dObj->memDevIndex) != SYS_STATUS_READY)
        {
            /* Attached memory device is not ready */
            return status;
        }
    }

    if (dObj->memoryDevice->Open != NULL)
    {
        dObj->memDevHandle = dObj->memoryDevice->Open(dObj->memDevIndex, (DRV_IO_INTENT)((uint32_t)DRV_IO_INTENT_READWRITE | (uint32_t)DRV_IO_INTENT_EXCLUSIVE));

        if (dObj->memDevHandle == DRV_HANDLE_INVALID)
        {
             return status;
        }
    }

    if (true == DRV_MEMORY_UpdateGeometry(dObj))
    {
        status = SYS_STATUS_READY;
        dObj->status = SYS_STATUS_READY;
    }

    return status;
}
// *****************************************************************************
// *****************************************************************************
// Section: MEMORY Driver Client Routines
// *****************************************************************************
// *****************************************************************************

DRV_HANDLE DRV_MEMORY_Open
(
    const SYS_MODULE_INDEX drvIndex,
    const DRV_IO_INTENT ioIntent
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;
    uint32_t iClient;

    /* Validate the driver index */
    if (drvIndex >= DRV_MEMORY_INSTANCES_NUMBER)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_Open(): Invalid driver index.\n");
        return DRV_HANDLE_INVALID;
    }

    dObj = &gDrvMemoryObj[drvIndex];

    /* Check if the driver is ready to be opened */
    if (dObj->status != SYS_STATUS_READY)
    {
        if (DRV_MEMORY_IsReady(dObj) != SYS_STATUS_READY)
        {
            SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_Open(): Driver is not ready.\n");
            return DRV_HANDLE_INVALID;
        }
    }

    /* Acquire the instance specific mutex to protect the instance specific
     * client pool
     */
    if (OSAL_MUTEX_Lock(&dObj->clientMutex , OSAL_WAIT_FOREVER ) == OSAL_RESULT_FAIL)
    {
        return DRV_HANDLE_INVALID;
    }

    /* Check if the driver has already been opened in exclusive mode */
    if (dObj->isExclusive)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_Open(): Driver is already open in exclusive mode.\n");
        (void) OSAL_MUTEX_Unlock( &dObj->clientMutex);
        return DRV_HANDLE_INVALID;
    }

    /* Driver has already been opened and cannot be opened exclusively */
    if ((dObj->numClients > 0U) && (((uint32_t)ioIntent & (uint32_t)DRV_IO_INTENT_EXCLUSIVE) != 0U))
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_Open(): Driver is already open. Can't be opened in exclusive mode.\n");
        (void) OSAL_MUTEX_Unlock( &dObj->clientMutex);
        return DRV_HANDLE_INVALID;
    }

    /* Allocate a client object from client pool */
    for(iClient = 0; iClient != dObj->nClientsMax; iClient++)
    {
        if (dObj->clientObjPool[iClient].inUse == false)
        {
            clientObj = &dObj->clientObjPool[iClient];

            /* Found a client object that can be used */
            clientObj->inUse = true;
            clientObj->drvIndex = (uint8_t)drvIndex;
            clientObj->intent = ioIntent;
            clientObj->transferHandler = NULL;

            if (((uint32_t)ioIntent & (uint32_t)DRV_IO_INTENT_EXCLUSIVE) != 0U)
            {
                /* Driver was opened in exclusive mode */
                dObj->isExclusive = true;
            }

            dObj->numClients ++;

            clientObj->clientHandle = DRV_MEMORY_MAKE_HANDLE((uint32_t)dObj->clientToken, (uint32_t)drvIndex, iClient);

            dObj->clientToken = DRV_MEMORY_UPDATE_TOKEN(dObj->clientToken);

            if (clientObj->clientHandle != DRV_HANDLE_INVALID)
            {
                if (dObj->isMemDevInterruptEnabled == true)
                {
                    if (dObj->memoryDevice->EventHandlerSet != NULL)
                    {
                        dObj->memoryDevice->EventHandlerSet(dObj->memDevHandle, DRV_MEMORY_EventHandler, (uintptr_t)dObj);
                    }
                }
            }

            break;
        }
    }

    (void) OSAL_MUTEX_Unlock(&dObj->clientMutex);

    return (clientObj != NULL) ? ((DRV_HANDLE)clientObj->clientHandle) : DRV_HANDLE_INVALID;
}

void DRV_MEMORY_Close
(
    const DRV_HANDLE handle
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;

    /* Get the Client object from the handle passed */
    clientObj = DRV_MEMORY_DriverHandleValidate(handle);

    /* Check if the client object is valid */
    if (clientObj == NULL)
    {
        SYS_DEBUG_MESSAGE (SYS_ERROR_INFO, "DRV_MEMORY_Close(): Invalid handle.\n");
        return;
    }

    dObj = &gDrvMemoryObj[clientObj->drvIndex];

    if (OSAL_MUTEX_Lock(&dObj->clientMutex , OSAL_WAIT_FOREVER ) == OSAL_RESULT_SUCCESS)
    {
        /* Update the client count */
        dObj->numClients --;
        dObj->isExclusive = false;

        /* Free the Client Instance */
        clientObj->inUse = false;

        /* Release the instance specific mutex */
        (void) OSAL_MUTEX_Unlock( &dObj->clientMutex );
    }
}

void DRV_MEMORY_Read
(
    const DRV_HANDLE handle,
    DRV_MEMORY_COMMAND_HANDLE *commandHandle,
    void *targetBuffer,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    (void) DRV_MEMORY_SetupXfer(handle, commandHandle, targetBuffer, blockStart, nBlock,
            SYS_MEDIA_GEOMETRY_TABLE_READ_ENTRY,
            DRV_MEMORY_OPERATION_TYPE_READ,
            DRV_IO_INTENT_READ);
}

bool DRV_MEMORY_SyncRead
(
    const DRV_HANDLE handle,
    void *targetBuffer,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    return DRV_MEMORY_SetupXfer(handle, NULL, targetBuffer, blockStart, nBlock,
                SYS_MEDIA_GEOMETRY_TABLE_READ_ENTRY,
                DRV_MEMORY_OPERATION_TYPE_READ,
                DRV_IO_INTENT_READ);
}

void DRV_MEMORY_Write
(
    const DRV_HANDLE handle,
    DRV_MEMORY_COMMAND_HANDLE *commandHandle,
    void *sourceBuffer,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    (void) DRV_MEMORY_SetupXfer(handle, commandHandle, sourceBuffer, blockStart, nBlock,
            SYS_MEDIA_GEOMETRY_TABLE_WRITE_ENTRY,
            DRV_MEMORY_OPERATION_TYPE_WRITE,
            DRV_IO_INTENT_WRITE);
}

bool DRV_MEMORY_SyncWrite
(
    const DRV_HANDLE handle,
    void *sourceBuffer,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    return DRV_MEMORY_SetupXfer(handle, NULL, sourceBuffer, blockStart, nBlock,
                SYS_MEDIA_GEOMETRY_TABLE_WRITE_ENTRY,
                DRV_MEMORY_OPERATION_TYPE_WRITE,
                DRV_IO_INTENT_WRITE);
}

void DRV_MEMORY_Erase
(
    const DRV_HANDLE handle,
    DRV_MEMORY_COMMAND_HANDLE *commandHandle,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    (void) DRV_MEMORY_SetupXfer(handle, commandHandle, NULL, blockStart, nBlock,
            SYS_MEDIA_GEOMETRY_TABLE_ERASE_ENTRY,
            DRV_MEMORY_OPERATION_TYPE_ERASE,
            DRV_IO_INTENT_WRITE);
}

bool DRV_MEMORY_SyncErase
(
    const DRV_HANDLE handle,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    return DRV_MEMORY_SetupXfer(handle, NULL, NULL, blockStart, nBlock,
                SYS_MEDIA_GEOMETRY_TABLE_ERASE_ENTRY,
                DRV_MEMORY_OPERATION_TYPE_ERASE,
                DRV_IO_INTENT_WRITE);
}

void DRV_MEMORY_EraseWrite
(
    const DRV_HANDLE handle,
    DRV_MEMORY_COMMAND_HANDLE *commandHandle,
    void *sourceBuffer,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    (void) DRV_MEMORY_SetupXfer(handle, commandHandle, sourceBuffer, blockStart, nBlock,
            SYS_MEDIA_GEOMETRY_TABLE_WRITE_ENTRY,
            DRV_MEMORY_OPERATION_TYPE_ERASE_WRITE,
            DRV_IO_INTENT_WRITE);
}

bool DRV_MEMORY_SyncEraseWrite
(
    const DRV_HANDLE handle,
    void *sourceBuffer,
    uint32_t blockStart,
    uint32_t nBlock
)
{
    return DRV_MEMORY_SetupXfer(handle, NULL, sourceBuffer, blockStart, nBlock,
                SYS_MEDIA_GEOMETRY_TABLE_WRITE_ENTRY,
                DRV_MEMORY_OPERATION_TYPE_ERASE_WRITE,
                DRV_IO_INTENT_WRITE);
}

DRV_MEMORY_COMMAND_STATUS DRV_MEMORY_CommandStatusGet
(
    const DRV_HANDLE handle,
    const DRV_MEMORY_COMMAND_HANDLE commandHandle
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;
    DRV_MEMORY_COMMAND_STATUS status = DRV_MEMORY_COMMAND_ERROR_UNKNOWN;

    /* Get the Client object from the handle passed */
    clientObj = DRV_MEMORY_DriverHandleValidate(handle);

    /* Check if the client object is valid */
    if (clientObj == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_CommandStatusGet(): Invalid driver handle.\n");
        return status;
    }

    dObj = &gDrvMemoryObj[clientObj->drvIndex];

    /* Acquire the instance specific mutex to protect the instance specific
     * client pool
     */
    if (OSAL_MUTEX_Lock(&dObj->transferMutex , OSAL_WAIT_FOREVER ) == OSAL_RESULT_SUCCESS)
    {
        /* Compare the buffer handle with buffer handle in the object */
        if(dObj->currentBufObj.commandHandle == commandHandle)
        {
            /* Return the last known buffer object status */
            status = (dObj->currentBufObj.status);
        }
        (void) OSAL_MUTEX_Unlock(&dObj->transferMutex);
    }

    return status;
}


void DRV_MEMORY_TransferHandlerSet
(
    const DRV_HANDLE handle,
    const void * transferHandler,
    const uintptr_t context
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;

    clientObj = DRV_MEMORY_DriverHandleValidate(handle);

    /* Check if the client object is valid */
    if (clientObj == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_TransferHandlerSet(): Invalid driver handle.\n");
        return;
    }

    /* Set the event handler */
    clientObj->transferHandler = (DRV_MEMORY_TRANSFER_HANDLER)transferHandler;
    clientObj->context = context;
}
/* MISRAC 2012 deviation block end */
SYS_MEDIA_GEOMETRY * DRV_MEMORY_GeometryGet
(
    const DRV_HANDLE handle
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;

    /* Get the Client object from the handle passed */
    clientObj = DRV_MEMORY_DriverHandleValidate(handle);

    /* Check if the client object is valid */
    if (clientObj == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_GeometryGet(): Invalid driver handle.\n");
        return NULL;
    }

    dObj = &gDrvMemoryObj[clientObj->drvIndex];
    return &dObj->mediaGeometryObj;
}

bool DRV_MEMORY_IsAttached
(
    const DRV_HANDLE handle
)
{
    /* Validate the driver handle */
    if (DRV_MEMORY_DriverHandleValidate(handle) == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_IsAttached(): Invalid driver handle.\n");
        return false;
    }

   return true;
}

bool DRV_MEMORY_IsWriteProtected
(
    const DRV_HANDLE handle
)
{
    /* This function always returns false */
    return false;
}

uintptr_t DRV_MEMORY_AddressGet
(
    const DRV_HANDLE handle
)
{
    DRV_MEMORY_CLIENT_OBJECT *clientObj = NULL;
    DRV_MEMORY_OBJECT *dObj = NULL;

    /* Get the Client object from the handle passed */
    clientObj = DRV_MEMORY_DriverHandleValidate(handle);

    /* Check if the client object is valid */
    if (clientObj == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_INFO, "DRV_MEMORY_AddressGet(): Invalid driver handle.\n");
        return (0U);
    }

    dObj = &gDrvMemoryObj[clientObj->drvIndex];

    return dObj->blockStartAddress;
}
