// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Lumin/CAPIShims/LuminAPI.h"

namespace MagicLeap
{
#if WITH_MLSDK
	FORCEINLINE MLHandle FGuidToMLHandle(const FGuid& UnrealGuid)
	{
		MLHandle MLGuid = ML_INVALID_HANDLE;
		FMemory::Memcpy(&MLGuid, &UnrealGuid, sizeof(MLHandle));
		return MLGuid;
	}

	FORCEINLINE FGuid MLHandleToFGuid(MLHandle MLGuid)
	{
		FGuid UnrealGuid;
		FMemory::Memcpy(&UnrealGuid, &MLGuid, sizeof(MLHandle));
		return UnrealGuid;
	}
#endif // WITH_MLSDK

	FORCEINLINE bool FGuidIsValidHandle(const FGuid& UnrealGuid)
	{
#if WITH_MLSDK
		return MLHandleIsValid(MagicLeap::FGuidToMLHandle(UnrealGuid));
#else
		return false;
#endif // WITH_MLSDK
	}

#if WITH_MLSDK
	static FGuid INVALID_FGUID = MLHandleToFGuid(ML_INVALID_HANDLE);
#else
	static FGuid INVALID_FGUID = FGuid();
#endif // WITH_MLSDK
}
