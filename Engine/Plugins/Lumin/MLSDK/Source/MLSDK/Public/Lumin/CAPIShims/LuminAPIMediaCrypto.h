// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_crypto.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoCreate)
#define MLMediaCryptoCreate ::LUMIN_MLSDK_API::MLMediaCryptoCreateShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoRelease)
#define MLMediaCryptoRelease ::LUMIN_MLSDK_API::MLMediaCryptoReleaseShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoIsCryptoSchemeSupported)
#define MLMediaCryptoIsCryptoSchemeSupported ::LUMIN_MLSDK_API::MLMediaCryptoIsCryptoSchemeSupportedShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoRequiresSecureDecoderComponent)
#define MLMediaCryptoRequiresSecureDecoderComponent ::LUMIN_MLSDK_API::MLMediaCryptoRequiresSecureDecoderComponentShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoSetMediaDRMSession)
#define MLMediaCryptoSetMediaDRMSession ::LUMIN_MLSDK_API::MLMediaCryptoSetMediaDRMSessionShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
