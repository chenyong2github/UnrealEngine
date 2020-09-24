// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"

/**
 * ActorDesc for LandscapeActors.
 */
class ENGINE_API FLandscapeActorDesc : public FPartitionActorDesc
{
#if WITH_EDITOR
	friend class FLandscapeActorDescFactory;

protected:
	virtual void InitFrom(const AActor* InActor) override;
#endif
};
