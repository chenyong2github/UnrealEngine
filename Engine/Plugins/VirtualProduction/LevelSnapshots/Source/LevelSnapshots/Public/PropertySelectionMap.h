// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelection.h"
#include "PropertySelectionMap.generated.h"

class AActor;

/* Binds an object to its selected properties */
USTRUCT()
struct LEVELSNAPSHOTS_API FPropertySelectionMap
{
	GENERATED_BODY()

	/* Respawn the actor from the data in the snapshot. */
	void AddDeletedActorToRespawn(const FSoftObjectPath& Original);
	void RemoveDeletedActorToRespawn(const FSoftObjectPath& Original);

	/* Destroy the given actors when a snapshot is applied. */
	void AddNewActorToDespawn(AActor* WorldActor);
	void RemoveNewActorToDespawn(AActor* WorldActor);

	/* Binds properties to an object which are supposed to be rolled back. */
	bool AddObjectProperties(UObject* WorldObject, const FPropertySelection& SelectedProperties);
	void RemoveObjectPropertiesFromMap(UObject* WorldObject);
	
	const FPropertySelection* GetSelectedProperties(UObject* WorldObject) const;
	const FPropertySelection* GetSelectedProperties(const FSoftObjectPath& WorldObjectPath) const;
	TArray<FSoftObjectPath> GetKeys() const;
	
	int32 GetKeyCount() const { return SelectedWorldObjectsToSelectedProperties.Num(); }
	const TSet<FSoftObjectPath>& GetDeletedActorsToRespawn() const { return DeletedActorsToRespawn; }
	const TSet<TWeakObjectPtr<AActor>>& GetNewActorsToDespawn() const { return NewActorsToDespawn; }

	void Empty(bool bCanShrink = true);

	/* Gets the direct subobjects of root that have selected properties. You can recursively call this function with the element of the result array. */
	TArray<UObject*> GetDirectSubobjectsWithProperties(UObject* Root) const;
	
private:
	
	/* Maps a world actor to the properties that should be restored to values in a snapshot.
	 * The properties are located in the actor itself or any other subcontainer (structs, components, or subobjects)
	 */
	TMap<FSoftObjectPath, FPropertySelection> SelectedWorldObjectsToSelectedProperties;

	/* These actors were removed since the snapshot was taken. Re-create them.
	 * This contains the original objects paths stored in the snapshot.
	 */
	UPROPERTY()
	TSet<FSoftObjectPath> DeletedActorsToRespawn;

	/* These actors were added since the snapshot was taken. Remove them. */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> NewActorsToDespawn;
};
