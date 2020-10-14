// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotFilters.h"

// All actors are valid by default
bool ULevelSnapshotFilter::IsActorValid(const FName ActorName, const UClass* ActorClass) const
{
	return true;
}

bool ULevelSnapshotFilter::IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const
{
	return true;
}

bool ULevelSnapshotBlueprintFilter::IsActorValid_Implementation(const FName ActorName, const UClass* ActorClass) const
{
	return true;
}

bool ULevelSnapshotBlueprintFilter::IsPropertyValid_Implementation(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const
{
	return true;
}