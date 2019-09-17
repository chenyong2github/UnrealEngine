// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Foliage Helper class
 */
class FFoliageHelper
{
public:
#if WITH_EDITOR
	static void SetIsOwnedByFoliage(AActor* InActor) { if (InActor) { InActor->Tags.AddUnique("FoliageActorInstance"); } }
	static bool IsOwnedByFoliage(const AActor* InActor) { return InActor != nullptr && InActor->ActorHasTag("FoliageActorInstance"); }
#endif
};