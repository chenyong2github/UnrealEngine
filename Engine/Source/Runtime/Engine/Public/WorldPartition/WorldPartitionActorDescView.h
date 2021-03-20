// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
class UHLODLayer;
class FWorldPartitionActorDesc;
class UActorDescContainer;
enum class EContainerClusterMode : uint8;
enum class EActorGridPlacement : uint8;

/**
 * A view on top of an actor desc, used to cache information that can be (potentially) different than the actor desc
 * itself due to streaming generation logic, etc.
 */
class ENGINE_API FWorldPartitionActorDescView
{
	friend class UWorldPartitionRuntimeHash;

public:
	FWorldPartitionActorDescView();
	FWorldPartitionActorDescView(const FWorldPartitionActorDesc* InActorDesc);

	const FGuid& GetGuid() const;
	FName GetClass() const;
	UClass* GetActorClass() const;
	FVector GetOrigin() const;
	EActorGridPlacement GetGridPlacement() const;
	FName GetRuntimeGrid() const;
	bool GetActorIsEditorOnly() const;
	bool GetLevelBoundsRelevant() const;
	bool GetActorIsHLODRelevant() const;
	UHLODLayer* GetHLODLayer() const;
	const TArray<FName>& GetDataLayers() const;
	FName GetActorPackage() const;
	FName GetActorPath() const;
	FName GetActorLabel() const;
	FBox GetBounds() const;
	const TArray<FGuid>& GetReferences() const;
	uint32 GetTag() const;

	bool GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const;

	bool operator==(const FWorldPartitionActorDescView& Other) const
	{
		return GetGuid() == Other.GetGuid();
	}

	friend uint32 GetTypeHash(const FWorldPartitionActorDescView& Key)
	{
		return GetTypeHash(Key.GetGuid());
	}

protected:
	const FWorldPartitionActorDesc* ActorDesc;
	EActorGridPlacement EffectiveGridPlacement;
};
#endif