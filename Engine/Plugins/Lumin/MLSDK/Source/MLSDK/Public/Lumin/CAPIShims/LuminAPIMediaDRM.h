// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_drm.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayAllocate)
#define MLMediaDRMByteArrayAllocate ::MLSDK_API::MLMediaDRMByteArrayAllocateShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayAllocAndCopy)
#define MLMediaDRMByteArrayAllocAndCopy ::MLSDK_API::MLMediaDRMByteArrayAllocAndCopyShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMKeyValueArrayAllocate)
#define MLMediaDRMKeyValueArrayAllocate ::MLSDK_API::MLMediaDRMKeyValueArrayAllocateShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMCreate)
#define MLMediaDRMCreate ::MLSDK_API::MLMediaDRMCreateShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRelease)
#define MLMediaDRMRelease ::MLSDK_API::MLMediaDRMReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayRelease)
#define MLMediaDRMByteArrayRelease ::MLSDK_API::MLMediaDRMByteArrayReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMByteArrayListRelease)
#define MLMediaDRMByteArrayListRelease ::MLSDK_API::MLMediaDRMByteArrayListReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMKeyValueArrayRelease)
#define MLMediaDRMKeyValueArrayRelease ::MLSDK_API::MLMediaDRMKeyValueArrayReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRequestMessageRelease)
#define MLMediaDRMRequestMessageRelease ::MLSDK_API::MLMediaDRMRequestMessageReleaseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMKeyValueArrayAdd)
#define MLMediaDRMKeyValueArrayAdd ::MLSDK_API::MLMediaDRMKeyValueArrayAddShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMIsCryptoSchemeSupported)
#define MLMediaDRMIsCryptoSchemeSupported ::MLSDK_API::MLMediaDRMIsCryptoSchemeSupportedShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSetOnEventListener)
#define MLMediaDRMSetOnEventListener ::MLSDK_API::MLMediaDRMSetOnEventListenerShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMOpenSession)
#define MLMediaDRMOpenSession ::MLSDK_API::MLMediaDRMOpenSessionShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMCloseSession)
#define MLMediaDRMCloseSession ::MLSDK_API::MLMediaDRMCloseSessionShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetKeyRequest)
#define MLMediaDRMGetKeyRequest ::MLSDK_API::MLMediaDRMGetKeyRequestShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMProvideKeyResponse)
#define MLMediaDRMProvideKeyResponse ::MLSDK_API::MLMediaDRMProvideKeyResponseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRestoreKeys)
#define MLMediaDRMRestoreKeys ::MLSDK_API::MLMediaDRMRestoreKeysShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMRemoveKeys)
#define MLMediaDRMRemoveKeys ::MLSDK_API::MLMediaDRMRemoveKeysShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMQueryKeyStatus)
#define MLMediaDRMQueryKeyStatus ::MLSDK_API::MLMediaDRMQueryKeyStatusShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetProvisionRequest)
#define MLMediaDRMGetProvisionRequest ::MLSDK_API::MLMediaDRMGetProvisionRequestShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMProvideProvisionResponse)
#define MLMediaDRMProvideProvisionResponse ::MLSDK_API::MLMediaDRMProvideProvisionResponseShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetSecureStops)
#define MLMediaDRMGetSecureStops ::MLSDK_API::MLMediaDRMGetSecureStopsShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetSecureStop)
#define MLMediaDRMGetSecureStop ::MLSDK_API::MLMediaDRMGetSecureStopShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMReleaseSecureStops)
#define MLMediaDRMReleaseSecureStops ::MLSDK_API::MLMediaDRMReleaseSecureStopsShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMReleaseAllSecureStops)
#define MLMediaDRMReleaseAllSecureStops ::MLSDK_API::MLMediaDRMReleaseAllSecureStopsShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetPropertyString)
#define MLMediaDRMGetPropertyString ::MLSDK_API::MLMediaDRMGetPropertyStringShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMGetPropertyByteArray)
#define MLMediaDRMGetPropertyByteArray ::MLSDK_API::MLMediaDRMGetPropertyByteArrayShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSetPropertyString)
#define MLMediaDRMSetPropertyString ::MLSDK_API::MLMediaDRMSetPropertyStringShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSetPropertyByteArray)
#define MLMediaDRMSetPropertyByteArray ::MLSDK_API::MLMediaDRMSetPropertyByteArrayShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMEncrypt)
#define MLMediaDRMEncrypt ::MLSDK_API::MLMediaDRMEncryptShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMDecrypt)
#define MLMediaDRMDecrypt ::MLSDK_API::MLMediaDRMDecryptShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSign)
#define MLMediaDRMSign ::MLSDK_API::MLMediaDRMSignShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMVerify)
#define MLMediaDRMVerify ::MLSDK_API::MLMediaDRMVerifyShim
CREATE_FUNCTION_SHIM(ml_mediadrm, MLResult, MLMediaDRMSignRSA)
#define MLMediaDRMSignRSA ::MLSDK_API::MLMediaDRMSignRSAShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
