// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySelectionMap.h"

#include "LevelSnapshotsLog.h"

#include "GameFramework/Actor.h"
#include "UObject/UObjectHash.h"

void FPropertySelectionMap::AddDeletedActorToRespawn(const FSoftObjectPath& Original)
{
	DeletedActorsToRespawn.Add(Original);
}

void FPropertySelectionMap::RemoveDeletedActorToRespawn(const FSoftObjectPath& Original)
{
	DeletedActorsToRespawn.Remove(Original);
}

void FPropertySelectionMap::AddNewActorToDespawn(AActor* WorldActor)
{
	NewActorsToDespawn.Add(WorldActor);
}

void FPropertySelectionMap::RemoveNewActorToDespawn(AActor* WorldActor)
{
	NewActorsToDespawn.Remove(WorldActor);
}

bool FPropertySelectionMap::AddObjectProperties(UObject* WorldObject, const FPropertySelection& SelectedProperties)
{
	if (!SelectedProperties.IsEmpty() && ensure(WorldObject))
	{
		SelectedWorldObjectsToSelectedProperties.FindOrAdd(WorldObject) = SelectedProperties;
		return true;
	}
	return false;
}

void FPropertySelectionMap::RemoveObjectPropertiesFromMap(UObject* WorldObject)
{
	SelectedWorldObjectsToSelectedProperties.Remove(WorldObject);
}

const FPropertySelection* FPropertySelectionMap::GetSelectedProperties(UObject* WorldObject) const
{
	return GetSelectedProperties(FSoftObjectPath(WorldObject));
}

const FPropertySelection* FPropertySelectionMap::GetSelectedProperties(const FSoftObjectPath& WorldObjectPath) const
{
	return SelectedWorldObjectsToSelectedProperties.Find(WorldObjectPath); 
}

TArray<FSoftObjectPath> FPropertySelectionMap::GetKeys() const
{
	TArray<FSoftObjectPath> Result;
	SelectedWorldObjectsToSelectedProperties.GenerateKeyArray(Result);
	return Result;
}

void FPropertySelectionMap::Empty(bool bCanShrink)
{
	SelectedWorldObjectsToSelectedProperties.Empty(bCanShrink ? SelectedWorldObjectsToSelectedProperties.Num() : 0);
	DeletedActorsToRespawn.Empty(bCanShrink ? DeletedActorsToRespawn.Num() : 0);
	NewActorsToDespawn.Empty(bCanShrink ? NewActorsToDespawn.Num() : 0);
}

TArray<UObject*> FPropertySelectionMap::GetDirectSubobjectsWithProperties(UObject* Root) const
{
	SCOPED_SNAPSHOT_CORE_TRACE(GetDirectSubobjectsWithProperties);
	
	TArray<UObject*> Subobjects;
	GetObjectsWithOuter(Root, Subobjects, true);

	for (int32 i = Subobjects.Num() - 1; i > 0; --i)
	{
		const bool bHasSelectedProperties = GetSelectedProperties(Subobjects[i]) != nullptr;
		if (!bHasSelectedProperties)
		{
			Subobjects.RemoveAt(i);
		}
	}

	return Subobjects;
}
