// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_drm.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayAllocate)
#define MLMediaDRMByteArrayAllocate ::LUMIN_MLSDK_API::MLMediaDRMByteArrayAllocateShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayAllocAndCopy)
#define MLMediaDRMByteArrayAllocAndCopy ::LUMIN_MLSDK_API::MLMediaDRMByteArrayAllocAndCopyShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMKeyValueArrayAllocate)
#define MLMediaDRMKeyValueArrayAllocate ::LUMIN_MLSDK_API::MLMediaDRMKeyValueArrayAllocateShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMCreate)
#define MLMediaDRMCreate ::LUMIN_MLSDK_API::MLMediaDRMCreateShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRelease)
#define MLMediaDRMRelease ::LUMIN_MLSDK_API::MLMediaDRMReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayRelease)
#define MLMediaDRMByteArrayRelease ::LUMIN_MLSDK_API::MLMediaDRMByteArrayReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayListRelease)
#define MLMediaDRMByteArrayListRelease ::LUMIN_MLSDK_API::MLMediaDRMByteArrayListReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMKeyValueArrayRelease)
#define MLMediaDRMKeyValueArrayRelease ::LUMIN_MLSDK_API::MLMediaDRMKeyValueArrayReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRequestMessageRelease)
#define MLMediaDRMRequestMessageRelease ::LUMIN_MLSDK_API::MLMediaDRMRequestMessageReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMKeyValueArrayAdd)
#define MLMediaDRMKeyValueArrayAdd ::LUMIN_MLSDK_API::MLMediaDRMKeyValueArrayAddShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMIsCryptoSchemeSupported)
#define MLMediaDRMIsCryptoSchemeSupported ::LUMIN_MLSDK_API::MLMediaDRMIsCryptoSchemeSupportedShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSetOnEventListener)
#define MLMediaDRMSetOnEventListener ::LUMIN_MLSDK_API::MLMediaDRMSetOnEventListenerShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMOpenSession)
#define MLMediaDRMOpenSession ::LUMIN_MLSDK_API::MLMediaDRMOpenSessionShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMCloseSession)
#define MLMediaDRMCloseSession ::LUMIN_MLSDK_API::MLMediaDRMCloseSessionShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetKeyRequest)
#define MLMediaDRMGetKeyRequest ::LUMIN_MLSDK_API::MLMediaDRMGetKeyRequestShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMProvideKeyResponse)
#define MLMediaDRMProvideKeyResponse ::LUMIN_MLSDK_API::MLMediaDRMProvideKeyResponseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRestoreKeys)
#define MLMediaDRMRestoreKeys ::LUMIN_MLSDK_API::MLMediaDRMRestoreKeysShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRemoveKeys)
#define MLMediaDRMRemoveKeys ::LUMIN_MLSDK_API::MLMediaDRMRemoveKeysShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMQueryKeyStatus)
#define MLMediaDRMQueryKeyStatus ::LUMIN_MLSDK_API::MLMediaDRMQueryKeyStatusShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetProvisionRequest)
#define MLMediaDRMGetProvisionRequest ::LUMIN_MLSDK_API::MLMediaDRMGetProvisionRequestShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMProvideProvisionResponse)
#define MLMediaDRMProvideProvisionResponse ::LUMIN_MLSDK_API::MLMediaDRMProvideProvisionResponseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetSecureStops)
#define MLMediaDRMGetSecureStops ::LUMIN_MLSDK_API::MLMediaDRMGetSecureStopsShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetSecureStop)
#define MLMediaDRMGetSecureStop ::LUMIN_MLSDK_API::MLMediaDRMGetSecureStopShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMReleaseSecureStops)
#define MLMediaDRMReleaseSecureStops ::LUMIN_MLSDK_API::MLMediaDRMReleaseSecureStopsShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMReleaseAllSecureStops)
#define MLMediaDRMReleaseAllSecureStops ::LUMIN_MLSDK_API::MLMediaDRMReleaseAllSecureStopsShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetPropertyString)
#define MLMediaDRMGetPropertyString ::LUMIN_MLSDK_API::MLMediaDRMGetPropertyStringShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetPropertyByteArray)
#define MLMediaDRMGetPropertyByteArray ::LUMIN_MLSDK_API::MLMediaDRMGetPropertyByteArrayShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSetPropertyString)
#define MLMediaDRMSetPropertyString ::LUMIN_MLSDK_API::MLMediaDRMSetPropertyStringShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSetPropertyByteArray)
#define MLMediaDRMSetPropertyByteArray ::LUMIN_MLSDK_API::MLMediaDRMSetPropertyByteArrayShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMEncrypt)
#define MLMediaDRMEncrypt ::LUMIN_MLSDK_API::MLMediaDRMEncryptShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMDecrypt)
#define MLMediaDRMDecrypt ::LUMIN_MLSDK_API::MLMediaDRMDecryptShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSign)
#define MLMediaDRMSign ::LUMIN_MLSDK_API::MLMediaDRMSignShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMVerify)
#define MLMediaDRMVerify ::LUMIN_MLSDK_API::MLMediaDRMVerifyShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSignRSA)
#define MLMediaDRMSignRSA ::LUMIN_MLSDK_API::MLMediaDRMSignRSAShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
