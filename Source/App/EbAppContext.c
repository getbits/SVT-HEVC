/*
* Copyright(c) 2018 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/

/***************************************
 * Includes
 ***************************************/

#include <stdlib.h>

#include "EbTypes.h"
#include "EbAppContext.h"
#include "EbAppConfig.h"
#include "EbApiSei.h"


#define INPUT_SIZE_576p_TH				0x90000		// 0.58 Million   
#define INPUT_SIZE_1080i_TH				0xB71B0		// 0.75 Million
#define INPUT_SIZE_1080p_TH				0x1AB3F0	// 1.75 Million
#define INPUT_SIZE_4K_TH				0x29F630	// 2.75 Million   

#define SIZE_OF_ONE_FRAME_IN_BYTES(width, height,is16bit) ( ( ((width)*(height)*3)>>1 )<<is16bit)
#define IS_16_BIT(bit_depth) (bit_depth==10?1:0)
#define EB_OUTPUTSTREAMBUFFERSIZE_MACRO(ResolutionSize)                ((ResolutionSize) < (INPUT_SIZE_1080i_TH) ? 0x1E8480 : (ResolutionSize) < (INPUT_SIZE_1080p_TH) ? 0x2DC6C0 : (ResolutionSize) < (INPUT_SIZE_4K_TH) ? 0x2DC6C0 : 0x2DC6C0  )   

 /***************************************
 * Variables Defining a memory table 
 *  hosting all allocated pointers 
 ***************************************/
EbMemoryMapEntry               *appMemoryMap; 
EB_U32                         *appMemoryMapIndex;
EB_U64                         *totalAppMemory;
EB_U32                         appMallocCount = 0;
static EbMemoryMapEntry        *appMemoryMapAllChannels[MAX_CHANNEL_NUMBER]; 
static EB_U32                   appMemoryMapIndexAllChannels[MAX_CHANNEL_NUMBER];
static EB_U64                   appMemoryMallocdAllChannels[MAX_CHANNEL_NUMBER];

/***************************************
* Allocation and initializing a memory table
*  hosting all allocated pointers
***************************************/
void AllocateMemoryTable(
    EB_U32	instanceIdx)
{
    // Malloc Memory Table for the instance @ instanceIdx
    appMemoryMapAllChannels[instanceIdx]        = (EbMemoryMapEntry*)malloc(sizeof(EbMemoryMapEntry) * MAX_APP_NUM_PTR);

    // Init the table index
    appMemoryMapIndexAllChannels[instanceIdx]   = 0;

    // Size of the table
    appMemoryMallocdAllChannels[instanceIdx]    = sizeof(EbMemoryMapEntry) * MAX_APP_NUM_PTR;
    totalAppMemory = &appMemoryMallocdAllChannels[instanceIdx];

    // Set pointer to the first entry
    appMemoryMap                                = appMemoryMapAllChannels[instanceIdx];

    // Set index to the first entry
    appMemoryMapIndex                           = &appMemoryMapIndexAllChannels[instanceIdx];

    // Init Number of pointers
    appMallocCount = 0;

    return;
}


/*************************************
**************************************
*** Helper functions Input / Output **
**************************************
**************************************/

/***********************************
 * AppContext Constructor
 ***********************************/
void EbAppContextCtor(EbAppContext_t *contextPtr)
{
    contextPtr->inputContext.processedByteCount     = 0;
    contextPtr->inputContext.processedFrameCount    = 0;
    contextPtr->inputContext.previousTimeSeconds    = 0;
    contextPtr->inputContext.measuredFrameRate      = 0;

    contextPtr->feedBackIsComplete                  = EB_FALSE;
    return;
}


/***********************************
* ParentAppContext Constructor 
*  Allowing multi instance support
***********************************/
void EbParentAppContextCtor(EbAppContext_t **contextPtr, EbParentAppContext_t *parentContextPtr, unsigned int numChannels, EB_U32 totalBuffersSize)
{

	unsigned int count;
	AppCommandFifoCtor(&parentContextPtr->fifo, totalBuffersSize);

	for (count=0; count < numChannels; ++count){
		parentContextPtr->appCallBacks[count]			= contextPtr[count];
		parentContextPtr->appCallBacks[count]->fifoPtr	= &parentContextPtr->fifo;
	}

	parentContextPtr->numChannels = (EB_U8) numChannels;
    

    return;
}

/***********************************
 * AppContext Destructor
 ***********************************/
void EbParentAppContextDtor(EbParentAppContext_t *parentContextPtr)
{
    AppCommandFifoDtor(&parentContextPtr->fifo);

    return;
}

