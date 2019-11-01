// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_error.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediaerror, const char *, MLMediaResultGetString)
#define MLMediaResultGetString ::MLSDK_API::MLMediaResultGetStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
