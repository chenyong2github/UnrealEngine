// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/CoreDelegates.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "PackageSourceControlHelper.h"
#include "CookPackageSplitter.h"
#endif

#include "WorldPartition.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartitionEditorCell;
class UWorldPartitionEditorHash;
class UWorldPartitionRuntimeCell;
class UWorldPartitionRuntimeHash;
class UWorldPartitionStreamingPolicy;
class IStreamingGenerationErrorHandler;
class FHLODActorDesc;
class UCanvas;

struct IWorldPartitionStreamingSourceProvider;

enum class EWorldPartitionRuntimeCellState : uint8;
enum class EWorldPartitionStreamingPerformance : uint8;

enum class EWorldPartitionInitState
{
	Uninitialized,
	Initializing,
	Initialized,
	Uninitializing
};

#define WORLDPARTITION_MAX UE_OLD_WORLD_MAX

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

public:
#if WITH_EDITOR
	static UWorldPartition* CreateOrRepairWorldPartition(AWorldSettings* WorldSettings, TSubclassOf<UWorldPartitionEditorHash> EditorHashClass = nullptr, TSubclassOf<UWorldPartitionRuntimeHash> RuntimeHashClass = nullptr);
	
	DECLARE_MULTICAST_DELEGATE_OneParam(FCancelWorldPartitionUpdateEditorCellsDelegate, UWorldPartition*);
	FCancelWorldPartitionUpdateEditorCellsDelegate OnCancelWorldPartitionUpdateEditorCells;
#endif

	DECLARE_MULTICAST_DELEGATE_OneParam(FWorldPartitionInitializeDelegate, UWorldPartition*);
	FWorldPartitionInitializeDelegate OnWorldPartitionInitialized;
	FWorldPartitionInitializeDelegate OnWorldPartitionUninitialized;

#if WITH_EDITOR
	TArray<FName> GetUserLoadedEditorGridCells() const;
private:

	void SavePerUserSettings();
		
	void OnGCPostReachabilityAnalysis();

	// PIE/Game Methods
	void OnPreBeginPIE(bool bStartSimulate);
	void OnPrePIEEnded(bool bWasSimulatingInEditor);
	void OnCancelPIE();
	void OnBeginPlay();
	void OnEndPlay();

	//~ Begin UActorDescContainer Interface
	virtual void OnWorldRenamed() override;

	virtual void OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc) override;
	virtual void OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc) override;
	virtual void OnActorDescUpdating(FWorldPartitionActorDesc* ActorDesc) override;
	virtual void OnActorDescUpdated(FWorldPartitionActorDesc* ActorDesc) override;

	virtual void OnActorDescRegistered(const FWorldPartitionActorDesc&) override;
	virtual void OnActorDescUnregistered(const FWorldPartitionActorDesc&) override;

	virtual bool GetInstancingContext(const FLinkerInstancingContext*& OutInstancingContext, FSoftObjectPathFixupArchive*& OutSoftObjectPathFixupArchive) const override;
	//~ End UActorDescContainer Interface
#endif

public:
	//~ Begin UObject Interface
	virtual bool ResolveSubobject(const TCHAR* SubObjectPath, UObject*& OutObject, bool bLoadIfExists) override;
	//~ End UObject Interface

#if WITH_EDITOR
	FName GetWorldPartitionEditorName() const;

	void LoadEditorCells(const FBox& Box, bool bIsFromUserChange);
	void LoadEditorCells(const TArray<FName>& CellNames, bool bIsFromUserChange);
	void UnloadEditorCells(const FBox& Box, bool bIsFromUserChange);
	bool AreEditorCellsLoaded(const FBox& Box);
	bool RefreshLoadedEditorCells(bool bIsFromUserChange);

	// PIE/Game/Cook Methods
	bool GenerateStreaming(TArray<FString>* OutPackagesToGenerate = nullptr);
	void RemapSoftObjectPath(FSoftObjectPath& ObjectPath);

	// Cook Methods
	bool PopulateGeneratedPackageForCook(UPackage* InPackage, const FString& InPackageRelativePath);
	bool FinalizeGeneratorPackageForCook(const TArray<ICookPackageSplitter::FGeneratedPackageForPreSave>& InGeneratedPackages);

	FBox GetWorldBounds() const;
	FBox GetEditorWorldBounds() const;
	void GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly);

	// Debugging Methods
	void DrawRuntimeHashPreview();
	void DumpActorDescs(const FString& Path);

	void CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const;
	static void CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler, const UActorDescContainer* ActorDescContainer);

	uint32 GetWantedEditorCellSize() const;
	void SetEditorWantedCellSize(uint32 InCellSize);

	// Actors pinning
	AActor* PinActor(const FGuid& ActorGuid);
	void UnpinActor(const FGuid& ActorGuid);
	bool IsActorPinned(const FGuid& ActorGuid) const;
