// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_codec.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecCreateCodec)
#define MLMediaCodecCreateCodec ::LUMIN_MLSDK_API::MLMediaCodecCreateCodecShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecDestroy)
#define MLMediaCodecDestroy ::LUMIN_MLSDK_API::MLMediaCodecDestroyShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecSetCallbacks)
#define MLMediaCodecSetCallbacks ::LUMIN_MLSDK_API::MLMediaCodecSetCallbacksShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetName)
#define MLMediaCodecGetName ::LUMIN_MLSDK_API::MLMediaCodecGetNameShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecSetSurfaceHint)
#define MLMediaCodecSetSurfaceHint ::LUMIN_MLSDK_API::MLMediaCodecSetSurfaceHintShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecConfigure)
#define MLMediaCodecConfigure ::LUMIN_MLSDK_API::MLMediaCodecConfigureShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecConfigureWithSurface)
#define MLMediaCodecConfigureWithSurface ::LUMIN_MLSDK_API::MLMediaCodecConfigureWithSurfaceShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecStart)
#define MLMediaCodecStart ::LUMIN_MLSDK_API::MLMediaCodecStartShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecStop)
#define MLMediaCodecStop ::LUMIN_MLSDK_API::MLMediaCodecStopShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecFlush)
#define MLMediaCodecFlush ::LUMIN_MLSDK_API::MLMediaCodecFlushShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetInputBufferPointer)
#define MLMediaCodecGetInputBufferPointer ::LUMIN_MLSDK_API::MLMediaCodecGetInputBufferPointerShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetOutputBufferPointer)
#define MLMediaCodecGetOutputBufferPointer ::LUMIN_MLSDK_API::MLMediaCodecGetOutputBufferPointerShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecQueueInputBuffer)
#define MLMediaCodecQueueInputBuffer ::LUMIN_MLSDK_API::MLMediaCodecQueueInputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecQueueSecureInputBuffer)
#define MLMediaCodecQueueSecureInputBuffer ::LUMIN_MLSDK_API::MLMediaCodecQueueSecureInputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecDequeueInputBuffer)
#define MLMediaCodecDequeueInputBuffer ::LUMIN_MLSDK_API::MLMediaCodecDequeueInputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecDequeueOutputBuffer)
#define MLMediaCodecDequeueOutputBuffer ::LUMIN_MLSDK_API::MLMediaCodecDequeueOutputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetInputFormat)
#define MLMediaCodecGetInputFormat ::LUMIN_MLSDK_API::MLMediaCodecGetInputFormatShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetOutputFormat)
#define MLMediaCodecGetOutputFormat ::LUMIN_MLSDK_API::MLMediaCodecGetOutputFormatShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecReleaseOutputBuffer)
#define MLMediaCodecReleaseOutputBuffer ::LUMIN_MLSDK_API::MLMediaCodecReleaseOutputBufferShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecReleaseOutputBufferAtTime)
#define MLMediaCodecReleaseOutputBufferAtTime ::LUMIN_MLSDK_API::MLMediaCodecReleaseOutputBufferAtTimeShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecAcquireNextAvailableFrame)
#define MLMediaCodecAcquireNextAvailableFrame ::LUMIN_MLSDK_API::MLMediaCodecAcquireNextAvailableFrameShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecReleaseFrame)
#define MLMediaCodecReleaseFrame ::LUMIN_MLSDK_API::MLMediaCodecReleaseFrameShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameTransformationMatrix)
#define MLMediaCodecGetFrameTransformationMatrix ::LUMIN_MLSDK_API::MLMediaCodecGetFrameTransformationMatrixShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameTimestamp)
#define MLMediaCodecGetFrameTimestamp ::LUMIN_MLSDK_API::MLMediaCodecGetFrameTimestampShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameQueueBufferTimestamp)
#define MLMediaCodecGetFrameQueueBufferTimestamp ::LUMIN_MLSDK_API::MLMediaCodecGetFrameQueueBufferTimestampShim
CREATE_FUNCTION_SHIM(ml_mediacodec, MLResult, MLMediaCodecGetFrameNumber)
#define MLMediaCodecGetFrameNumber ::LUMIN_MLSDK_API::MLMediaCodecGetFrameNumberShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
