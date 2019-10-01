// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_crypto.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoCreate)
#define MLMediaCryptoCreate ::MLSDK_API::MLMediaCryptoCreateShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoRelease)
#define MLMediaCryptoRelease ::MLSDK_API::MLMediaCryptoReleaseShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoIsCryptoSchemeSupported)
#define MLMediaCryptoIsCryptoSchemeSupported ::MLSDK_API::MLMediaCryptoIsCryptoSchemeSupportedShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoRequiresSecureDecoderComponent)
#define MLMediaCryptoRequiresSecureDecoderComponent ::MLSDK_API::MLMediaCryptoRequiresSecureDecoderComponentShim
CREATE_FUNCTION_SHIM(ml_mediacrypto, MLResult, MLMediaCryptoSetMediaDRMSession)
#define MLMediaCryptoSetMediaDRMSession ::MLSDK_API::MLMediaCryptoSetMediaDRMSessionShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
