// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/ActorDescContainer.h"

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

struct IWorldPartitionStreamingSourceProvider;

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
	virtual bool Save(UPackage* Package) const =0;
};
#endif

UCLASS(AutoExpandCategories=(WorldPartition))
class ENGINE_API UWorldPartition final : public UActorDescContainer
{
	GENERATED_UCLASS_BODY()

	friend class FWorldPartitionActorDesc;
	friend class UWorldPartitionEditorCell;
	friend class FWorldPartitionEditorModule;
	friend class FUnrealEdMisc;

#if WITH_EDITOR
private:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FEnableWorldPartitionEvent, UWorld*, bool);
	static FEnableWorldPartitionEvent EnableWorldPartitionEvent;

	DECLARE_MULTICAST_DELEGATE_OneParam(FWorldPartitionChangedEvent, UWorld*);
	static FWorldPartitionChangedEvent WorldPartitionChangedEvent;

	void FlushStreaming();

	// PIE Methods
	void OnPreBeginPIE(bool bStartSimulate);
	void OnEndPIE(bool bStartSimulate);

	// UActorDescContainer events
	virtual void OnActorDescAdded(const TUniquePtr<FWorldPartitionActorDesc>& NewActorDesc) override;
	virtual void OnActorDescRemoved(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) override;
	virtual void OnActorDescUpdating(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) override;
	virtual void OnActorDescUpdated(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) override;

	bool ShouldHandleAssetEvent(const FAssetData& InAssetData);
#endif

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	static void WorldPartitionOnLevelRemovedFromWorld(class ULevel* Level, UWorld* InWorld);

	// UWorldPartitionSubsystem interface+
	friend class UWorldPartitionSubsystem;
#if WITH_EDITOR
	void ForEachIntersectingActorDesc(const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const;
	void ForEachActorDesc(TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate) const;
#endif
	const TArray<FWorldPartitionStreamingSource>& GetStreamingSources() const;
	// UActorPartitionSubsystem interface-

#if WITH_EDITOR
public:
	//~ Begin UObject Interface
	virtual UObject* LoadSubobject(const TCHAR* SubObjectPath) override;
	//~ End UObject Interface

	FName GetWorldPartitionEditorName();

	void LoadEditorCells(const FBox& Box);
	void UnloadEditorCells(const FBox& Box);
	bool AreEditorCellsLoaded(const FBox& Box);
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
	FBox GetEditorWorldBounds() const;
	void GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly);
	void GenerateNavigationData();

	// Debugging Methods
	void DrawRuntimeHashPreview();
	void DumpActorDescs(const FString& Path);
#endif

public:
	static bool IsSimulating();

	void Initialize(UWorld* World, const FTransform& InTransform);
	bool IsInitialized() const;
	virtual void Uninitialize() override;

	void CleanupWorldPartition();

	const FTransform& GetInstanceTransform() const { return InstanceTransform; }

	void Tick(float DeltaSeconds);
	void UpdateStreamingState();
	class ULevel* GetPreferredLoadedLevelToAddToWorld() const;

	bool CanDrawRuntimeHash() const;
	FVector2D GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize);
	void DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize);
	void DrawRuntimeHash3D();

	virtual UWorld* GetWorld() const override;

	void RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);
	bool UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World;

#if WITH_EDITORONLY_DATA
	UPROPERTY(DuplicateTransient)
	TObjectPtr<UWorldPartitionEditorHash> EditorHash;

	IWorldPartitionEditor* WorldPartitionEditor;
#endif

	UPROPERTY(EditAnywhere, Category=RuntimeHash, NoClear, meta = (NoResetToDefault), Instanced)
	TObjectPtr<UWorldPartitionRuntimeHash> RuntimeHash;

#if WITH_EDITOR
	bool bForceGarbageCollection;
	bool bForceGarbageCollectionPurge;
#endif

#if WITH_EDITORONLY_DATA
	// Default HLOD layer
	UPROPERTY(EditAnywhere, Category=HLOD)
	TObjectPtr<class UHLODLayer> DefaultHLODLayer;

	TArray<FWorldPartitionReference> LoadedSubobjects;
#endif

private:
	EWorldPartitionInitState InitState;
	FTransform InstanceTransform;

	UPROPERTY(Transient, DuplicateTransient)
	mutable TObjectPtr<UWorldPartitionStreamingPolicy> StreamingPolicy;

	TArray<IWorldPartitionStreamingSourceProvider*> StreamingSourceProviders;

#if WITH_EDITORONLY_DATA
	FLinkerInstancingContext InstancingContext;
#endif

	bool IsMainWorldPartition() const;
		
#if WITH_EDITOR
	// Delegates registration
	virtual void RegisterDelegates() override;
	virtual void UnregisterDelegates() override;

	void UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded);
	void HashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	void UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	bool ShouldActorBeLoaded(const FWorldPartitionActorDesc* ActorDesc) const;
	bool UpdateEditorCells(TFunctionRef<bool(TArray<UWorldPartitionEditorCell*>&)> GetCellsToProcess, bool bIsCellShouldBeLoaded);
#endif

	UWorldPartitionStreamingPolicy* GetStreamingPolicy() const;
	friend class UWorldPartitionStreamingPolicy;
};