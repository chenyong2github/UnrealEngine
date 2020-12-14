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
	virtual void OnRegister(UWorldPartition* WorldPartition) override;
	virtual void OnUnregister(UWorldPartition* WorldPartition) override;

	FGuid LandscapeGuid;
#endif
};
