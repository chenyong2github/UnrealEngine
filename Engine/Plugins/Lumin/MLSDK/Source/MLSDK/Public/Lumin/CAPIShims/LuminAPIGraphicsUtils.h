// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"
#include "LuminAPIGraphics.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_graphics_utils.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

#ifdef VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_graphics_utils, MLResult, MLGraphicsEnumerateRequiredVkDeviceExtensionsForMediaHandleImport)
#define MLGraphicsEnumerateRequiredVkDeviceExtensionsForMediaHandleImport ::LUMIN_MLSDK_API::MLGraphicsEnumerateRequiredVkDeviceExtensionsForMediaHandleImportShim
CREATE_FUNCTION_SHIM(ml_graphics_utils, MLResult, MLGraphicsImportVkImageFromMediaHandle)
#define MLGraphicsImportVkImageFromMediaHandle ::LUMIN_MLSDK_API::MLGraphicsImportVkImageFromMediaHandleShim
#endif // VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_graphics_utils, MLResult, MLMeshingPopulateDepth)
#define MLMeshingPopulateDepth ::LUMIN_MLSDK_API::MLMeshingPopulateDepthShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
