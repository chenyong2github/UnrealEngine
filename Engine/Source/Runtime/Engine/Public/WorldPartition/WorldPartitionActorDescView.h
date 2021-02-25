// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
class UHLODLayer;
class FWorldPartitionActorDesc;
enum class EContainerClusterMode : uint8;

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
	inline FName GetClass() const;
	inline UClass* GetActorClass() const;
	inline FVector GetOrigin() const;
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

	bool GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const;

	FGuid GetHLODParent() const;
	void SetHLODParent(const FGuid& InHLODParent);

protected:
	const FWorldPartitionActorDesc* ActorDesc;
	EActorGridPlacement EffectiveGridPlacement;
	FGuid HLODParent;
};
#endif