/******************************************************
* Copy fields from the stream to the input buffer
    Input   : stream
    Output  : valid input buffer
******************************************************/
void ProcessInputFieldBufferingMode(
    EB_U64                    processedFrameCount,
    int                      *filledLen,
    FILE                     *inputFile,
    EB_U8                    *lumaInputPtr,
    EB_U8                    *cbInputPtr,
    EB_U8                    *crInputPtr,
    EB_U32                    inputPaddedWidth,
    EB_U32                    inputPaddedHeight,
    unsigned char             is16bit) {


    EB_U64  sourceLumaRowSize   = (EB_U64)(inputPaddedWidth << is16bit);
    EB_U64  sourceChromaRowSize = sourceLumaRowSize >> 1;
   
    EB_U8  *ebInputPtr;
    EB_U32  inputRowIndex;

    // Y
    ebInputPtr = lumaInputPtr;
    // Skip 1 luma row if bottom field (point to the bottom field)
    if (processedFrameCount % 2 != 0)
        fseeko64(inputFile, (long)sourceLumaRowSize, SEEK_CUR);

    for (inputRowIndex = 0; inputRowIndex < inputPaddedHeight; inputRowIndex++) {

        *filledLen += (EB_U32)fread(ebInputPtr, 1, sourceLumaRowSize, inputFile);
        // Skip 1 luma row (only fields)
        fseeko64(inputFile, (long)sourceLumaRowSize, SEEK_CUR);
        ebInputPtr += sourceLumaRowSize;
    }

    // U
    ebInputPtr = cbInputPtr;
    // Step back 1 luma row if bottom field (undo the previous jump), and skip 1 chroma row if bottom field (point to the bottom field)
    if (processedFrameCount % 2 != 0) {
        fseeko64(inputFile, -(long)sourceLumaRowSize, SEEK_CUR);
        fseeko64(inputFile, (long)sourceChromaRowSize, SEEK_CUR);
    }

    for (inputRowIndex = 0; inputRowIndex < inputPaddedHeight >> 1; inputRowIndex++) {

        *filledLen += (EB_U32)fread(ebInputPtr, 1, sourceChromaRowSize, inputFile);
        // Skip 1 chroma row (only fields)
        fseeko64(inputFile, (long)sourceChromaRowSize, SEEK_CUR);
        ebInputPtr += sourceChromaRowSize;
    }

    // V
    ebInputPtr = crInputPtr;
    // Step back 1 chroma row if bottom field (undo the previous jump), and skip 1 chroma row if bottom field (point to the bottom field) 
    // => no action


    for (inputRowIndex = 0; inputRowIndex < inputPaddedHeight >> 1; inputRowIndex++) {

        *filledLen += (EB_U32)fread(ebInputPtr, 1, sourceChromaRowSize, inputFile);
        // Skip 1 chroma row (only fields)
        fseeko64(inputFile, (long)sourceChromaRowSize, SEEK_CUR);
        ebInputPtr += sourceChromaRowSize;
    }

    // Step back 1 chroma row if bottom field (undo the previous jump)
    if (processedFrameCount % 2 != 0) {
        fseeko64(inputFile, -(long)sourceChromaRowSize, SEEK_CUR);
    }
}


