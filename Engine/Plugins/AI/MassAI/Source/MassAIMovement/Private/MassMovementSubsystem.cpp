// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassMovementSubsystem.h"
#include "Engine/World.h"
#include "MassSimulationSubsystem.h"
#include "ZoneGraphSubsystem.h"

//----------------------------------------------------------------------//
// UMassMovementSubsystem
//----------------------------------------------------------------------//
UMassMovementSubsystem::UMassMovementSubsystem() : AvoidanceObstacleGrid(250.f) // 2.5m grid
{
}

void UMassMovementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UMassSimulationSubsystem>();
	ZoneGraphSubsystem = Collection.InitializeDependency<UZoneGraphSubsystem>();

	EntitySubsystem = UPipeEntitySubsystem::GetCurrent(GetWorld());
}

