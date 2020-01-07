// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_dispatch.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchAllocateEmptyPacket)
#define MLDispatchAllocateEmptyPacket ::MLSDK_API::MLDispatchAllocateEmptyPacketShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchReleasePacket)
#define MLDispatchReleasePacket ::MLSDK_API::MLDispatchReleasePacketShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchAllocateFileInfoList)
#define MLDispatchAllocateFileInfoList ::MLSDK_API::MLDispatchAllocateFileInfoListShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchGetFileInfoListLength)
#define MLDispatchGetFileInfoListLength ::MLSDK_API::MLDispatchGetFileInfoListLengthShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchGetFileInfoByIndex)
#define MLDispatchGetFileInfoByIndex ::MLSDK_API::MLDispatchGetFileInfoByIndexShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchAddFileInfo)
#define MLDispatchAddFileInfo ::MLSDK_API::MLDispatchAddFileInfoShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchReleaseFileInfoList)
#define MLDispatchReleaseFileInfoList ::MLSDK_API::MLDispatchReleaseFileInfoListShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchSetUri)
#define MLDispatchSetUri ::MLSDK_API::MLDispatchSetUriShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchReleaseUri)
#define MLDispatchReleaseUri ::MLSDK_API::MLDispatchReleaseUriShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchTryOpenApplication)
#define MLDispatchTryOpenApplication ::MLSDK_API::MLDispatchTryOpenApplicationShim
CREATE_FUNCTION_SHIM(ml_dispatch, const char*, MLDispatchGetResultString)
#define MLDispatchGetResultString ::MLSDK_API::MLDispatchGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
