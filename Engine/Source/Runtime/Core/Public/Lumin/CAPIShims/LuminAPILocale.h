// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_locale.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

	CREATE_FUNCTION_SHIM(ml_locale, MLResult, MLLocaleGetSystemLanguage)
	#define MLLocaleGetSystemLanguage ::MLSDK_API::MLLocaleGetSystemLanguageShim
	CREATE_FUNCTION_SHIM(ml_locale, MLResult, MLLocaleGetSystemCountry)
	#define MLLocaleGetSystemCountry ::MLSDK_API::MLLocaleGetSystemCountryShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
