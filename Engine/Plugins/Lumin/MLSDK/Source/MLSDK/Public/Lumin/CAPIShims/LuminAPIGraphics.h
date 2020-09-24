// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Lumin/CAPIShims/LuminAPI.h"

#define PLATFORM_SUPPORTS_VULKAN (PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_LUMIN)

#if PLATFORM_SUPPORTS_VULKAN && defined(MLSDK_API_USE_VULKAN) && MLSDK_API_USE_VULKAN
// Need all this for: class FVulkanTexture2D, and other Vulkan types.
#include "VulkanRHIPrivate.h"
#endif // PLATFORM_SUPPORTS_VULKAN && defined(MLSDK_API_USE_VULKAN) && MLSDK_API_USE_VULKAN

#if !defined(WITH_MLSDK) || WITH_MLSDK

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_graphics.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsEnableBlobCacheGL)
#define MLGraphicsEnableBlobCacheGL ::LUMIN_MLSDK_API::MLGraphicsEnableBlobCacheGLShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsCreateClientGL)
#define MLGraphicsCreateClientGL ::LUMIN_MLSDK_API::MLGraphicsCreateClientGLShim
CREATE_FUNCTION_SHIM(ml_graphics, uint32_t, MLGraphicsGLFormatFromMLSurfaceFormat)
#define MLGraphicsGLFormatFromMLSurfaceFormat ::LUMIN_MLSDK_API::MLGraphicsGLFormatFromMLSurfaceFormatShim
CREATE_FUNCTION_SHIM(ml_graphics, MLSurfaceFormat, MLGraphicsMLSurfaceFormatFromGLFormat)
#define MLGraphicsMLSurfaceFormatFromGLFormat ::LUMIN_MLSDK_API::MLGraphicsMLSurfaceFormatFromGLFormatShim
#ifdef VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsCreateClientVk)
#define MLGraphicsCreateClientVk ::LUMIN_MLSDK_API::MLGraphicsCreateClientVkShim
CREATE_FUNCTION_SHIM(ml_graphics, VkFormat, MLGraphicsVkFormatFromMLSurfaceFormat)
#define MLGraphicsVkFormatFromMLSurfaceFormat ::LUMIN_MLSDK_API::MLGraphicsVkFormatFromMLSurfaceFormatShim
CREATE_FUNCTION_SHIM(ml_graphics, MLSurfaceFormat, MLGraphicsMLSurfaceFormatFromVkFormat)
#define MLGraphicsMLSurfaceFormatFromVkFormat ::LUMIN_MLSDK_API::MLGraphicsMLSurfaceFormatFromVkFormatShim
#endif // VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsDestroyClient)
#define MLGraphicsDestroyClient ::LUMIN_MLSDK_API::MLGraphicsDestroyClientShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsSetFrameTimingHint)
#define MLGraphicsSetFrameTimingHint ::LUMIN_MLSDK_API::MLGraphicsSetFrameTimingHintShim
CREATE_DEPRECATED_SHIM(ml_graphics, MLResult, MLGraphicsInitFrameParams)
#define MLGraphicsInitFrameParams ::LUMIN_MLSDK_API::MLGraphicsInitFrameParamsShim
CREATE_DEPRECATED_MSG_SHIM(ml_graphics, MLResult, MLGraphicsBeginFrame, "Replaced by MLGraphicsBeginFrameEx.")
#define MLGraphicsBeginFrame ::LUMIN_MLSDK_API::MLGraphicsBeginFrameShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsBeginFrameEx)
#define MLGraphicsBeginFrameEx ::LUMIN_MLSDK_API::MLGraphicsBeginFrameExShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsSignalSyncObjectGL)
#define MLGraphicsSignalSyncObjectGL ::LUMIN_MLSDK_API::MLGraphicsSignalSyncObjectGLShim
CREATE_DEPRECATED_MSG_SHIM(ml_graphics, MLResult, MLGraphicsGetClipExtents, "Replaced by MLGraphicsGetClipExtentsEx.")
#define MLGraphicsGetClipExtents ::LUMIN_MLSDK_API::MLGraphicsGetClipExtentsShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetClipExtentsEx)
#define MLGraphicsGetClipExtentsEx ::LUMIN_MLSDK_API::MLGraphicsGetClipExtentsExShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetRenderTargets)
#define MLGraphicsGetRenderTargets ::LUMIN_MLSDK_API::MLGraphicsGetRenderTargetsShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsEndFrame)
#define MLGraphicsEndFrame ::LUMIN_MLSDK_API::MLGraphicsEndFrameShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLGraphicsGetClientPerformanceInfo)
#define MLGraphicsGetClientPerformanceInfo ::LUMIN_MLSDK_API::MLGraphicsGetClientPerformanceInfoShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
