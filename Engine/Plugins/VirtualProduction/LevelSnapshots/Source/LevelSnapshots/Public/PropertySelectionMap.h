// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelection.h"
#include "PropertySelectionMap.generated.h"

/* Binds an object to its selected properties.
 * 
 * Similar to ULevelSnapshotsSelectionSet only that:
 *	- This struct uses weak object ptr instead of soft object paths to avoid StaticFindObject calls
 *	- This struct is more lightweight that creating a UObject for temporary results.
 */
USTRUCT()
struct LEVELSNAPSHOTS_API FPropertySelectionMap
{
	GENERATED_BODY()
public:
	
	void AddObjectProperties(UObject* WorldObject, const TArray<TFieldPath<FProperty>>& SelectedPropertyPaths);
	void RemoveObjectPropertiesFromMap(UObject* WorldObject);
	
	const FPropertySelection* GetSelectedProperties(UObject* WorldObject) const;
	TArray<TWeakObjectPtr<UObject>> GetKeys() const;

private:
	
	/* Maps a world actor to the properties that should be restored to values in a snapshot.
	 * The properties are located in the actor itself or any other subcontainer (structs, components, or subobjects)
	 */
	UPROPERTY()
	TMap<TWeakObjectPtr<UObject>, FPropertySelection> SelectedWorldObjectsToSelectedProperties;
	
};
