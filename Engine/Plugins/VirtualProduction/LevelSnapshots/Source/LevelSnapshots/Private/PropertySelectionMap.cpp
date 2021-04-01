// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySelectionMap.h"

void FPropertySelectionMap::AddObjectProperties(UObject* WorldObject, const TArray<TFieldPath<FProperty>>& SelectedPropertyPaths)
{
	if (SelectedPropertyPaths.Num() == 0)
	{
		return;
	}
	if (!ensure(WorldObject))
	{
		return;
	}
	
	FPropertySelection& PropertySelection = SelectedWorldObjectsToSelectedProperties.FindOrAdd(WorldObject);
	for (const TFieldPath<FProperty>& PropertyPath : SelectedPropertyPaths)
	{
		PropertySelection.SelectedPropertyPaths.AddUnique(PropertyPath);
	}
}

void FPropertySelectionMap::RemoveObjectPropertiesFromMap(UObject* WorldObject)
{
	SelectedWorldObjectsToSelectedProperties.Remove(WorldObject);
}

const FPropertySelection* FPropertySelectionMap::GetSelectedProperties(UObject* WorldObject) const
{
	return SelectedWorldObjectsToSelectedProperties.Find(WorldObject); 
}

TArray<TWeakObjectPtr<UObject>> FPropertySelectionMap::GetKeys() const
{
	TArray<TWeakObjectPtr<UObject>> Result;
	SelectedWorldObjectsToSelectedProperties.GenerateKeyArray(Result);
	return Result;
}
