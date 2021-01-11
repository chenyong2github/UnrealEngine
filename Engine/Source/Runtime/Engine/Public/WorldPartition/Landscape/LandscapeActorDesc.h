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
public:
	virtual void Init(const AActor* InActor) override;
	virtual void Unload() override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void Serialize(FArchive& Ar) override;
#endif
};
