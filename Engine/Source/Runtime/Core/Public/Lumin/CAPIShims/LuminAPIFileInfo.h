// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_fileinfo.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoAllocateEmpty)
#define MLFileInfoAllocateEmpty ::MLSDK_API::MLFileInfoAllocateEmptyShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoGetMimeType)
#define MLFileInfoGetMimeType ::MLSDK_API::MLFileInfoGetMimeTypeShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoGetFileName)
#define MLFileInfoGetFileName ::MLSDK_API::MLFileInfoGetFileNameShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoGetFD)
#define MLFileInfoGetFD ::MLSDK_API::MLFileInfoGetFDShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoSetFD)
#define MLFileInfoSetFD ::MLSDK_API::MLFileInfoSetFDShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoSetFileName)
#define MLFileInfoSetFileName ::MLSDK_API::MLFileInfoSetFileNameShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoSetMimeType)
#define MLFileInfoSetMimeType ::MLSDK_API::MLFileInfoSetMimeTypeShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoRelease)
#define MLFileInfoRelease ::MLSDK_API::MLFileInfoReleaseShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
