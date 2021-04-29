// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSnapshotSelections.h"
#include "PropertySelectionMap.h"
#include "LevelSnapshotsFunctionLibrary.generated.h"

class ULevelSnapshot;
class ULevelSnapshotFilter;

UCLASS()
class LEVELSNAPSHOTS_API ULevelSnapshotsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta = (WorldContext = "WorldContextObject"))
	static ULevelSnapshot* TakeLevelSnapshot(const UObject* WorldContextObject, const FName NewSnapshotName = "NewLevelSnapshot", const FString Description = "");
	static ULevelSnapshot* TakeLevelSnapshot_Internal(const UObject* WorldContextObject, const FName NewSnapshotName = "NewLevelSnapshot", UPackage* InPackage = nullptr, const FString Description = "");

	// TODO: Add ApplySnapshotToWorldWithFilter UFUNCTION for Blueprints here
	
	/**
	 * Goes through the properties of the actors and their components calling IsPropertyValid on them.
	 * This function does not recursively check object references, e.g. 'Instanced' uproperties. These properties are currently unsupported by the snapshot framework.
	 */
	static void ApplyFilterToFindSelectedProperties(
		ULevelSnapshot* Snapshot,
		FPropertySelectionMap& MapToAddTo,
		AActor* WorldActor,
		AActor* DeserializedSnapshotActor,
		const ULevelSnapshotFilter* Filter,
		bool bAllowUnchangedProperties = false,
		bool bAllowNonEditableProperties = false
		);
};