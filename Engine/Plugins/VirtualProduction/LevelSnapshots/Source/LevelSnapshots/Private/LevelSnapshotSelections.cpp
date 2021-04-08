// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotSelections.h"

#include "LevelSnapshotsLog.h"
#include "PropertySelectionMap.h"

void ULevelSnapshotSelectionSet::AddObjectProperties(const UObject* WorldObject, const TArray<TFieldPath<FProperty>>& SelectedPropertyPaths)
{
	// Adding an empty SelectedPropertyPaths is valid
	if (!ensure(WorldObject))
	{
		return;
	}
	
	const FSoftObjectPath ObjectPath(WorldObject);
	AddObjectProperties(ObjectPath, SelectedPropertyPaths);
}

void ULevelSnapshotSelectionSet::AddObjectProperties(const FSoftObjectPath ObjectPath, const TArray<TFieldPath<FProperty>>& SelectedPropertyPaths)
{
	// If there's no properties to add then early out
	if (!SelectedPropertyPaths.Num())
	{
		return;
	}

	FPropertySelection& PropertySelection = SelectedWorldObjectsToSelectedProperties.FindOrAdd(ObjectPath);
	for (const TFieldPath<FProperty>& PropertyPath : SelectedPropertyPaths)
	{
		PropertySelection.SelectedPropertyPaths.AddUnique(PropertyPath);
	}
}

void ULevelSnapshotSelectionSet::AddPropertyMap(const FPropertySelectionMap& SelectionMap)
{
	const TArray<TWeakObjectPtr<UObject>> WorldObjects = SelectionMap.GetKeys();
	for (const TWeakObjectPtr<UObject>& WorldObject : WorldObjects)
	{
		if (UObject* Object = WorldObject.Get())
		{
			const FPropertySelection* Selection = SelectionMap.GetSelectedProperties(Object);
			check(Selection);
			SelectedWorldObjectsToSelectedProperties.Add(Object, *Selection);
		}
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

const int32 ULevelSnapshotSelectionSet::NumberOfObjects() const
{
	return SelectedWorldObjectsToSelectedProperties.Num();
}

const FString ULevelSnapshotSelectionSet::ToString() const
{
	FString DebugString = FString::Printf(TEXT("-- %s --\n"), *this->GetName());
	for (const TPair<FSoftObjectPath, FPropertySelection>& ObjectSelection : SelectedWorldObjectsToSelectedProperties)
	{
		DebugString += ObjectSelection.Key.ToString() + TEXT("\n");
		for (const TFieldPath<FProperty>& PropertyPath : ObjectSelection.Value.SelectedPropertyPaths)
		{
			DebugString += TEXT("|- ") + PropertyPath.ToString() + TEXT("\n");
		}
	}
	return DebugString;
}
