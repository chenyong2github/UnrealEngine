// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Optional.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "PackageSourceControlHelper.h"
#include "CookPackageSplitter.h"
#endif

#include "WorldPartition.generated.h"

class FWorldPartitionActorDesc;
class UWorldPartitionEditorHash;
class UWorldPartitionRuntimeCell;
class UWorldPartitionRuntimeHash;
class UWorldPartitionStreamingPolicy;
class IStreamingGenerationErrorHandler;
class FLoaderAdapterAlwaysLoadedActors;
class FLoaderAdapterPinnedActors;
class FHLODActorDesc;
class UCanvas;
class ULevel;

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
	virtual void Reconstruct() {}
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
	friend class FWorldPartitionConverter;
	friend class UWorldPartitionConvertCommandlet;
	friend class FWorldPartitionEditorModule;
	friend class FWorldPartitionDetails;
	friend class FUnrealEdMisc;

public:
#if WITH_EDITOR
	static UWorldPartition* CreateOrRepairWorldPartition(AWorldSettings* WorldSettings, TSubclassOf<UWorldPartitionEditorHash> EditorHashClass = nullptr, TSubclassOf<UWorldPartitionRuntimeHash> RuntimeHashClass = nullptr);
#endif

	DECLARE_MULTICAST_DELEGATE_OneParam(FWorldPartitionInitializeDelegate, UWorldPartition*);
	
	UE_DEPRECATED(5.1, "Please use FWorldPartitionInitializedEvent& UWorld::OnWorldPartitionInitialized() instead.")
	FWorldPartitionInitializeDelegate OnWorldPartitionInitialized;

	UE_DEPRECATED(5.1, "Please use FWorldPartitionInitializedEvent& UWorld::OnWorldPartitionUninitialized() instead.")
	FWorldPartitionInitializeDelegate OnWorldPartitionUninitialized;

#if WITH_EDITOR
	TArray<FBox> GetUserLoadedEditorRegions() const;

public:
	bool SupportsStreaming() const;
	bool IsStreamingEnabled() const;
	void SetEnableStreaming(bool bInEnableStreaming);
	bool CanBeUsedByLevelInstance() const;
	void SetCanBeUsedByLevelInstance(bool bInCanBeUsedByLevelInstance);
	void OnEnableStreamingChanged();

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
	static void CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler, const UActorDescContainer* ActorDescContainer, bool bEnableStreaming);

	void AppendAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const;

	uint32 GetWantedEditorCellSize() const;
	void SetEditorWantedCellSize(uint32 InCellSize);

	// Actors pinning
	AActor* PinActor(const FGuid& ActorGuid);
	void UnpinActor(const FGuid& ActorGuid);
	bool IsActorPinned(const FGuid& ActorGuid) const;

	void LoadLastLoadedRegions(const TArray<FBox>& EditorLastLoadedRegions);
	void LoadLastLoadedRegions();
#endif

public:
	static bool IsSimulating();

	void Initialize(UWorld* World, const FTransform& InTransform);
	bool IsInitialized() const;
	virtual void Uninitialize() override;

	bool CanStream() const;
	bool IsMainWorldPartition() const;
	const FTransform& GetInstanceTransform() const;
	const FTransform* GetInstanceTransformPtr() const;

	void Tick(float DeltaSeconds);
	void UpdateStreamingState();
	bool CanAddLoadedLevelToWorld(ULevel* InLevel) const;
	bool IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const;

	const TArray<FWorldPartitionStreamingSource>& GetStreamingSources() const;

	// Debugging Methods
	bool CanDrawRuntimeHash() const;
	FVector2D GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize);
	void DrawRuntimeHash2D(UCanvas* Canvas, const FVector2D& PartitionCanvasSize, const FVector2D& Offset);
	void DrawRuntimeHash3D();
	void DrawRuntimeCellsDetails(UCanvas* Canvas, FVector2D& Offset);
	void DrawStreamingStatusLegend(UCanvas* Canvas, FVector2D& Offset);

	EWorldPartitionStreamingPerformance GetStreamingPerformance() const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(DuplicateTransient)
	TObjectPtr<UWorldPartitionEditorHash> EditorHash;

	FLoaderAdapterAlwaysLoadedActors* AlwaysLoadedActors;
	FLoaderAdapterPinnedActors* PinnedActors;

	IWorldPartitionEditor* WorldPartitionEditor;

