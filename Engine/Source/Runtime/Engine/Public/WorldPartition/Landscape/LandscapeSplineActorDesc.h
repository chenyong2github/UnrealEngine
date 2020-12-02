// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

/**
 * ActorDesc for LandscapeSplineActors.
 */
class ENGINE_API FLandscapeSplineActorDesc : public FWorldPartitionActorDesc
{
#if WITH_EDITOR
protected:
	virtual void Init(const AActor* InActor) override;
	virtual void Serialize(FArchive& Ar) override;

	FGuid LandscapeGuid;
#endif
};
