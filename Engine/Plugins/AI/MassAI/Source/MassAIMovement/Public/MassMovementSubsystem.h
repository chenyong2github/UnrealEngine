// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntitySubsystem.h"
#include "HierarchicalHashGrid2D.h"
#include "MassMovementTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassMovementSubsystem.generated.h"

class UZoneGraphSubsystem;

UCLASS()
class MASSAIMOVEMENT_API UMassMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UMassMovementSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const FAvoidanceObstacleHashGrid2D& GetGrid() const { return AvoidanceObstacleGrid; }
	FAvoidanceObstacleHashGrid2D& GetGridMutable() { return AvoidanceObstacleGrid; }

protected:

	UPROPERTY(Transient)
	UMassEntitySubsystem* EntitySubsystem;

	UPROPERTY(Transient)
	UZoneGraphSubsystem* ZoneGraphSubsystem;

	FAvoidanceObstacleHashGrid2D AvoidanceObstacleGrid;
};
