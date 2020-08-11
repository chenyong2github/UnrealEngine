// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

class UFoundationSubsystem;
class UWorldPartition;

/**
 * ActorDesc for Actors that are part of a FoundationActor Level.
 */
class ENGINE_API FFoundationActorDesc : public FWorldPartitionActorDesc
{
public:
#if WITH_EDITOR
	inline FName GetLevelPackage() const { return LevelPackage; }

protected:
	FFoundationActorDesc() = delete;
	FFoundationActorDesc(const FWorldPartitionActorDescData& DescData, FName LevelPackage);
	FFoundationActorDesc(AActor* InActor);

	virtual void BuildHash(FHashBuilder& HashBuilder) override;

	friend class FFoundationActorDescFactory;

	FName LevelPackage;
#endif
};
