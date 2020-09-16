// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescFactory.h"
#include "WorldPartition.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartitionEditorCell;
class UWorldPartitionEditorHash;
class UWorldPartitionRuntimeCell;
class UWorldPartitionRuntimeHash;
class UWorldPartitionStreamingPolicy;

DECLARE_LOG_CATEGORY_EXTERN(LogWorldPartition, Log, All);

enum class EWorldPartitionStreamingMode
{
	PIE,
	RuntimeExternalObjects,
	RuntimeStreamingCells
};

enum class EWorldPartitionInitState
{
	Uninitialized,
	Initializing,
	Initialized,
	Uninitializing
};

#if WITH_EDITOR
/**
 * Interface for the world partition editor
 */
struct ENGINE_API IWorldPartitionEditor
{
	virtual void InvalidatePartition() {}
	virtual void RecreatePartition() {}
	virtual void Refresh() {}
};
#endif

UCLASS(AutoExpandCategories=(WorldPartition))
class ENGINE_API UWorldPartition final : public UObject
{
	GENERATED_UCLASS_BODY()

	friend class AActor;
	friend class UWorldPartitionEditorCell;
	friend class FWorldPartitionEditorModule;
	friend class FUnrealEdMisc;

#if WITH_EDITOR
private:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FEnableWorldPartitionEvent, UWorld*, bool);
	static FEnableWorldPartitionEvent EnableWorldPartitionEvent;

	DECLARE_MULTICAST_DELEGATE_OneParam(FWorldPartitionChangedEvent, UWorld*);
	static FWorldPartitionChangedEvent WorldPartitionChangedEvent;

	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& InPropertyChangedEvent);
	void OnObjectModified(UObject* Object);
	void OnObjectSaved(UObject* Object);
	void OnPostGarbageCollect();

	static FWorldPartitionActorDescFactory* GetActorDescFactory(TSubclassOf<AActor> Class);
	static FWorldPartitionActorDescFactory* GetActorDescFactory(const AActor* Actor);

	void FlushStreaming();

	// PIE Methods
	void OnPreBeginPIE(bool bStartSimulate);
	void OnEndPIE(bool bStartSimulate);

	void OnActorAdded(AActor* InActor);
	void OnActorDeleted(AActor* InActor);
	void OnActorOuterChanged(AActor* InActor, UObject* InOldOuter);
	void OnActorMoving(AActor* InActor);
		
	FDelegateHandle OnInitialAssetRegistrySearchCompleteHandle;
#endif

	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;

	static void WorldPartitionOnLevelRemovedFromWorld(class ULevel* Level, UWorld* InWorld);

#if WITH_EDITOR
	// UWorldPartitionSubsystem interface+
	friend class UWorldPartitionSubsystem;

	// UActorPartitionSubsystem interface+
	TArray<const FWorldPartitionActorDesc*> GetIntersectingActorDescs(const FBox& Box, TSubclassOf<AActor> ActorClass) const;
	// UActorPartitionSubsystem interface-

	void ApplyActorTransform(AActor* InActor, const FTransform& InTransform);

	static void RegisterActorDescFactory(TSubclassOf<AActor> Class, FWorldPartitionActorDescFactory* Factory);
	// UWorldPartitionSubsystem interface-

public:
	// public interface+
	FName GetWorldPartitionEditorName();
	
	// PIE Methods
	void PrepareForPIE();
	void CleanupForPIE();
	void OnPreFixupForPIE(int32 InPIEInstanceID, FSoftObjectPath& ObjectPath);

	bool IsSimulating() const;

	FWorldPartitionActorDesc* GetActorDesc(const FGuid& Guid);
	const FWorldPartitionActorDesc* GetActorDesc(const FGuid& Guid) const;

	void LoadEditorCells(const FBox& Box);
	void UnloadEditorCells(const FBox& Box);

	void LoadEditorCells(const TArray<UWorldPartitionEditorCell*>& Cells);
	void UnloadEditorCells(const TArray<UWorldPartitionEditorCell*>& Cells);

	void UpdateActorDesc(AActor* InActor);
	bool IsPreCooked() const { return bIsPreCooked; }
	void SetIsPreCooked(bool bInIsPreCooked) { bIsPreCooked = bInIsPreCooked; }

	bool GenerateStreaming(EWorldPartitionStreamingMode Mode);
	FBox GetWorldBounds() const;

	void GenerateHLOD();

	// Clustering
	struct FActorCluster
	{
		TSet<FGuid>			Actors;
		EActorGridPlacement	GridPlacement;
		FName				RuntimeGrid;
		FBox				Bounds;

		FActorCluster(const FWorldPartitionActorDesc* ActorDesc);
		void Add(const FActorCluster& ActorCluster);
	};

	const TSet<FActorCluster*>& GetActorClusters() const;
	const FActorCluster* GetClusterForActor(const FGuid& InActorGuid) const;
	// public interface-