/***********************************************
* Copy configuration parameters from 
*  The config structure, to the 
*  callback structure to send to the library
***********************************************/
EB_ERRORTYPE CopyConfigurationParameters(
    EbConfig_t				*config,
    EbAppContext_t			*callbackData,
    EB_U32                   instanceIdx)
{
    EB_ERRORTYPE   return_error = EB_ErrorNone;
    EB_U32         hmeRegionIndex;

    // Assign Instance index to the library
    callbackData->instanceIdx = (EB_U8)instanceIdx;

    // Initialize Port Activity Flags
    callbackData->outputStreamPortActive = APP_PortActive;

    callbackData->inputPortDefinition.nFrameWidth = config->sourceWidth;
    callbackData->inputPortDefinition.nFrameHeight = config->sourceHeight;
    callbackData->inputPortDefinition.nStride = config->inputPaddedWidth;

    callbackData->ebEncParameters.interlacedVideo = (EB_BOOL)config->interlacedVideo;
    callbackData->ebEncParameters.intraPeriodLength = config->intraPeriod;
    callbackData->ebEncParameters.intraRefreshType = config->intraRefreshType;
    callbackData->ebEncParameters.baseLayerSwitchMode = config->baseLayerSwitchMode;
    callbackData->ebEncParameters.encMode = (EB_BOOL)config->encMode;
    callbackData->ebEncParameters.frameRate = config->frameRate;
    callbackData->ebEncParameters.frameRateDenominator = config->frameRateDenominator;
    callbackData->ebEncParameters.frameRateNumerator = config->frameRateNumerator;
	callbackData->ebEncParameters.hierarchicalLevels = config->hierarchicalLevels;

	callbackData->ebEncParameters.predStructure = (EB_PRED)config->predStructure;

    callbackData->ebEncParameters.sceneChangeDetection = config->sceneChangeDetection;
    callbackData->ebEncParameters.lookAheadDistance = config->lookAheadDistance;
    callbackData->ebEncParameters.framesToBeEncoded = config->framesToBeEncoded;
    callbackData->ebEncParameters.rateControlMode = config->rateControlMode;
    callbackData->ebEncParameters.targetBitRate = config->targetBitRate;
    callbackData->ebEncParameters.maxQpAllowed = config->maxQpAllowed;
    callbackData->ebEncParameters.minQpAllowed = config->minQpAllowed;
    callbackData->ebEncParameters.qp = config->qp;
    callbackData->ebEncParameters.useQpFile = (EB_BOOL)config->useQpFile;
    callbackData->ebEncParameters.disableDlfFlag = (EB_BOOL)config->disableDlfFlag;
    callbackData->ebEncParameters.enableSaoFlag = (EB_BOOL)config->enableSaoFlag;
    callbackData->ebEncParameters.useDefaultMeHme = (EB_BOOL)config->useDefaultMeHme;
    callbackData->ebEncParameters.enableHmeFlag = (EB_BOOL)config->enableHmeFlag;
    callbackData->ebEncParameters.enableHmeLevel0Flag = (EB_BOOL)config->enableHmeLevel0Flag;
    callbackData->ebEncParameters.enableHmeLevel1Flag = (EB_BOOL)config->enableHmeLevel1Flag;
    callbackData->ebEncParameters.enableHmeLevel2Flag = (EB_BOOL)config->enableHmeLevel2Flag;
    callbackData->ebEncParameters.searchAreaWidth = config->searchAreaWidth;
    callbackData->ebEncParameters.searchAreaHeight = config->searchAreaHeight;
    callbackData->ebEncParameters.numberHmeSearchRegionInWidth = config->numberHmeSearchRegionInWidth;
    callbackData->ebEncParameters.numberHmeSearchRegionInHeight = config->numberHmeSearchRegionInHeight;
    callbackData->ebEncParameters.hmeLevel0TotalSearchAreaWidth = config->hmeLevel0TotalSearchAreaWidth;
    callbackData->ebEncParameters.hmeLevel0TotalSearchAreaHeight = config->hmeLevel0TotalSearchAreaHeight;
    callbackData->ebEncParameters.constrainedIntra = (EB_BOOL)config->constrainedIntra;

    callbackData->ebEncParameters.tune = config->tune;

    callbackData->ebEncParameters.channelId = config->channelId;
    callbackData->ebEncParameters.activeChannelCount = config->activeChannelCount;
    callbackData->ebEncParameters.useRoundRobinThreadAssignment = (EB_BOOL)config->useRoundRobinThreadAssignment;

	callbackData->ebEncParameters.bitRateReduction = (EB_U8)config->bitRateReduction;
	callbackData->ebEncParameters.improveSharpness = (EB_U8)config->improveSharpness;
    callbackData->ebEncParameters.videoUsabilityInfo = config->videoUsabilityInfo;
    callbackData->ebEncParameters.highDynamicRangeInput = config->highDynamicRangeInput;
    callbackData->ebEncParameters.accessUnitDelimiter = config->accessUnitDelimiter;
    callbackData->ebEncParameters.bufferingPeriodSEI = config->bufferingPeriodSEI;
    callbackData->ebEncParameters.pictureTimingSEI = config->pictureTimingSEI;
    callbackData->ebEncParameters.registeredUserDataSeiFlag = config->registeredUserDataSeiFlag;
    callbackData->ebEncParameters.unregisteredUserDataSeiFlag = config->unregisteredUserDataSeiFlag;
    callbackData->ebEncParameters.recoveryPointSeiFlag = config->recoveryPointSeiFlag;
    callbackData->ebEncParameters.enableTemporalId = config->enableTemporalId;
    callbackData->ebEncParameters.encoderBitDepth = config->encoderBitDepth;
    callbackData->ebEncParameters.compressedTenBitFormat = config->compressedTenBitFormat;
    callbackData->ebEncParameters.profile = config->profile;
    callbackData->ebEncParameters.tier = config->tier;
    callbackData->ebEncParameters.level = config->level;
    callbackData->ebEncParameters.injectorFrameRate = config->injectorFrameRate;
    callbackData->ebEncParameters.speedControlFlag = config->speedControlFlag;
    callbackData->ebEncParameters.inputOutputBufferFifoInitCount = config->inputOutputBufferFifoInitCount;
    callbackData->ebEncParameters.asmType = config->asmType;

    for (hmeRegionIndex = 0; hmeRegionIndex < callbackData->ebEncParameters.numberHmeSearchRegionInWidth; ++hmeRegionIndex) {
        callbackData->ebEncParameters.hmeLevel0SearchAreaInWidthArray[hmeRegionIndex] = config->hmeLevel0SearchAreaInWidthArray[hmeRegionIndex];
        callbackData->ebEncParameters.hmeLevel1SearchAreaInWidthArray[hmeRegionIndex] = config->hmeLevel1SearchAreaInWidthArray[hmeRegionIndex];
        callbackData->ebEncParameters.hmeLevel2SearchAreaInWidthArray[hmeRegionIndex] = config->hmeLevel2SearchAreaInWidthArray[hmeRegionIndex];
    }

    for (hmeRegionIndex = 0; hmeRegionIndex < callbackData->ebEncParameters.numberHmeSearchRegionInHeight; ++hmeRegionIndex) {
        callbackData->ebEncParameters.hmeLevel0SearchAreaInHeightArray[hmeRegionIndex] = config->hmeLevel0SearchAreaInHeightArray[hmeRegionIndex];
        callbackData->ebEncParameters.hmeLevel1SearchAreaInHeightArray[hmeRegionIndex] = config->hmeLevel1SearchAreaInHeightArray[hmeRegionIndex];
        callbackData->ebEncParameters.hmeLevel2SearchAreaInHeightArray[hmeRegionIndex] = config->hmeLevel2SearchAreaInHeightArray[hmeRegionIndex];
    }

    // Video Usability Info
    EB_APP_MALLOC(AppVideoUsabilityInfo_t*, callbackData->ebEncParameters.vuiPtr, sizeof(AppVideoUsabilityInfo_t), EB_N_PTR, EB_ErrorInsufficientResources);

    // Initialize vui parameters
    return_error = (EB_ERRORTYPE)EbAppVideoUsabilityInfoCtor(
        callbackData->ebEncParameters.vuiPtr);
    

    // Set the Parameters
    return_error = EbH265EncInitParameter(
                       callbackData->svtEncoderHandle,
                       &callbackData->inputPortDefinition);

    return return_error;

}


