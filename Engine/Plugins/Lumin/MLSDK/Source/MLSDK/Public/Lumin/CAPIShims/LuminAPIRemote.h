// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"
#include "LuminAPIGraphics.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_remote.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_remote, MLResult, MLRemoteIsServerConfigured)
#define MLRemoteIsServerConfigured ::LUMIN_MLSDK_API::MLRemoteIsServerConfiguredShim
#ifdef VK_VERSION_1_0
CREATE_FUNCTION_SHIM(ml_remote, MLResult, MLRemoteEnumerateRequiredVkInstanceExtensions)
#define MLRemoteEnumerateRequiredVkInstanceExtensions ::LUMIN_MLSDK_API::MLRemoteEnumerateRequiredVkInstanceExtensionsShim
CREATE_FUNCTION_SHIM(ml_remote, MLResult, MLRemoteEnumerateRequiredVkDeviceExtensions)
#define MLRemoteEnumerateRequiredVkDeviceExtensions ::LUMIN_MLSDK_API::MLRemoteEnumerateRequiredVkDeviceExtensionsShim
#endif // VK_VERSION_1_0

// While these functions live in the ml_graphics library, they are defined in the ml_remote header
// and thus should be defined in LuminAPIRemote.h to avoid a circular header include dependency.
#if PLATFORM_MAC
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLRemoteGraphicsCreateClientMTL)
#define MLRemoteGraphicsCreateClientMTL ::LUMIN_MLSDK_API::MLRemoteGraphicsCreateClientMTLShim
CREATE_FUNCTION_SHIM(ml_graphics, MTLPixelFormat, MLRemoteGraphicsMTLFormatFromMLSurfaceFormat)
#define MLRemoteGraphicsMTLFormatFromMLSurfaceFormat ::LUMIN_MLSDK_API::MLRemoteGraphicsMTLFormatFromMLSurfaceFormatShim
CREATE_FUNCTION_SHIM(ml_graphics, MLSurfaceFormat, MLRemoteGraphicsMLSurfaceFormatFromMTLFormat)
#define MLRemoteGraphicsMLSurfaceFormatFromMTLFormat ::LUMIN_MLSDK_API::MLRemoteGraphicsMLSurfaceFormatFromMTLFormatShim
CREATE_FUNCTION_SHIM(ml_graphics, MLResult, MLRemoteGraphicsSignalSyncObjectMTL)
#define MLRemoteGraphicsSignalSyncObjectMTL ::LUMIN_MLSDK_API::MLRemoteGraphicsSignalSyncObjectMTLShim
#endif // PLATFORM_MAC

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
