// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class ULevelInstanceSubsystem;
class UWorldPartition;

/**
 * ActorDesc for Actors that are part of a LevelInstanceActor Level.
 */
class ENGINE_API FLevelInstanceActorDesc : public FWorldPartitionActorDesc
{
#if WITH_EDITOR
	friend class FLevelInstanceActorDescFactory;

public:
	inline FName GetLevelPackage() const { return LevelPackage; }

protected:
	virtual void BuildHash(FHashBuilder& HashBuilder) override;

	virtual void InitFrom(const AActor* InActor) override;
	virtual void Serialize(FArchive& Ar) override;

	FName LevelPackage;
	FTransform LevelInstanceTransform;
#endif
};
