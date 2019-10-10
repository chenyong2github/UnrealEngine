// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_codec.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecCreateCodec)
#define MLMediaCodecCreateCodec ::MLSDK_API::MLMediaCodecCreateCodecShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecDestroy)
#define MLMediaCodecDestroy ::MLSDK_API::MLMediaCodecDestroyShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecSetCallbacks)
#define MLMediaCodecSetCallbacks ::MLSDK_API::MLMediaCodecSetCallbacksShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetName)
#define MLMediaCodecGetName ::MLSDK_API::MLMediaCodecGetNameShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecSetSurfaceHint)
#define MLMediaCodecSetSurfaceHint ::MLSDK_API::MLMediaCodecSetSurfaceHintShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecConfigure)
#define MLMediaCodecConfigure ::MLSDK_API::MLMediaCodecConfigureShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecStart)
#define MLMediaCodecStart ::MLSDK_API::MLMediaCodecStartShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecStop)
#define MLMediaCodecStop ::MLSDK_API::MLMediaCodecStopShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecFlush)
#define MLMediaCodecFlush ::MLSDK_API::MLMediaCodecFlushShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetInputBufferPointer)
#define MLMediaCodecGetInputBufferPointer ::MLSDK_API::MLMediaCodecGetInputBufferPointerShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetOutputBufferPointer)
#define MLMediaCodecGetOutputBufferPointer ::MLSDK_API::MLMediaCodecGetOutputBufferPointerShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecQueueInputBuffer)
#define MLMediaCodecQueueInputBuffer ::MLSDK_API::MLMediaCodecQueueInputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecQueueSecureInputBuffer)
#define MLMediaCodecQueueSecureInputBuffer ::MLSDK_API::MLMediaCodecQueueSecureInputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecDequeueInputBuffer)
#define MLMediaCodecDequeueInputBuffer ::MLSDK_API::MLMediaCodecDequeueInputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecDequeueOutputBuffer)
#define MLMediaCodecDequeueOutputBuffer ::MLSDK_API::MLMediaCodecDequeueOutputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetInputFormat)
#define MLMediaCodecGetInputFormat ::MLSDK_API::MLMediaCodecGetInputFormatShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetOutputFormat)
#define MLMediaCodecGetOutputFormat ::MLSDK_API::MLMediaCodecGetOutputFormatShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecReleaseOutputBuffer)
#define MLMediaCodecReleaseOutputBuffer ::MLSDK_API::MLMediaCodecReleaseOutputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecReleaseOutputBufferAtTime)
#define MLMediaCodecReleaseOutputBufferAtTime ::MLSDK_API::MLMediaCodecReleaseOutputBufferAtTimeShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecAcquireNextAvailableFrame)
#define MLMediaCodecAcquireNextAvailableFrame ::MLSDK_API::MLMediaCodecAcquireNextAvailableFrameShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecReleaseFrame)
#define MLMediaCodecReleaseFrame ::MLSDK_API::MLMediaCodecReleaseFrameShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameTransformationMatrix)
#define MLMediaCodecGetFrameTransformationMatrix ::MLSDK_API::MLMediaCodecGetFrameTransformationMatrixShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameTimestamp)
#define MLMediaCodecGetFrameTimestamp ::MLSDK_API::MLMediaCodecGetFrameTimestampShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameQueueBufferTimestamp)
#define MLMediaCodecGetFrameQueueBufferTimestamp ::MLSDK_API::MLMediaCodecGetFrameQueueBufferTimestampShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameNumber)
#define MLMediaCodecGetFrameNumber ::MLSDK_API::MLMediaCodecGetFrameNumberShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