private:
	/** Class of WorldPartitionStreamingPolicy to be used to manage world partition streaming. */
	UPROPERTY()
	TSubclassOf<UWorldPartitionStreamingPolicy> WorldPartitionStreamingPolicyClass;

	/** Enables streaming for this world. */
	UPROPERTY()
	bool bEnableStreaming;

	/** Used to know if it's the first time streaming is enabled on this world. */
	UPROPERTY()
	bool bStreamingWasEnabled;

	/** Used to know if the user has already been warned about that it should enable streaming based on world size. */
	UPROPERTY()
	bool bShouldEnableStreamingWarned;

	/** Used to know if we need to recheck if the user should enable streaming based on world size. */
	bool bShouldCheckEnableStreamingWarning;

	/** Whether Level Instance can reference this partition. */
	UPROPERTY(EditAnywhere, Category = "WorldPartition", AdvancedDisplay, meta = (EditConditionHides, HideEditConditionToggle, EditCondition = "!bEnableStreaming"))
	bool bCanBeUsedByLevelInstance;
#endif

public:
	UPROPERTY()
	TObjectPtr<UWorldPartitionRuntimeHash> RuntimeHash;

#if WITH_EDITOR
private:
	bool bForceGarbageCollection;
	bool bForceGarbageCollectionPurge;
	bool bIsPIE;
#endif

#if WITH_EDITORONLY_DATA
public:
	// Default HLOD layer
	UPROPERTY(EditAnywhere, Category=WorldPartition, meta = (DisplayName = "Default HLOD Layer", EditCondition="bEnableStreaming", EditConditionHides, HideEditConditionToggle))
	TObjectPtr<class UHLODLayer> DefaultHLODLayer;

	TArray<FWorldPartitionReference> LoadedSubobjects;
#endif

private:
	EWorldPartitionInitState InitState;
	TOptional<FTransform> InstanceTransform;

	UPROPERTY()
	mutable TObjectPtr<UWorldPartitionStreamingPolicy> StreamingPolicy;

#if WITH_EDITORONLY_DATA
	FLinkerInstancingContext InstancingContext;
	TUniquePtr<FSoftObjectPathFixupArchive> InstancingSoftObjectPathFixupArchive;

	FWorldPartitionReference WorldDataLayersActor;
#endif

	void OnWorldMatchStarting();
	void OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld);
	void OnPostBugItGoCalled(const FVector& Loc, const FRotator& Rot);

	// Delegates registration
	void RegisterDelegates();
	void UnregisterDelegates();	

#if WITH_EDITOR
	void HashActorDesc(FWorldPartitionActorDesc* ActorDesc);
	void UnhashActorDesc(FWorldPartitionActorDesc* ActorDesc);

public:
	// Editor loader adapters management
	template <typename T, typename... ArgsType>
	T* CreateEditorLoaderAdapter(ArgsType&&... Args)
	{
		T* LoaderAdapter = new T(Forward<ArgsType>(Args)...);
		RegisteredEditorLoaderAdapters.Add(LoaderAdapter);
		return LoaderAdapter;
	}

	void ReleaseEditorLoaderAdapter(IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
	{
		verify(RegisteredEditorLoaderAdapters.Remove(LoaderAdapter) != INDEX_NONE);
		delete LoaderAdapter;
	}

	const TSet<IWorldPartitionActorLoaderInterface::ILoaderAdapter*>& GetRegisteredEditorLoaderAdapters() const
	{
		return RegisteredEditorLoaderAdapters;
	}

private:
	TSet<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> RegisteredEditorLoaderAdapters;
#endif

#if !UE_BUILD_SHIPPING
	void GetOnScreenMessages(FCoreDelegates::FSeverityMessageMap& OutMessages);

	class AWorldPartitionReplay* Replay;
#endif
	friend class AWorldPartitionReplay;
	friend class UWorldPartitionStreamingPolicy;
};