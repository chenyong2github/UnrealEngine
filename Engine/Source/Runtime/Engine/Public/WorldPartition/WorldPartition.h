// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescFactory.h"

#if WITH_EDITOR
#include "PackageSourceControlHelper.h"
#endif

#include "WorldPartition.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartitionEditorCell;
class UWorldPartitionEditorHash;
class UWorldPartitionRuntimeCell;
class UWorldPartitionRuntimeHash;
class UWorldPartitionStreamingPolicy;
class FHLODActorDesc;

DECLARE_LOG_CATEGORY_EXTERN(LogWorldPartition, Log, All);

enum class EWorldPartitionStreamingMode
{
	PIE,
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

class ENGINE_API ISourceControlHelper
{
public:
	virtual FString GetFilename(const FString& PackageName) const =0;
	virtual FString GetFilename(UPackage* Package) const =0;
	virtual bool Checkout(UPackage* Package) const =0;
	virtual bool Add(UPackage* Package) const =0;
	virtual bool Delete(const FString& PackageName) const =0;
	virtual bool Delete(UPackage* Package) const =0;
};

struct ENGINE_API FHLODGenerationContext
{
	TMap<uint64, FGuid> HLODActorDescs;
	
	// Everything needed to build the cell hash
	int64 GridIndexX;
	int64 GridIndexY;
	int64 GridIndexZ;
	FName HLODLayerName;
};
#endif

UCLASS(AutoExpandCategories=(WorldPartition))
class ENGINE_API UWorldPartition final : public UObject
{
	GENERATED_UCLASS_BODY()

	friend class UWorldPartitionEditorCell;
	friend class FWorldPartitionEditorModule;
	friend class FUnrealEdMisc;

#if WITH_EDITOR
private:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FEnableWorldPartitionEvent, UWorld*, bool);
	static FEnableWorldPartitionEvent EnableWorldPartitionEvent;

	DECLARE_MULTICAST_DELEGATE_OneParam(FWorldPartitionChangedEvent, UWorld*);
	static FWorldPartitionChangedEvent WorldPartitionChangedEvent;

	static FWorldPartitionActorDescFactory* GetActorDescFactory(TSubclassOf<AActor> Class);
	static FWorldPartitionActorDescFactory* GetActorDescFactory(const AActor* Actor);

	void FlushStreaming();

	// PIE Methods
	void OnPreBeginPIE(bool bStartSimulate);
	void OnEndPIE(bool bStartSimulate);

	// Asset registry events
	void OnAssetAdded(const FAssetData& InAssetData);
	void OnAssetRemoved(const FAssetData& InAssetData);
	void OnAssetUpdated(const FAssetData& InAssetData);

	bool ShouldHandleAssetEvent(const FAssetData& InAssetData);
	TUniquePtr<FWorldPartitionActorDesc> GetActorDescriptor(const FAssetData& InAssetData);

	void ApplyActorTransform(AActor* InActor, const FTransform& InTransform);
#endif

	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;

	static void WorldPartitionOnLevelRemovedFromWorld(class ULevel* Level, UWorld* InWorld);

#if WITH_EDITOR
	// UWorldPartitionSubsystem interface+
	friend class UWorldPartitionSubsystem;
	void ForEachIntersectingActorDesc(const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const;
	void ForEachActorDesc(TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const;
	// UActorPartitionSubsystem interface-
public:
	static void RegisterActorDescFactory(TSubclassOf<AActor> Class, FWorldPartitionActorDescFactory* Factory);

	FName GetWorldPartitionEditorName();
	bool IsSimulating() const;

	FWorldPartitionActorDesc* GetActorDesc(const FGuid& Guid);
	const FWorldPartitionActorDesc* GetActorDesc(const FGuid& Guid) const;

	static TUniquePtr<FWorldPartitionActorDesc> CreateActorDesc(const AActor* Actor);

	void LoadEditorCells(const FBox& Box);
	void UnloadEditorCells(const FBox& Box);
	bool RefreshLoadedEditorCells();

	// PIE Methods
	void PrepareForPIE();
	void CleanupForPIE();
	void OnPreFixupForPIE(int32 InPIEInstanceID, FSoftObjectPath& ObjectPath);

	// PIE/Cook Methods
	bool GenerateStreaming(EWorldPartitionStreamingMode Mode, TArray<FString>* OutPackagesToGenerate = nullptr);

	// Cook Methods
	bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, const FString& InPackageCookName);
	void FinalizeGeneratedPackageForCook();

	FBox GetWorldBounds() const;
	void GenerateHLOD(ISourceControlHelper* SourceControlHelper);
	void GenerateNavigationData();

	void DumpActorDescs(const FString& Path);
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
	
	static TUniquePtr<FWorldPartitionActorDescFactory> DefaultActorDescFactory;
	static TMap<FName, FWorldPartitionActorDescFactory*> ActorDescFactories;

	DECLARE_EVENT_TwoParams(UWorldPartition, FWorldPartitionActorRegisteredEvent, AActor&, bool);
	FWorldPartitionActorRegisteredEvent OnActorRegisteredEvent;

	bool bIgnoreAssetRegistryEvents;
	bool bForceGarbageCollection;
	bool bForceGarbageCollectionPurge;
#endif

#if WITH_EDITORONLY_DATA
	// Default HLOD layer
	UPROPERTY(EditAnywhere, Category=HLOD)
	class UHLODLayer* DefaultHLODLayer;
#endif

private:
	EWorldPartitionInitState InitState;
	FTransform InstanceTransform;

	UPROPERTY(Transient, DuplicateTransient)
	mutable UWorldPartitionStreamingPolicy* StreamingPolicy;

#if WITH_EDITORONLY_DATA
	FLinkerInstancingContext InstancingContext;
#endif

	// Actor registration
	AActor* RegisterActor(FWorldPartitionActorDesc* ActorDesc);
	void UnregisterActor(AActor* Actor);

	bool IsMainWorldPartition() const;

	// Delegates registration
	void RegisterDelegates();
	void UnregisterDelegates();

#if WITH_EDITOR
	void UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded);
	void HashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	void UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	bool ShouldActorBeLoaded(const FWorldPartitionActorDesc* ActorDesc) const;
	bool UpdateEditorCells(TFunctionRef<bool(TArray<UWorldPartitionEditorCell*>&)> GetCellsToProcess, bool bIsCellShouldBeLoaded);
#endif

	UWorldPartitionStreamingPolicy* GetStreamingPolicy() const;
	friend class UWorldPartitionStreamingPolicy;
};