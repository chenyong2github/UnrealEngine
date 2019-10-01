// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_secure_storage.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_secure_storage, MLResult, MLSecureStoragePutBlob)
#define MLSecureStoragePutBlob ::MLSDK_API::MLSecureStoragePutBlobShim
CREATE_FUNCTION_SHIM(ml_secure_storage, MLResult, MLSecureStorageGetBlob)
#define MLSecureStorageGetBlob ::MLSDK_API::MLSecureStorageGetBlobShim
CREATE_FUNCTION_SHIM(ml_secure_storage, MLResult, MLSecureStorageDeleteBlob)
#define MLSecureStorageDeleteBlob ::MLSDK_API::MLSecureStorageDeleteBlobShim
CREATE_FUNCTION_SHIM(ml_secure_storage, void, MLSecureStorageFreeBlobBuffer)
#define MLSecureStorageFreeBlobBuffer ::MLSDK_API::MLSecureStorageFreeBlobBufferShim
CREATE_FUNCTION_SHIM(ml_secure_storage, const char*, MLSecureStorageGetResultString)
#define MLSecureStorageGetResultString ::MLSDK_API::MLSecureStorageGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
