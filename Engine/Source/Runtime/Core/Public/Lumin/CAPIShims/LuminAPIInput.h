// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_input.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputCreate)
#define MLInputCreate ::MLSDK_API::MLInputCreateShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputSetControllerCallbacks)
#define MLInputSetControllerCallbacks ::MLSDK_API::MLInputSetControllerCallbacksShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetControllerState)
#define MLInputGetControllerState ::MLSDK_API::MLInputGetControllerStateShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputStartControllerFeedbackPatternVibe)
#define MLInputStartControllerFeedbackPatternVibe ::MLSDK_API::MLInputStartControllerFeedbackPatternVibeShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputStartControllerFeedbackPatternLED)
#define MLInputStartControllerFeedbackPatternLED ::MLSDK_API::MLInputStartControllerFeedbackPatternLEDShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputStartControllerFeedbackPatternEffectLED)
#define MLInputStartControllerFeedbackPatternEffectLED ::MLSDK_API::MLInputStartControllerFeedbackPatternEffectLEDShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputSetKeyboardCallbacks)
#define MLInputSetKeyboardCallbacks ::MLSDK_API::MLInputSetKeyboardCallbacksShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetKeyboardState)
#define MLInputGetKeyboardState ::MLSDK_API::MLInputGetKeyboardStateShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputDestroy)
#define MLInputDestroy ::MLSDK_API::MLInputDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
