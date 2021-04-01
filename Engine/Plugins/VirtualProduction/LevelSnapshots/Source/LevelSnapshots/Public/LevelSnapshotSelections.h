// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelection.h"

#include "LevelSnapshotSelections.generated.h"

struct FPropertySelectionMap;

UCLASS(BlueprintType)
class LEVELSNAPSHOTS_API ULevelSnapshotSelectionSet : public UObject
{
	GENERATED_BODY()
public:

	void AddObjectProperties(const UObject* WorldObject, const TArray<TFieldPath<FProperty>>& SelectedPropertyPaths);
	void AddObjectProperties(const FSoftObjectPath ObjectPath, const TArray<TFieldPath<FProperty>>& SelectedPropertyPaths);
	void AddPropertyMap(const FPropertySelectionMap& SelectionMap);
	void Clear();

	const TArray<FSoftObjectPath> GetSelectedWorldObjectPaths() const;
	const FPropertySelection* GetSelectedProperties(const FSoftObjectPath ObjectPath) const;

	const int32 NumberOfObjects() const;
	const FString ToString() const;
private:
	
	/* Maps a world actor to the properties that should be restored to values in a snapshot.
	 * The properties are located in the actor itself or any other subcontainer (structs, components, or subobjects)
	 */
	UPROPERTY()
	TMap<FSoftObjectPath, FPropertySelection> SelectedWorldObjectsToSelectedProperties;
	
};