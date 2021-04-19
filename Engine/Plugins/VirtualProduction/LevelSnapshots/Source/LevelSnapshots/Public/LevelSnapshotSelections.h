// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelection.h"

#include "LevelSnapshotSelections.generated.h"

struct FPropertySelectionMap;

/* This type is only used by the old system. TODO: Remove this type once legacy data has been recovered. */
UCLASS()
class LEVELSNAPSHOTS_API ULevelSnapshotSelectionSet : public UObject
{
	GENERATED_BODY()
public:

	void AddObjectProperties(const UObject* WorldObject, const FPropertySelection& SelectedProperties);
	void AddObjectProperties(const FSoftObjectPath ObjectPath, const FPropertySelection& SelectedProperties);
	void AddPropertyMap(const FPropertySelectionMap& SelectionMap);
	void Clear();

	const TArray<FSoftObjectPath> GetSelectedWorldObjectPaths() const;
	const FPropertySelection* GetSelectedProperties(const FSoftObjectPath ObjectPath) const;
	
private:
	
	/* Maps a world actor to the properties that should be restored to values in a snapshot.
	 * The properties are located in the actor itself or any other subcontainer (structs, components, or subobjects)
	 */
	TMap<FSoftObjectPath, FPropertySelection> SelectedWorldObjectsToSelectedProperties;
	
};