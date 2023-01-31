// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GeometryCollectionISMPoolSubSystem.generated.h"

class AGeometryCollectionISMPoolActor;

/**
* A subsystem managing StateTree assets in Mass
*/
UCLASS()
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollectionISMPoolSubSystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	UGeometryCollectionISMPoolSubSystem();

	// USubsystem BEGIN
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// USubsystem END

	// UWorldSubsystem BEGIN
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	// UWorldSubsystem END

	AGeometryCollectionISMPoolActor* FindISMPoolActor(const USceneComponent& Requester);

protected:

	/** for now we only use one ISMPool actor per world, but we could extend the system to manage many more and return the right one based on  search criteria */
	TObjectPtr<AGeometryCollectionISMPoolActor> ISMPoolActor;
};
