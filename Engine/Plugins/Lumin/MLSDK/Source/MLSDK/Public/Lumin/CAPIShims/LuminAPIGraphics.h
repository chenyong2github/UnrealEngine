// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Lumin/CAPIShims/LuminAPI.h"

#if !PLATFORM_MAC && defined(MLSDK_API_USE_VULKAN) && MLSDK_API_USE_VULKAN
// Need all this for: class FVulkanTexture2D, and other Vulkan types.
#include "VulkanRHIPrivate.h"
#endif // !PLATFORM_MAC && defined(MLSDK_API_USE_VULKAN) && MLSDK_API_USE_VULKAN

#if !defined(WITH_MLSDK) || WITH_MLSDK

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_graphics.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsEnableBlobCacheGL)
#define MLGraphicsEnableBlobCacheGL ::MLSDK_API::MLGraphicsEnableBlobCacheGLShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsCreateClientGL)
#define MLGraphicsCreateClientGL ::MLSDK_API::MLGraphicsCreateClientGLShim
CREATE_FUNCTION_SHIM(ml_graphics, uint32_t, MLGraphicsGLFormatFromMLSurfaceFormat)
#define MLGraphicsGLFormatFromMLSurfaceFormat ::MLSDK_API::MLGraphicsGLFormatFromMLSurfaceFormatShim
CREATE_FUNCTION_SHIM(ml_graphics, MLSurfaceFormat, MLGraphicsMLSurfaceFormatFromGLFormat)
#define MLGraphicsMLSurfaceFormatFromGLFormat ::MLSDK_API::MLGraphicsMLSurfaceFormatFromGLFormatShim
#ifdef VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsCreateClientVk)
#define MLGraphicsCreateClientVk ::MLSDK_API::MLGraphicsCreateClientVkShim
CREATE_FUNCTION_SHIM(ml_graphics, VkFormat, MLGraphicsVkFormatFromMLSurfaceFormat)
#define MLGraphicsVkFormatFromMLSurfaceFormat ::MLSDK_API::MLGraphicsVkFormatFromMLSurfaceFormatShim
CREATE_FUNCTION_SHIM(ml_graphics, MLSurfaceFormat, MLGraphicsMLSurfaceFormatFromVkFormat)
#define MLGraphicsMLSurfaceFormatFromVkFormat ::MLSDK_API::MLGraphicsMLSurfaceFormatFromVkFormatShim
#endif // VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsDestroyClient)
#define MLGraphicsDestroyClient ::MLSDK_API::MLGraphicsDestroyClientShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsSetFrameTimingHint)
#define MLGraphicsSetFrameTimingHint ::MLSDK_API::MLGraphicsSetFrameTimingHintShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsInitFrameParams)
#define MLGraphicsInitFrameParams ::MLSDK_API::MLGraphicsInitFrameParamsShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsBeginFrame)
#define MLGraphicsBeginFrame ::MLSDK_API::MLGraphicsBeginFrameShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsBeginFrameEx)
#define MLGraphicsBeginFrameEx ::MLSDK_API::MLGraphicsBeginFrameExShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsSignalSyncObjectGL)
#define MLGraphicsSignalSyncObjectGL ::MLSDK_API::MLGraphicsSignalSyncObjectGLShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetClipExtents)
#define MLGraphicsGetClipExtents ::MLSDK_API::MLGraphicsGetClipExtentsShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetClipExtentsEx)
#define MLGraphicsGetClipExtentsEx ::MLSDK_API::MLGraphicsGetClipExtentsExShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetRenderTargets)
#define MLGraphicsGetRenderTargets ::MLSDK_API::MLGraphicsGetRenderTargetsShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsEndFrame)
#define MLGraphicsEndFrame ::MLSDK_API::MLGraphicsEndFrameShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetClientPerformanceInfo)
#define MLGraphicsGetClientPerformanceInfo ::MLSDK_API::MLGraphicsGetClientPerformanceInfoShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
