// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseObjectInfo.h"

#include "ActorSnapshot.generated.h"

class ULevelSnapshot;

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

	// Initialize the snapshot from a given actor
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

	// Initialize the snapshot from a given actor
	explicit FLevelSnapshot_Actor(AActor* TargetActor);

	AActor* GetDeserializedActor() const;

	void FixupComponents(AActor* TargetActor) const;

	void Deserialize(AActor* TargetActor) const;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
		FBaseObjectInfo Base;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
		TMap<FString, FLevelSnapshot_Component> ComponentSnapshots;

	bool operator==(const FLevelSnapshot_Actor& OtherSnapshot) const
	{
		return OtherSnapshot.Base == this->Base;
	};
};