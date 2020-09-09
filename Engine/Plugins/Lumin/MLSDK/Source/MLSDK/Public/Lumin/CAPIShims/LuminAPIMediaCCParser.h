// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_cea608_caption.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserCreate)
#define MLMediaCCParserCreate ::LUMIN_MLSDK_API::MLMediaCCParserCreateShim
CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserGetDisplayable)
#define MLMediaCCParserGetDisplayable ::LUMIN_MLSDK_API::MLMediaCCParserGetDisplayableShim
CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserReleaseSegment)
#define MLMediaCCParserReleaseSegment ::LUMIN_MLSDK_API::MLMediaCCParserReleaseSegmentShim
CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserDestroy)
#define MLMediaCCParserDestroy ::LUMIN_MLSDK_API::MLMediaCCParserDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
