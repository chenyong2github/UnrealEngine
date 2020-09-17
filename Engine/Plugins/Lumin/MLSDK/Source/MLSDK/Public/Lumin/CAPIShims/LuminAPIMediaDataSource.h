// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_data_source.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaDataSourceCreate)
#define MLMediaDataSourceCreate ::LUMIN_MLSDK_API::MLMediaDataSourceCreateShim
CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaDataSourceDestroy)
#define MLMediaDataSourceDestroy ::LUMIN_MLSDK_API::MLMediaDataSourceDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