EB_ERRORTYPE AllocateFrameBuffer(
    EbConfig_t				*config,
    EB_U8      			*pBuffer)
{
    EB_ERRORTYPE   return_error = EB_ErrorNone;

    const int tenBitPackedMode = (config->encoderBitDepth > 8) && (config->compressedTenBitFormat == 0) ? 1 : 0;

    // Determine size of each plane
    const size_t luma8bitSize =

        config->inputPaddedWidth    *
        config->inputPaddedHeight   *

        (1 << tenBitPackedMode);

    const size_t chroma8bitSize = luma8bitSize >> 2;
    const size_t luma10bitSize = (config->encoderBitDepth > 8 && tenBitPackedMode == 0) ? luma8bitSize : 0;
    const size_t chroma10bitSize = (config->encoderBitDepth > 8 && tenBitPackedMode == 0) ? chroma8bitSize : 0;

    // Determine  
    EB_H265_ENC_INPUT* inputPtr = (EB_H265_ENC_INPUT*)pBuffer;

    if (luma8bitSize) {
        EB_APP_MALLOC(unsigned char*, inputPtr->luma, luma8bitSize, EB_N_PTR, EB_ErrorInsufficientResources);
    }
    else {
        inputPtr->luma = 0;
    }
    if (chroma8bitSize) {
        EB_APP_MALLOC(unsigned char*, inputPtr->cb, chroma8bitSize, EB_N_PTR, EB_ErrorInsufficientResources);
    }
    else {
        inputPtr->cb = 0;
    }

    if (chroma8bitSize) {
        EB_APP_MALLOC(unsigned char*, inputPtr->cr, chroma8bitSize, EB_N_PTR, EB_ErrorInsufficientResources);
    }
    else {
        inputPtr->cr = 0;
    }

    if (luma10bitSize) {
        EB_APP_MALLOC(unsigned char*, inputPtr->lumaExt, luma10bitSize, EB_N_PTR, EB_ErrorInsufficientResources);
    }
    else {
        inputPtr->lumaExt = 0;
    }

    if (chroma10bitSize) {
        EB_APP_MALLOC(unsigned char*, inputPtr->cbExt, chroma10bitSize, EB_N_PTR, EB_ErrorInsufficientResources);
    }
    else {
        inputPtr->cbExt = 0;
    }

    if (chroma10bitSize) {
        EB_APP_MALLOC(unsigned char*, inputPtr->crExt, chroma10bitSize, EB_N_PTR, EB_ErrorInsufficientResources);

    }
    else {
        inputPtr->crExt = 0;
    }

    return return_error;
}


