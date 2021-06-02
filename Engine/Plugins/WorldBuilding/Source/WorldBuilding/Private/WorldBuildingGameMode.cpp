// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBuildingGameMode.h"
#include "WorldBuildingPawn.h"

AWorldBuildingGameMode::AWorldBuildingGameMode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultPawnClass = AWorldBuildingPawn::StaticClass();
}

