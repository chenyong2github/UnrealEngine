// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

/**
 * ActorDesc for LandscapeSplineActors.
 */
class ENGINE_API FLandscapeSplineActorDesc : public FWorldPartitionActorDesc
{
public:
	FGuid LandscapeGuid;

protected:
	virtual void Init(const AActor* InActor) override;
	virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;
	virtual void Serialize(FArchive& Ar) override;
};
#endif
