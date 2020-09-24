// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/Landscape/LandscapeActorDesc.h"
#include "LandscapeProxy.h"

#if WITH_EDITOR

void FLandscapeActorDesc::InitFrom(const AActor* InActor)
{
	FPartitionActorDesc::InitFrom(InActor);

	const ALandscapeProxy* LandscapeProxy = CastChecked<ALandscapeProxy>(InActor);
	check(LandscapeProxy);
	GridIndexX = LandscapeProxy->LandscapeSectionOffset.X / LandscapeProxy->GridSize;
	GridIndexY = LandscapeProxy->LandscapeSectionOffset.Y / LandscapeProxy->GridSize;
	GridIndexZ = 0;
}

#endif