EB_ERRORTYPE AllocateInputBuffers(
    EbConfig_t				*config,
    EbAppContext_t			*callbackData)
{
    EB_ERRORTYPE   return_error = EB_ErrorNone;
    unsigned int    bufferIndex;
    // Allocate the buffer pools for each port
    // ... Input Port
    EB_APP_MALLOC(EB_BUFFERHEADERTYPE**, callbackData->inputBufferPool, config->inputOutputBufferFifoInitCount * sizeof(EB_BUFFERHEADERTYPE), EB_N_PTR, EB_ErrorInsufficientResources);

    for (bufferIndex = 0; bufferIndex < config->inputOutputBufferFifoInitCount; ++bufferIndex) {

        EB_APP_MALLOC(EB_BUFFERHEADERTYPE*, callbackData->inputBufferPool[bufferIndex], sizeof(EB_BUFFERHEADERTYPE), EB_N_PTR, EB_ErrorInsufficientResources);

        // Initialize Header
        callbackData->inputBufferPool[bufferIndex]->nSize                       = sizeof(EB_BUFFERHEADERTYPE);

        callbackData->inputPortDefinition.nFrameWidth              = config->sourceWidth;
        callbackData->inputPortDefinition.nFrameHeight             = config->sourceHeight;

        EB_APP_MALLOC(EB_U8*, callbackData->inputBufferPool[bufferIndex]->pBuffer, sizeof(EB_H265_ENC_INPUT), EB_N_PTR, EB_ErrorInsufficientResources);

        if (config->bufferedInput == -1) {

            // Allocate frame buffer for the pBuffer
            AllocateFrameBuffer(
                    config,
                    callbackData->inputBufferPool[bufferIndex]->pBuffer);
        }

        // Assign the variables 
        callbackData->inputBufferPool[bufferIndex]->nAllocLen               = callbackData->inputPortDefinition.nSize;
        callbackData->inputBufferPool[bufferIndex]->pAppPrivate             = (EB_PTR)callbackData;

    }

    return return_error;
}

EB_ERRORTYPE AllocateOutputBuffers(
    EbConfig_t				*config,
    EbAppContext_t			*callbackData)
{

    EB_ERRORTYPE   return_error = EB_ErrorNone;
    EB_U32		    outputStreamBufferSize;
    unsigned int    bufferIndex;

    // ... Bitstream Port
    EB_APP_MALLOC(EB_BUFFERHEADERTYPE**, callbackData->streamBufferPool, config->inputOutputBufferFifoInitCount * sizeof(EB_BUFFERHEADERTYPE), EB_N_PTR, EB_ErrorInsufficientResources);

    outputStreamBufferSize = (EB_U32)(EB_OUTPUTSTREAMBUFFERSIZE_MACRO(config->inputPaddedHeight * config->inputPaddedWidth));

    callbackData->outputStreamPortDefinition.nStride = outputStreamBufferSize;
    for (bufferIndex = 0; bufferIndex < config->inputOutputBufferFifoInitCount; ++bufferIndex) {
        EB_APP_MALLOC(EB_BUFFERHEADERTYPE*, callbackData->streamBufferPool[bufferIndex], sizeof(EB_BUFFERHEADERTYPE), EB_N_PTR, EB_ErrorInsufficientResources);

        // Initialize Header
        callbackData->streamBufferPool[bufferIndex]->nSize = sizeof(EB_BUFFERHEADERTYPE);

        EB_APP_MALLOC(EB_U8*, callbackData->streamBufferPool[bufferIndex]->pBuffer, callbackData->outputStreamPortDefinition.nStride, EB_N_PTR, EB_ErrorInsufficientResources);

        callbackData->streamBufferPool[bufferIndex]->nAllocLen = callbackData->outputStreamPortDefinition.nStride;
        callbackData->streamBufferPool[bufferIndex]->pAppPrivate = (EB_PTR)callbackData;
        callbackData->streamBufferPool[bufferIndex]->nOutputPortIndex = EB_ENCODERSTREAMPORT;
    }
    return return_error;
}

