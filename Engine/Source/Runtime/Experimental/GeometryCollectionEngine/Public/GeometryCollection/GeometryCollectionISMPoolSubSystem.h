// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GeometryCollectionISMPoolSubSystem.generated.h"

class AGeometryCollectionISMPoolActor;

/**
 * A subsystem managing ISMPool actors.
 * Used by geometry collection now but repurposed for more general use.
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

	/** Finds or creates an actor. */
	AGeometryCollectionISMPoolActor* FindISMPoolActor();
	
	/** Get all actors managed by the subsystem. */
	void GetISMPoolActors(TArray<AGeometryCollectionISMPoolActor*>& OutActors) const;

protected:
	/** For now we only use one ISMPool actor per world, but we could extend the system to manage many more and return the right one based on search criteria. */
	UPROPERTY(Transient)
	TObjectPtr<AGeometryCollectionISMPoolActor> ISMPoolActor;
};
