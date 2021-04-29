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

enum class EWorldPartitionRuntimeCellState : uint8;

DECLARE_LOG_CATEGORY_EXTERN(LogWorldPartition, Log, All);

enum class EWorldPartitionStreamingMode
{
	PIE,				// Used for PIE
	EditorStandalone,	// Used when running with -game
	Cook				// Used by cook commandlet
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
public:
	static UWorldPartition* CreateWorldPartition(AWorldSettings* WorldSettings, TSubclassOf<UWorldPartitionEditorHash> EditorHashClass = nullptr, TSubclassOf<UWorldPartitionRuntimeHash> RuntimeHashClass = nullptr);

private:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FEnableWorldPartitionEvent, UWorld*, bool);
	static FEnableWorldPartitionEvent EnableWorldPartitionEvent;

	DECLARE_MULTICAST_DELEGATE_OneParam(FWorldPartitionChangedEvent, UWorld*);
	static FWorldPartitionChangedEvent WorldPartitionChangedEvent;

	void SavePerUserSettings();
		
	void FlushStreaming();

	void OnGCPostReachabilityAnalysis();

	// PIE/Game Methods
	void OnPreBeginPIE(bool bStartSimulate);
	void OnPrePIEEnded(bool bWasSimulatingInEditor);
	void OnCancelPIE();
	void OnBeginPlay(EWorldPartitionStreamingMode Mode);
	void OnEndPlay();

	// UActorDescContainer events
	virtual void OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc) override;
	virtual void OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc) override;
	virtual void OnActorDescUpdating(FWorldPartitionActorDesc* ActorDesc) override;
	virtual void OnActorDescUpdated(FWorldPartitionActorDesc* ActorDesc) override;

	virtual void OnActorDescRegistered(const FWorldPartitionActorDesc&) override;
	virtual void OnActorDescUnregistered(const FWorldPartitionActorDesc&) override;
#endif

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	static void WorldPartitionOnLevelRemovedFromWorld(class ULevel* Level, UWorld* InWorld);

	// UWorldPartitionSubsystem interface+
	friend class UWorldPartitionSubsystem;

	const TArray<FWorldPartitionStreamingSource>& GetStreamingSources() const;
	// UActorPartitionSubsystem interface-

public:
	//~ Begin UObject Interface
	virtual bool ResolveSubobject(const TCHAR* SubObjectPath, UObject*& OutObject, bool bLoadIfExists) override;
	//~ End UObject Interface

#if WITH_EDITOR
	FName GetWorldPartitionEditorName();

	void LoadEditorCells(const FBox& Box);
	void UnloadEditorCells(const FBox& Box);
	bool AreEditorCellsLoaded(const FBox& Box);
	bool RefreshLoadedEditorCells();

	// PIE/Game/Cook Methods
	bool GenerateStreaming(EWorldPartitionStreamingMode Mode, TArray<FString>* OutPackagesToGenerate = nullptr);
	void RemapSoftObjectPath(FSoftObjectPath& ObjectPath);

	// Cook Methods
	bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath, const FString& InPackageCookName);
	void FinalizeGeneratedPackageForCook();

	FBox GetWorldBounds() const;
	FBox GetEditorWorldBounds() const;
	void GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly);
	void GenerateNavigationData();

	const UActorDescContainer* RegisterActorDescContainer(FName PackageName);

	// Debugging Methods
	void DrawRuntimeHashPreview();
	void DumpActorDescs(const FString& Path);

	void CheckForErrors() const;
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
	bool IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const;

	bool CanDrawRuntimeHash() const;
	FVector2D GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize);
	void DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize);
	void DrawRuntimeHash3D();

	void RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);
	bool UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);


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
	bool bIsPIE;
#endif

#if WITH_EDITORONLY_DATA
	// Default HLOD layer
	UPROPERTY(EditAnywhere, Category=HLOD)
	TObjectPtr<class UHLODLayer> DefaultHLODLayer;

	TArray<FWorldPartitionReference> LoadedSubobjects;

	TMap<FName, TWeakObjectPtr<UActorDescContainer>> ActorDescContainers;
#endif

private:
	EWorldPartitionInitState InitState;
	FTransform InstanceTransform;

	UPROPERTY()
	mutable TObjectPtr<UWorldPartitionStreamingPolicy> StreamingPolicy;

	TArray<IWorldPartitionStreamingSourceProvider*> StreamingSourceProviders;

#if WITH_EDITORONLY_DATA
	FLinkerInstancingContext InstancingContext;
#endif

	bool IsMainWorldPartition() const;
	void OnWorldBeginPlay();

	// Delegates registration
	void RegisterDelegates();
	void UnregisterDelegates();	

#if WITH_EDITOR
	void UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded);
	void HashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	void UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	bool ShouldActorBeLoaded(const FWorldPartitionActorDesc* ActorDesc) const;
	bool UpdateEditorCells(TFunctionRef<bool(TArray<UWorldPartitionEditorCell*>&)> GetCellsToProcess, bool bIsCellShouldBeLoaded);

	void ApplyActorTransform(AActor* Actor, const FTransform& InTransform);
#endif

	friend class UWorldPartitionStreamingPolicy;
};