EB_ERRORTYPE PreloadFramesIntoRam(
    EbConfig_t				*config)
{
    EB_ERRORTYPE   return_error = EB_ErrorNone;
    int processedFrameCount;

    int filledLen;


    int inputPaddedWidth = config->inputPaddedWidth;
    int inputPaddedHeight = config->inputPaddedHeight;

    int readSize;
    unsigned char *ebInputPtr;
    FILE *inputFile = config->inputFile;

    if (config->encoderBitDepth == 10 && config->compressedTenBitFormat == 1)
    {

        readSize = (inputPaddedWidth*inputPaddedHeight * 3) / 2 + (inputPaddedWidth / 4 * inputPaddedHeight * 3) / 2;

    }
    else
    {

        readSize = inputPaddedWidth * inputPaddedHeight * 3 * (config->encoderBitDepth > 8 ? 2 : 1) / 2;

    }
    EB_APP_MALLOC(unsigned char **, config->sequenceBuffer, sizeof(unsigned char*) * config->bufferedInput, EB_N_PTR, EB_ErrorInsufficientResources);


    for (processedFrameCount = 0; processedFrameCount < config->bufferedInput; ++processedFrameCount) {
        EB_APP_MALLOC(unsigned char*, config->sequenceBuffer[processedFrameCount], readSize, EB_N_PTR, EB_ErrorInsufficientResources);
        // Interlaced Video
        if (config->separateFields) {
            EB_BOOL is16bit = config->encoderBitDepth > 8;
            if (is16bit == 0 || (is16bit == 1 && config->compressedTenBitFormat == 0)) {

                const int tenBitPackedMode = (config->encoderBitDepth > 8) && (config->compressedTenBitFormat == 0) ? 1 : 0;

                const size_t luma8bitSize =

                    (config->inputPaddedWidth) *
                    (config->inputPaddedHeight) *

                    (1 << tenBitPackedMode);

                const size_t chroma8bitSize = luma8bitSize >> 2;

                filledLen = 0;

                ProcessInputFieldBufferingMode(
                    processedFrameCount,
                    &filledLen,
                    inputFile,
                    config->sequenceBuffer[processedFrameCount],
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize,
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize + chroma8bitSize,
                    (EB_U32)inputPaddedWidth,
                    (EB_U32)inputPaddedHeight,

                    is16bit);

                if (readSize != filledLen) {

                    fseek(inputFile, 0, SEEK_SET);
                    filledLen = 0;

                    ProcessInputFieldBufferingMode(
                        processedFrameCount,
                        &filledLen,
                        inputFile,
                        config->sequenceBuffer[processedFrameCount],
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize,
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize + chroma8bitSize,
                        (EB_U32)inputPaddedWidth,
                        (EB_U32)inputPaddedHeight,

                        is16bit);
                }

                // Reset the pointer position after a top field
                if (processedFrameCount % 2 == 0) {
                    fseek(inputFile, -(long)(readSize << 1), SEEK_CUR);
                }
            }
            // Unpacked 10 bit
            else {

                const int tenBitPackedMode = (config->encoderBitDepth > 8) && (config->compressedTenBitFormat == 0) ? 1 : 0;

                const size_t luma8bitSize =
                    (config->inputPaddedWidth) *
                    (config->inputPaddedHeight) *
                    (1 << tenBitPackedMode);

                const size_t chroma8bitSize = luma8bitSize >> 2;

                const size_t luma10bitSize = (config->encoderBitDepth > 8 && tenBitPackedMode == 0) ? luma8bitSize : 0;
                const size_t chroma10bitSize = (config->encoderBitDepth > 8 && tenBitPackedMode == 0) ? chroma8bitSize : 0;

                filledLen = 0;

                ProcessInputFieldBufferingMode(
                    processedFrameCount,
                    &filledLen,
                    inputFile,
                    config->sequenceBuffer[processedFrameCount],
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize,
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize + chroma8bitSize,
                    (EB_U32)inputPaddedWidth,
                    (EB_U32)inputPaddedHeight,
                    0);

                ProcessInputFieldBufferingMode(
                    processedFrameCount,
                    &filledLen,
                    inputFile,
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize + (chroma8bitSize << 1),
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize + (chroma8bitSize << 1) + luma10bitSize,
                    config->sequenceBuffer[processedFrameCount] + luma8bitSize + (chroma8bitSize << 1) + luma10bitSize + chroma10bitSize,
                    (EB_U32)inputPaddedWidth,
                    (EB_U32)inputPaddedHeight,
                    0);

                if (readSize != filledLen) {

                    fseek(inputFile, 0, SEEK_SET);
                    filledLen = 0;

                    ProcessInputFieldBufferingMode(
                        processedFrameCount,
                        &filledLen,
                        inputFile,
                        config->sequenceBuffer[processedFrameCount],
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize,
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize + chroma8bitSize,
                        (EB_U32)inputPaddedWidth,
                        (EB_U32)inputPaddedHeight,
                        0);

                    ProcessInputFieldBufferingMode(
                        processedFrameCount,
                        &filledLen,
                        inputFile,
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize + (chroma8bitSize << 1),
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize + (chroma8bitSize << 1) + luma10bitSize,
                        config->sequenceBuffer[processedFrameCount] + luma8bitSize + (chroma8bitSize << 1) + luma10bitSize + chroma10bitSize,
                        (EB_U32)inputPaddedWidth,
                        (EB_U32)inputPaddedHeight,
                        0);

                }

                // Reset the pointer position after a top field
                if (processedFrameCount % 2 == 0) {
                    fseek(inputFile, -(long)(readSize << 1), SEEK_CUR);
                }
            }
        }
        else {

            // Fill the buffer with a complete frame
            filledLen = 0;
            ebInputPtr = config->sequenceBuffer[processedFrameCount];
            filledLen += (EB_U32)fread(ebInputPtr, 1, readSize, inputFile);

            if (readSize != filledLen) {

                fseek(config->inputFile, 0, SEEK_SET);

                // Fill the buffer with a complete frame
                filledLen = 0;
                ebInputPtr = config->sequenceBuffer[processedFrameCount];
                filledLen += (EB_U32)fread(ebInputPtr, 1, readSize, inputFile);
            }
        }
    }

    return return_error;
}


/***************************************
* Functions Implementation
***************************************/

