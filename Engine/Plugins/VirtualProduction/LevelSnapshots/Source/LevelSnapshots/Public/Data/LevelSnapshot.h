// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

	DECLARE_DELEGATE_OneParam(FActorPathConsumer, const FSoftObjectPath& /*OriginalActorPath*/);
	DECLARE_DELEGATE_OneParam(FActorConsumer, AActor* /*WorldActor*/);
	
	
	/* Applies this snapshot to the given world. We assume the world matches. SelectionSet specifies which properties to roll back. */
	void ApplySnapshotToWorld(UWorld* TargetWorld, const FPropertySelectionMap& SelectionSet);
	/* Captures the current state of the given world. */
	bool SnapshotWorld(UWorld* TargetWorld);

	

	/* Checks whether the original actor has any properties that changed since the snapshot was taken.  */
	bool HasOriginalChangedPropertiesSinceSnapshotWasTaken(AActor* SnapshotActor, AActor* WorldActor) const;
	/**
	* Checks whether the snapshot and original property value should be considered equal.
	* Primitive properties are trivial. Special support is needed for object references.
	*/
	bool AreSnapshotAndOriginalPropertiesEquivalent(const FProperty* LeafProperty, void* SnapshotContainer, void* WorldContainer, AActor* SnapshotActor, AActor* WorldActor) const;
	
	
	
	/* Given an actor path in the world, gets the equivalent actor from the snapshot. */
	TOptional<AActor*> GetDeserializedActor(const FSoftObjectPath& OriginalActorPath);
	
	int32 GetNumSavedActors() const;
	/**
	 * Compares this snapshot to the world and calls the appropriate callbacks:
	 * @param World to check in
	 *	@param HandleMatchedActor Actor exists both in world and snapshot. Receives the original actor path.
	 *	@param HandleRemovedActor Actor exists in snapshot but not in world. Receives the original actor path.
	 *	@param HandleAddedActor Actor exists in world but not in snapshot. Receives reference to world actor.
	 */
	void DiffWorld(UWorld* World, FActorPathConsumer HandleMatchedActor, FActorPathConsumer HandleRemovedActor, FActorConsumer HandleAddedActor) const;

	
	
	/* Sets the name of this snapshot. */
	UFUNCTION(BlueprintCallable, Category = "Level Snapshots")
	void SetSnapshotName(const FName& InSnapshotName);
	UFUNCTION(BlueprintCallable, Category = "Level Snapshots")
	void SetSnapshotDescription(const FString& InSnapshotDescription);

	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FSoftObjectPath GetMapPath() const { return MapPath; }
	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FDateTime GetCaptureTime() const { return CaptureTime; }
	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FName GetSnapshotName() const { return SnapshotName; }
	UFUNCTION(BlueprintPure, Category = "Level Snapshots")
	FString GetSnapshotDescription() const { return SnapshotDescription; }

	const FWorldSnapshotData& GetSerializedData() const { return SerializedData; }

	
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface
	
private:

	void EnsureWorldInitialised();
	void DestroyWorld();

	
	/* The world we will be adding temporary actors to */
	UPROPERTY(Transient)
	UWorld* SnapshotContainerWorld;
	/* Callback to destroy our world when editor (editor build) or play (game builds) world is destroyed. */
	FDelegateHandle OnWorldDestroyed;
	

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FWorldSnapshotData SerializedData;

	/* Path of the map that the snapshot was taken in */
	UPROPERTY(VisibleAnywhere, BlueprintGetter = "GetMapPath", AssetRegistrySearchable, Category = "Snapshot")
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
