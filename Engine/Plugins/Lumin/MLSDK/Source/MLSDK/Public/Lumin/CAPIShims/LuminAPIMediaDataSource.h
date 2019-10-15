// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_media_common.h>
#include <ml_media_data_source.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaDataSourceCreate)
#define MLMediaDataSourceCreate ::MLSDK_API::MLMediaDataSourceCreateShim
CREATE_FUNCTION_SHIM(ml_mediaplayer, MLResult, MLMediaDataSourceDestroy)
#define MLMediaDataSourceDestroy ::MLSDK_API::MLMediaDataSourceDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