/***************************************
* Encoder Event Callback
* This callback is used for error reporting
* or to signal the encoding being done
***************************************/
EB_ERRORTYPE encoderFeedbackComplete(
    EB_HANDLETYPE     hComponent,     // Component Handle
    EB_PTR            pAppData,       // Pointer to private data
    EB_U32            nData1,         // Type defined by event
    EB_U32            nData2,         // Type defined by event
    EB_PTR            pEventData)     // Pointer to event data
{
    EbAppContext_t *callbackDataPtr = (EbAppContext_t*)pAppData;
    AppCommandItem_t commandItem;

    pAppData = (EB_PTR)0;

    // Configure the command
    commandItem.command = APP_FeedBackIsComplete;
    commandItem.headerPtr = (EB_BUFFERHEADERTYPE*)EB_NULL;
    commandItem.instanceIndex = callbackDataPtr->instanceIdx;
    AppCommandFifoPush(callbackDataPtr->fifoPtr, &commandItem);

    (void)pEventData;
    (void)hComponent;
    (void)nData1;
    (void)nData2;
   
    return EB_ErrorNone;
}

/***************************************
* Encoder Event Callback
* This callback is used for error reporting
* or to signal the encoding being done
***************************************/
EB_ERRORTYPE encoderEventCb(
    EB_HANDLETYPE     hComponent,     // Component Handle
    EB_PTR            pAppData,       // Pointer to private data
    EB_U32            nData1,         // Type defined by event
    EB_U32            nData2,         // Type defined by event
    EB_PTR            pEventData)
{
    EbAppContext_t *callbackDataPtr = (EbAppContext_t*)pAppData;
    AppCommandItem_t commandItem;

    pAppData = (EB_PTR)0;

    commandItem.command = APP_ExitError;
    commandItem.errorCode = nData1;
    commandItem.headerPtr = (EB_BUFFERHEADERTYPE*)EB_NULL;
    commandItem.instanceIndex = callbackDataPtr->instanceIdx;
    AppCommandFifoPush(callbackDataPtr->fifoPtr, &commandItem);

    (void)pEventData;
    (void)hComponent;
    (void)nData1;
    (void)nData2;

    return EB_ErrorNone;
}

/***************************************
* Encoder Empty Buffer Done Callback
***************************************/
EB_ERRORTYPE encoderSendPictureDone(
    EB_HANDLETYPE          hComponent,
    EB_PTR                 pAppData,
    EB_BUFFERHEADERTYPE   *pBuffer)
{
    EB_ERRORTYPE   return_error = EB_ErrorNone;
    EbAppContext_t *callbackDataPtr = (EbAppContext_t*)pAppData;
    AppCommandItem_t commandItem;

    // Unused variable
    hComponent = (EB_HANDLETYPE)0;
    (void)hComponent;

    // Configure the command
    commandItem.command = APP_InputEmptyThisBuffer;
    commandItem.headerPtr = pBuffer;
    commandItem.instanceIndex = callbackDataPtr->instanceIdx;
    AppCommandFifoPush(callbackDataPtr->fifoPtr, &commandItem);

    return return_error;
}

/***************************************
* Encoder Fill Buffer Done Callback
***************************************/
EB_ERRORTYPE encoderFillPacketDone(
    EB_HANDLETYPE           hComponent,
    EB_PTR                  pAppData,
    EB_BUFFERHEADERTYPE    *pBuffer)
{
    AppCommandItem_t commandItem;
    EB_ERRORTYPE   return_error = EB_ErrorNone;
    EbAppContext_t *callbackDataPtr = (EbAppContext_t*)pAppData;

    // Unused variable
    hComponent = (EB_HANDLETYPE)0;
    (void)hComponent;

    // Configure the command
    switch (pBuffer->nOutputPortIndex) {
    case EB_ENCODERSTREAMPORT:
        commandItem.command = APP_OutputStreamFillThisBuffer;
        break;
    default:
        commandItem.command = APP_OutputStreamFillThisBuffer;
        break;

    }
    commandItem.headerPtr = pBuffer;
    commandItem.instanceIndex = callbackDataPtr->instanceIdx;

    AppCommandFifoPush(callbackDataPtr->fifoPtr, &commandItem);

    return return_error;
}


/***********************************
 * Initialize Core & Component
 ***********************************/
