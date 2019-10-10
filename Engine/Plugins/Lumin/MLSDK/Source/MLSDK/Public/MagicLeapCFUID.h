// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Lumin/CAPIShims/LuminAPI.h"

namespace MagicLeap
{
#if WITH_MLSDK
    static_assert(sizeof(FGuid) >= sizeof(MLCoordinateFrameUID), "Size of FGuid should be at least as much as MLCoordinateFrameUID.");

    FORCEINLINE void FGuidToMLCFUID(const FGuid& UnrealGuid, MLCoordinateFrameUID& MLGuid)
    {
        FMemory::Memcpy(&MLGuid, &UnrealGuid, sizeof(MLCoordinateFrameUID));
    }

    FORCEINLINE void MLCFUIDToFGuid(const MLCoordinateFrameUID& MLGuid, FGuid& UnrealGuid)
    {
        FMemory::Memcpy(&UnrealGuid, &MLGuid, sizeof(FGuid));
    }
#endif // WITH_MLSDK
}
