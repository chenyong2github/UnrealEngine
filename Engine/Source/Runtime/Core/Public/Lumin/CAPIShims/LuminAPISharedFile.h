// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_sharedfile.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileRead)
#define MLSharedFileRead ::MLSDK_API::MLSharedFileReadShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileWrite)
#define MLSharedFileWrite ::MLSDK_API::MLSharedFileWriteShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileListAccessibleFiles)
#define MLSharedFileListAccessibleFiles ::MLSDK_API::MLSharedFileListAccessibleFilesShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFilePick)
#define MLSharedFilePick ::MLSDK_API::MLSharedFilePickShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileGetListLength)
#define MLSharedFileGetListLength ::MLSDK_API::MLSharedFileGetListLengthShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileGetMLFileInfoByIndex)
#define MLSharedFileGetMLFileInfoByIndex ::MLSDK_API::MLSharedFileGetMLFileInfoByIndexShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileGetErrorCode)
#define MLSharedFileGetErrorCode ::MLSDK_API::MLSharedFileGetErrorCodeShim
CREATE_FUNCTION_SHIM(ml_sharedfile, MLResult, MLSharedFileListRelease)
#define MLSharedFileListRelease ::MLSDK_API::MLSharedFileListReleaseShim
CREATE_FUNCTION_SHIM(ml_sharedfile, const char*, MLSharedFileGetResultString)
#define MLSharedFileGetResultString ::MLSDK_API::MLSharedFileGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
