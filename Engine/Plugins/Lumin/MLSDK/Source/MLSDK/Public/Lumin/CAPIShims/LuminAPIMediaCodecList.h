// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_codeclist.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListCountCodecs)
#define MLMediaCodecListCountCodecs ::MLSDK_API::MLMediaCodecListCountCodecsShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListIsSoftwareCodec)
#define MLMediaCodecListIsSoftwareCodec ::MLSDK_API::MLMediaCodecListIsSoftwareCodecShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetMatchingCodecs)
#define MLMediaCodecListGetMatchingCodecs ::MLSDK_API::MLMediaCodecListGetMatchingCodecsShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetCodecByType)
#define MLMediaCodecListGetCodecByType ::MLSDK_API::MLMediaCodecListGetCodecByTypeShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetCodecByName)
#define MLMediaCodecListGetCodecByName ::MLSDK_API::MLMediaCodecListGetCodecByNameShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListIsEncoder)
#define MLMediaCodecListIsEncoder ::MLSDK_API::MLMediaCodecListIsEncoderShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetCodecName)
#define MLMediaCodecListGetCodecName ::MLSDK_API::MLMediaCodecListGetCodecNameShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetSupportedMimes)
#define MLMediaCodecListGetSupportedMimes ::MLSDK_API::MLMediaCodecListGetSupportedMimesShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetCapabilityFlag)
#define MLMediaCodecListGetCapabilityFlag ::MLSDK_API::MLMediaCodecListGetCapabilityFlagShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListGetSecureCodecName)
#define MLMediaCodecListGetSecureCodecName ::MLSDK_API::MLMediaCodecListGetSecureCodecNameShim
CREATE_FUNCTION_SHIM(ml_mediacodeclist, MLResult, MLMediaCodecListQueryResultsRelease)
#define MLMediaCodecListQueryResultsRelease ::MLSDK_API::MLMediaCodecListQueryResultsReleaseShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
