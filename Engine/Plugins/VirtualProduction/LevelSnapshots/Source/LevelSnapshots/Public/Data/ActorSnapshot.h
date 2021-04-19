// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Data/BaseObjectInfo.h"
#include "ActorSnapshot.generated.h"

class UActorComponent;
class ULevelSnapshot;
class ULevelSnapshotFilter;
class ULevelSnapshotSelectionSet;

/**
 * TypeTraits to define FSerializedActorData with a Serialize function
 */
template<>
struct TStructOpsTypeTraits<FSerializedActorData> : public TStructOpsTypeTraitsBase2<FSerializedActorData>
{
	enum
	{
		WithSerializer = true,
	};
};

USTRUCT()
struct LEVELSNAPSHOTS_API FLevelSnapshot_Component
{
	GENERATED_BODY()

	FLevelSnapshot_Component() = default;
	explicit FLevelSnapshot_Component(UActorComponent* TargetComponent);

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FBaseObjectInfo Base;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	bool bIsSceneComponent = false;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString ParentComponentPath;
};

USTRUCT()
struct LEVELSNAPSHOTS_API FLevelSnapshot_Actor
{
	GENERATED_BODY()

	FLevelSnapshot_Actor() = default;
	explicit FLevelSnapshot_Actor(AActor* TargetActor);

	/* Checks whether this is saving data for the given world actor */
	bool CorrespondsToActorInWorld(const AActor* WorldActor) const;
	
	AActor* GetDeserializedActor(UWorld* TempWorld);
	void DeserializeIntoWorldActor(AActor* InTargetActor, const ULevelSnapshotSelectionSet* InPropertiesToDeserializeInto) const;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FBaseObjectInfo Base;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TArray<FLevelSnapshot_Component> ComponentSnapshots;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CachedDeserialisedActor;

private:

	enum EActorType
	{
		/* The actor does not belong to any world */
		TransientActor,
        /* The actor belongs to a world */
        WorldActor
    };
	
	void DeserializeTransientActorProperties(AActor* InTargetActor) const;
	void DeserializeWorldActorProperties(AActor* InTargetActor, const ULevelSnapshotSelectionSet* InSelectedProperties) const;
};