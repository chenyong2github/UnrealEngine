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
public:
#if WITH_EDITOR
	inline FName GetLevelPackage() const { return LevelPackage; }

protected:
	FLevelInstanceActorDesc() = delete;
	FLevelInstanceActorDesc(const FWorldPartitionActorDescData& DescData, FName LevelPackage);
	FLevelInstanceActorDesc(AActor* InActor);

	virtual void BuildHash(FHashBuilder& HashBuilder) override;

	friend class FLevelInstanceActorDescFactory;

	FName LevelPackage;
#endif
};