EB_ERRORTYPE InitEncoder(
    EbConfig_t				*config,
    EbAppContext_t			*callbackData,
	EB_U32					instanceIdx)
{
    EB_CALLBACKTYPE     encoderCallBacks = {encoderFeedbackComplete, encoderEventCb, encoderSendPictureDone, encoderFillPacketDone};
    EB_ERRORTYPE        return_error = EB_ErrorNone;
    unsigned int        bufferIndex;
    
    // Allocate a memory table hosting all allocated pointers
    AllocateMemoryTable(instanceIdx);
    	
    // STEP 1: Call the library to construct a Component Handle
    return_error = EbInitHandle(&callbackData->svtEncoderHandle, callbackData, &encoderCallBacks);

    if (return_error != EB_ErrorNone) {
        return return_error;
    }
    
    // STEP 2: Copy all configuration parameters into the callback structure
    return_error = CopyConfigurationParameters(
                    config,
                    callbackData,
                    instanceIdx);

    if (return_error != EB_ErrorNone) {
        return return_error;
    }
    
    // STEP 3: Send over all configuration parameters
    // Set the Parameters
    return_error = EbH265EncSetParameter(
                       callbackData->svtEncoderHandle,
                       &callbackData->ebEncParameters);

    if (return_error != EB_ErrorNone) {
        return return_error;
    }

    // STEP 4: Allocate input buffers carrying the yuv frames in
    return_error = AllocateInputBuffers(
        config,
        callbackData);

    if (return_error != EB_ErrorNone) {
        return return_error;
    }

    // STEP 5: Allocate output buffers carrying the bitstream out
    return_error = AllocateOutputBuffers(
        config,
        callbackData);

    if (return_error != EB_ErrorNone) {
        return return_error;
    }

	// Allocate the Sequence Buffer
    if (config->bufferedInput != -1) {

        // Preload frames into the ram for a faster yuv access time
        PreloadFramesIntoRam(
            config);
    }
    else {
        config->sequenceBuffer = 0;
    }

    // STEP 8: Init Encoder
    return_error = EbInitEncoder(callbackData->svtEncoderHandle);

    if (return_error != EB_ErrorNone) {
        return return_error;
    }

    
    // STEP 9: Queue the Input Buffers to be populated 
    for(bufferIndex=0; bufferIndex < callbackData->ebEncParameters.inputOutputBufferFifoInitCount; ++bufferIndex) {

        // Tag the input buffers with APP_InputEmptyThisBuffer for the library to empty them
        AppCommandItem_t commandItem;
		commandItem.instanceIndex   = callbackData->instanceIdx;
        commandItem.command         = APP_InputEmptyThisBuffer;
        commandItem.headerPtr       = callbackData->inputBufferPool[bufferIndex];

        AppCommandFifoPush(callbackData->fifoPtr, &commandItem);
    }

    // STEP 10:  Queue the Bitstream Buffers (link the library bitstream pointers to the newly created buffers)
    for(bufferIndex=0; bufferIndex < callbackData->ebEncParameters.inputOutputBufferFifoInitCount; ++bufferIndex) {
         return_error = EbH265EncFillPacket(
                           callbackData->svtEncoderHandle,
                           callbackData->streamBufferPool[bufferIndex]);
    }

    
    return return_error;
}

/***********************************
 * Deinit Components
 ***********************************/
EB_ERRORTYPE DeInitEncoder(
    EbAppContext_t *callbackDataPtr,
    EB_U32          instanceIndex,
    EB_ERRORTYPE   libExitError)
{
    EB_ERRORTYPE return_error = EB_ErrorNone;
    EB_S32              ptrIndex        = 0;
    EbMemoryMapEntry*   memoryEntry     = (EbMemoryMapEntry*)EB_NULL;
    
    if (((EB_COMPONENTTYPE*)(callbackDataPtr->svtEncoderHandle)) != NULL) {
        if (libExitError == EB_ErrorInsufficientResources) {
            return_error = EbStopEncoder(callbackDataPtr->svtEncoderHandle, 0);
        }
        else {
            return_error = EbDeinitEncoder(callbackDataPtr->svtEncoderHandle);
        }
    }

    // Destruct the buffer memory pool
    if (return_error != EB_ErrorNone) {
        return return_error;
    }

    // Loop through the ptr table and free all malloc'd pointers per channel
    for (ptrIndex = appMemoryMapIndexAllChannels[instanceIndex] - 1; ptrIndex >= 0; --ptrIndex) {
        memoryEntry = &appMemoryMapAllChannels[instanceIndex][ptrIndex];
        switch (memoryEntry->ptrType) {
        case EB_N_PTR:
            free(memoryEntry->ptr);
            break;
        default:
            return_error = EB_ErrorMax;
            break;
        }
    }
    free(appMemoryMapAllChannels[instanceIndex]);

    // Destruct the component
    EbDeinitHandle(callbackDataPtr->svtEncoderHandle);

    return return_error;
}

/***********************************
 * Start the Encoder Component
 ***********************************/
EB_ERRORTYPE StartEncoder(
    EbAppContext_t  *callbackDataPtr)
{
    EB_ERRORTYPE return_error = EB_ErrorNone;

    return_error = EbStartEncoder(
        callbackDataPtr->svtEncoderHandle, 0);

    return return_error;
}

/***********************************
 * Stop the Encoder Component
 ***********************************/
EB_ERRORTYPE StopEncoder(
    EbAppContext_t  *callbackDataPtr)
{
    EB_ERRORTYPE return_error = EB_ErrorNone;

    return_error = EbStopEncoder(callbackDataPtr->svtEncoderHandle, 0);

    return return_error;
}