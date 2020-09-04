// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_secure_storage.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_secure_storage, MLResult, MLSecureStoragePutBlob)
#define MLSecureStoragePutBlob ::LUMIN_MLSDK_API::MLSecureStoragePutBlobShim
CREATE_FUNCTION_SHIM(ml_secure_storage, MLResult, MLSecureStorageGetBlob)
#define MLSecureStorageGetBlob ::LUMIN_MLSDK_API::MLSecureStorageGetBlobShim
CREATE_FUNCTION_SHIM(ml_secure_storage, MLResult, MLSecureStorageDeleteBlob)
#define MLSecureStorageDeleteBlob ::LUMIN_MLSDK_API::MLSecureStorageDeleteBlobShim
CREATE_FUNCTION_SHIM(ml_secure_storage, void, MLSecureStorageFreeBlobBuffer)
#define MLSecureStorageFreeBlobBuffer ::LUMIN_MLSDK_API::MLSecureStorageFreeBlobBufferShim
CREATE_FUNCTION_SHIM(ml_secure_storage, const char*, MLSecureStorageGetResultString)
#define MLSecureStorageGetResultString ::LUMIN_MLSDK_API::MLSecureStorageGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