#endif

public:
	void Initialize(UWorld* World, const FTransform& InTransform);
	bool IsInitialized() const;
	void Uninitialize();
	const FTransform& GetInstanceTransform() const { return InstanceTransform; }

	void Tick(float DeltaSeconds);
	void UpdateStreamingState();
	class ULevel* GetPreferredLoadedLevelToAddToWorld() const;

	FVector2D GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize);
	void DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize);
	void DrawRuntimeHash3D();

	virtual UWorld* GetWorld() const override;

	UPROPERTY(Transient)
	UWorld* World;

#if WITH_EDITORONLY_DATA
	UPROPERTY(DuplicateTransient)
	UWorldPartitionEditorHash* EditorHash;

	IWorldPartitionEditor* WorldPartitionEditor;
#endif

	UPROPERTY(EditAnywhere, Category=RuntimeHash, NoClear, meta = (NoResetToDefault), Instanced)
	UWorldPartitionRuntimeHash* RuntimeHash;

#if WITH_EDITOR
	TMap<FGuid, TUniquePtr<FWorldPartitionActorDesc>> Actors;
	class ULayersSubsystem* LayerSubSystem;
	FDelegateHandle OnLayersChangedHandle;
	FDelegateHandle OnActorsLayersChangedHandle;

	static TUniquePtr<FWorldPartitionActorDescFactory> DefaultActorDescFactory;
	static TMap<FName, FWorldPartitionActorDescFactory*> ActorDescFactories;

	DECLARE_EVENT_TwoParams(UWorldPartition, FWorldPartitionActorRegisteredEvent, AActor&, bool);
	FWorldPartitionActorRegisteredEvent OnActorRegisteredEvent;

	bool bDirty;
	bool bForceGarbageCollection;
	bool bForceGarbageCollectionPurge;
#endif

#if WITH_EDITORONLY_DATA
	// Default HLOD layer
	UPROPERTY()
	class UHLODLayer* DefaultHLODLayer;
#endif

private:
	EWorldPartitionInitState InitState;
	FTransform InstanceTransform;

	UPROPERTY(Transient, DuplicateTransient)
	mutable UWorldPartitionStreamingPolicy* StreamingPolicy;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bIsPreCooked;

	UPROPERTY(Transient)
	TSet<AActor*> ModifiedActors;

	FLinkerInstancingContext InstancingContext;
#endif

	// Actor registration
	AActor* RegisterActor(FWorldPartitionActorDesc* ActorDesc);
	void UnregisterActor(AActor* Actor);

	bool IsMainWorldPartition() const;
	bool IsValidPartitionActor(AActor* Actor) const;

	// Delegates registration
	void RegisterDelegates();
	void UnregisterDelegates();

#if WITH_EDITOR
	void UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded);
	void CreateLayers(const TSet<FName>& LayerNames);

	void RefreshLoadedCells();

	void HashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	void UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc);

	void AddToClusters(const FWorldPartitionActorDesc* ActorDesc);
	void RemoveFromClusters(const FWorldPartitionActorDesc* ActorDesc);

	void AddToPartition(FWorldPartitionActorDesc* ActorDesc);
	void RemoveFromPartition(FWorldPartitionActorDesc* ActorDesc, bool bRemoveDescriptorFromArray = true);

	TMap<FGuid, UWorldPartition::FActorCluster*> ActorToActorCluster;
	TSet<UWorldPartition::FActorCluster*> ActorClustersSet;
#endif

	UWorldPartitionStreamingPolicy* GetStreamingPolicy() const;
	friend class UWorldPartitionStreamingPolicy;
};