// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_cea608_caption.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserCreate)
#define MLMediaCCParserCreate ::MLSDK_API::MLMediaCCParserCreateShim
CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserGetDisplayable)
#define MLMediaCCParserGetDisplayable ::MLSDK_API::MLMediaCCParserGetDisplayableShim
CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserReleaseSegment)
#define MLMediaCCParserReleaseSegment ::MLSDK_API::MLMediaCCParserReleaseSegmentShim
CREATE_FUNCTION_SHIM(ml_media_ccparser, MLResult, MLMediaCCParserDestroy)
#define MLMediaCCParserDestroy ::MLSDK_API::MLMediaCCParserDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