#endif

public:
	static bool IsSimulating();

	void Initialize(UWorld* World, const FTransform& InTransform);
	bool IsInitialized() const;
	virtual void Uninitialize() override;

	void Tick(float DeltaSeconds);
	void UpdateStreamingState();
	bool CanAddLoadedLevelToWorld(class ULevel* InLevel) const;
	bool IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const;

	const TArray<FWorldPartitionStreamingSource>& GetStreamingSources() const;

	void RegisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);
	bool UnregisterStreamingSourceProvider(IWorldPartitionStreamingSourceProvider* StreamingSource);

	// Debugging Methods
	bool CanDrawRuntimeHash() const;
	void DrawRuntimeHash2D(UCanvas* Canvas, const FVector2D& PartitionCanvasSize, FVector2D& Offset);
	void DrawRuntimeHash3D();
	void DrawRuntimeCellsDetails(UCanvas* Canvas, FVector2D& Offset);
	void DrawStreamingStatusLegend(UCanvas* Canvas, FVector2D& Offset);

	EWorldPartitionStreamingPerformance GetStreamingPerformance() const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(DuplicateTransient)
	TObjectPtr<UWorldPartitionEditorHash> EditorHash;

	IWorldPartitionEditor* WorldPartitionEditor;

	/** Class of WorldPartitionStreamingPolicy to be used to manage world partition streaming. */
	UPROPERTY(EditAnywhere, Category=WorldPartition, NoClear, meta = (NoResetToDefault))
	TSubclassOf<UWorldPartitionStreamingPolicy> WorldPartitionStreamingPolicyClass;
#endif

	UPROPERTY(EditAnywhere, Category=WorldPartition, NoClear, meta = (NoResetToDefault), Instanced)
	TObjectPtr<UWorldPartitionRuntimeHash> RuntimeHash;

#if WITH_EDITOR
	bool bForceGarbageCollection;
	bool bForceGarbageCollectionPurge;
	bool bIsPIE;
#endif

#if WITH_EDITORONLY_DATA
	// Default HLOD layer
	UPROPERTY(EditAnywhere, Category=WorldPartition, meta = (DisplayName = "Default HLOD Layer"))
	TObjectPtr<class UHLODLayer> DefaultHLODLayer;

	TArray<FWorldPartitionReference> LoadedSubobjects;
#endif

private:
	EWorldPartitionInitState InitState;

	UPROPERTY()
	mutable TObjectPtr<UWorldPartitionStreamingPolicy> StreamingPolicy;

	TArray<IWorldPartitionStreamingSourceProvider*> StreamingSourceProviders;

#if WITH_EDITORONLY_DATA
	FLinkerInstancingContext InstancingContext;
	TUniquePtr<FSoftObjectPathFixupArchive> InstancingSoftObjectPathFixupArchive;

	FWorldPartitionReference WorldDataLayersActor;
#endif

	void OnWorldMatchStarting();

	void OnPostBugItGoCalled(const FVector& Loc, const FRotator& Rot);

	// Delegates registration
	void RegisterDelegates();
	void UnregisterDelegates();	

#if WITH_EDITOR
	void UpdateLoadingEditorCell(UWorldPartitionEditorCell* Cell, bool bShouldBeLoaded, bool bFromUserOperation);
	void HashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	void UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	bool ShouldActorBeLoadedByEditorCells(const FWorldPartitionActorDesc* ActorDesc) const;
	bool UpdateEditorCells(TFunctionRef<bool(TArray<UWorldPartitionEditorCell*>&)> GetCellsToProcess, bool bIsCellShouldBeLoaded, bool bIsFromUserChange);

	void ApplyActorTransform(AActor* Actor, const FTransform& InTransform);

	TMap<FGuid, FWorldPartitionReference> PinnedActors;
	TMap<FGuid, TMap<FGuid, FWorldPartitionReference>> PinnedActorRefs;
#endif

#if !UE_BUILD_SHIPPING
	void GetOnScreenMessages(FCoreDelegates::FSeverityMessageMap& OutMessages);

	class AWorldPartitionReplay* Replay;
#endif
	friend class AWorldPartitionReplay;
	friend class UWorldPartitionStreamingPolicy;
};