// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorSnapshot.h"
#include "PreviewScene.h"
#include "WorldSnapshotData.h"
#include "LevelSnapshot.generated.h"

class ULevelSnapshotTrackingComponent;
struct FPropertySelectionMap;

/* Holds the state of a world at a given time. This asset can be used to rollback certain properties in a UWorld. */
UCLASS(BlueprintType)
class LEVELSNAPSHOTS_API ULevelSnapshot : public UObject
{
	GENERATED_BODY()
public:

	/* Should this actor supported for the snapshot system? */
	static bool IsActorDesirableForCapture(const AActor* Actor);
	static bool IsComponentDesirableForCapture(const UActorComponent* Component);
	
	
	/* Applies this snapshot to the given world. We assume the world matches. SelectionSet specifies which properties to roll back. */
	void ApplySnapshotToWorld(UWorld* TargetWorld, ULevelSnapshotSelectionSet* SelectionSet);
	/* Captures the current state of the given world. */
	void SnapshotWorld(UWorld* TargetWorld);

	

	/* Checks whether the original actor has any properties that changed since the snapshot was taken.  */
	bool HasOriginalChangedSinceSnapshot(AActor* SnapshotActor, AActor* WorldActor) const;
	/**
	* Checks whether the snapshot and original property value should be considered equal.
	* Primitive properties are trivial. Special support is needed for object references.
	*/
	bool AreSnapshotAndOriginalPropertiesEquivalent(const FProperty* Property, void* SnapshotContainer, void* WorldContainer, AActor* SnapshotActor, AActor* WorldActor) const;
	
	
	
	/* Given an actor path in the world, gets the equivalent actor from the snapshot. */
	TOptional<AActor*> GetDeserializedActor(const FSoftObjectPath& OriginalActorPath);
	/* Iterates all saved actors. */
	void ForEachOriginalActor(TFunction<void(const FSoftObjectPath& ActorPath)> HandleOriginalActorPath) const;

	
	
	/* Sets the name of this snapshot. */
	UFUNCTION(BlueprintCallable, Category = "Level Snapshots")
	void SetSnapshotName(const FName& InSnapshotName);
	UFUNCTION(BlueprintCallable, Category = "Level Snapshots")
	void SetSnapshotDescription(const FString& InSnapshotDescription);

	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FDateTime GetCaptureTime() const;
	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FName GetSnapshotName() const;
	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FString GetSnapshotDescription() const;

	
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface
	
private:

	void EnsureWorldInitialised();
	void DestroyWorld();
	
	/****************************** Start legacy members ******************************/

	void LegacyApplySnapshotToWorld(ULevelSnapshotSelectionSet* SelectionSet);
	
	// Map of Actor Snapshots mapping from the object path to the actual snapshot
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FSoftObjectPath, FLevelSnapshot_Actor> ActorSnapshots;

	/****************************** End legacy members ******************************/

	
	/* The world we will be adding temporary actors to */
	TSharedPtr<FPreviewScene> TempActorWorld;
	FDelegateHandle OnCleanWorldHandle;
	

	UPROPERTY()
	FWorldSnapshotData SerializedData;

	/* Path of the map that the snapshot was taken in */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Snapshot")
	FSoftObjectPath MapPath;
	
	/* UTC Time that the snapshot was taken */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetCaptureTime", VisibleAnywhere, Category = "Snapshot")
	FDateTime CaptureTime;

	/* User defined name for the snapshot, can differ from the actual asset name. */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetSnapshotName", EditAnywhere, Category = "Snapshot")
	FName SnapshotName;
	
	/* User defined description of the snapshot */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetSnapshotDescription", EditAnywhere, Category = "Snapshot")
	FString SnapshotDescription;
};
