// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"

/**
 * ActorDesc for LandscapeActors.
 */
class ENGINE_API FLandscapeActorDesc : public FPartitionActorDesc
{
#if WITH_EDITOR
protected:
	virtual void Init(const AActor* InActor) override;
#endif
};
