// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotSelections.h"

#include "PropertySelectionMap.h"

void ULevelSnapshotSelectionSet::AddObjectProperties(const UObject* WorldObject, const FPropertySelection& SelectedProperties)
{
	if (ensure(WorldObject))
	{
		const FSoftObjectPath ObjectPath(WorldObject);
		AddObjectProperties(ObjectPath, SelectedProperties);
	}
}

void ULevelSnapshotSelectionSet::AddObjectProperties(const FSoftObjectPath ObjectPath, const FPropertySelection& SelectedProperties)
{
	// If there's no properties to add then early out
	if (ObjectPath.IsValid() && !SelectedProperties.IsEmpty())
	{
		SelectedWorldObjectsToSelectedProperties.FindOrAdd(ObjectPath) = SelectedProperties;
	}
}

void ULevelSnapshotSelectionSet::AddPropertyMap(const FPropertySelectionMap& SelectionMap)
{
	const TArray<FSoftObjectPath> WorldObjects = SelectionMap.GetKeys();
	for (const FSoftObjectPath& WorldObject : WorldObjects)
	{
		const FPropertySelection* Selection = SelectionMap.GetSelectedProperties(WorldObject);
		check(Selection);
		SelectedWorldObjectsToSelectedProperties.Add(WorldObject, *Selection);
	}
}

void ULevelSnapshotSelectionSet::Clear()
{
	SelectedWorldObjectsToSelectedProperties.Empty();
}

const TArray<FSoftObjectPath> ULevelSnapshotSelectionSet::GetSelectedWorldObjectPaths() const
{
	TArray<FSoftObjectPath> OutPaths;
	SelectedWorldObjectsToSelectedProperties.GetKeys(OutPaths);

	return OutPaths;
}

const FPropertySelection* ULevelSnapshotSelectionSet::GetSelectedProperties(const FSoftObjectPath ObjectPath) const
{
	return SelectedWorldObjectsToSelectedProperties.Find(ObjectPath); 
}
