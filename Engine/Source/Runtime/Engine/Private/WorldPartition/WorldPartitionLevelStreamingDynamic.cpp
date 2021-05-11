// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Model.h"
#include "ContentStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "UnrealEngine.h"
#include "Editor/FixupLazyObjectPtrForPIEArchive.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#endif

#define LOCTEXT_NAMESPACE "World"

/*-----------------------------------------------------------------------------
	UWorldPartitionLevelStreamingDynamic
-----------------------------------------------------------------------------*/

UWorldPartitionLevelStreamingDynamic::UWorldPartitionLevelStreamingDynamic(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, RuntimeLevel(nullptr)
	, bLoadRequestInProgress(false)
#endif
	, bShouldBeAlwaysLoaded(false)
{
}

#if WITH_EDITOR

/**
 * Initializes a UWorldPartitionLevelStreamingDynamic.
 */
void UWorldPartitionLevelStreamingDynamic::Initialize(const UWorldPartitionRuntimeLevelStreamingCell& InCell)
{
	StreamingCell = &InCell;
	UWorld* World = GetWorld();
	check(!ShouldBeLoaded());
	check((World->IsGameWorld() && !ShouldBeVisible()) || (!World->IsGameWorld() && !GetShouldBeVisibleFlag()));
	check(ChildPackages.Num() == 0);
	check(!WorldAsset.IsNull());

	bShouldBeAlwaysLoaded = InCell.IsAlwaysLoaded();
	StreamingPriority = InCell.GetStreamingPriority();
	ChildPackages = InCell.GetPackages();
	ActorContainer = InCell.ActorContainer;

	UWorld* OuterWorld = InCell.GetOuterUWorldPartition()->GetTypedOuter<UWorld>();
	OriginalLevelPackageName = OuterWorld->GetPackage()->GetLoadedPath().GetPackageFName();
	PackageNameToLoad = GetWorldAssetPackageFName();
	OuterWorldPartition = OuterWorld->GetWorldPartition();	
}

/**
 Custom destroy (delegate removal)
 */
void UWorldPartitionLevelStreamingDynamic::BeginDestroy()
{
	if (IsValid(RuntimeLevel))
	{
		RuntimeLevel->OnCleanupLevel.Remove(OnCleanupLevelDelegateHandle);
	}
	Super::BeginDestroy();
}

/**
 * Creates a runtime level that we will use to emulate Level streaming
 */
void UWorldPartitionLevelStreamingDynamic::CreateRuntimeLevel()
{
	check(PendingUnloadLevel == nullptr);
	check(RuntimeLevel == nullptr);
	UWorld* PlayWorld = GetWorld();
	check(PlayWorld && PlayWorld->IsGameWorld());

	// Create streaming cell Level package
	RuntimeLevel = FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(PlayWorld, GetWorldAsset().ToString());
	check(RuntimeLevel);

	// Attach ourself to Level cleanup to do our own cleanup
	OnCleanupLevelDelegateHandle = RuntimeLevel->OnCleanupLevel.AddUObject(this, &UWorldPartitionLevelStreamingDynamic::OnCleanupLevel);
}

/**
 * Overrides default StreamingLevel behavior and manually load actors and add them to the runtime Level
 */
bool UWorldPartitionLevelStreamingDynamic::RequestLevel(UWorld* InPersistentWorld, bool bInAllowLevelLoadRequests, EReqLevelBlock InBlockPolicy)
{
	// Quit early in case load request already issued
	if (GetCurrentState() == ECurrentState::Loading)
	{
		return true;
	}

	// Previous attempts have failed, no reason to try again
	if (GetCurrentState() == ECurrentState::FailedToLoad)
	{
		return false;
	}

	// Check if currently loaded level is what we want right now
	if (LoadedLevel)
	{
		check(GetLoadedLevelPackageName() == GetWorldAssetPackageFName());
		return true;
	}

	// Can not load new level now, there is still level pending unload
	if (PendingUnloadLevel)
	{
		return false;
	}

	// Can not load new level now either, we're still processing visibility for this one
	ULevel* PendingLevelVisOrInvis = (InPersistentWorld->GetCurrentLevelPendingVisibility() ? InPersistentWorld->GetCurrentLevelPendingVisibility() : InPersistentWorld->GetCurrentLevelPendingInvisibility());
	if (PendingLevelVisOrInvis && PendingLevelVisOrInvis == LoadedLevel)
	{
		UE_LOG(LogLevelStreaming, Verbose, TEXT("Delaying load of new level %s, because still processing visibility request."), *GetWorldAssetPackageName());
		return false;
	}

	QUICK_SCOPE_CYCLE_COUNTER(STAT_ULevelStreaming_RequestLevel);
	FScopeCycleCounterUObject Context(InPersistentWorld);

	// Try to find the package to load
	const FName DesiredPackageName = GetWorldAssetPackageFName();
	UPackage* LevelPackage = (UPackage*)StaticFindObjectFast(UPackage::StaticClass(), nullptr, DesiredPackageName, 0, 0, RF_NoFlags, EInternalObjectFlags::PendingKill);
	UWorld* FoundWorld = LevelPackage ? UWorld::FindWorldInPackage(LevelPackage) : nullptr;
	check(!FoundWorld || !FoundWorld->IsPendingKill());
	check(!FoundWorld || FoundWorld->PersistentLevel);
	if (FoundWorld && FoundWorld->PersistentLevel != RuntimeLevel)
	{
		check(RuntimeLevel == nullptr);
		check(LoadedLevel == nullptr);
		RuntimeLevel = FoundWorld->PersistentLevel;
	}

	if (RuntimeLevel)
	{
		// Reuse existing Level
		UPackage* CellLevelPackage = RuntimeLevel->GetPackage();
		check(CellLevelPackage);
		UWorld* CellWorld = UWorld::FindWorldInPackage(CellLevelPackage);
		check(CellWorld);
		check(CellWorld == FoundWorld);
		check(!CellWorld->IsPendingKill());
		check(CellWorld->PersistentLevel == RuntimeLevel);
		check(CellWorld->PersistentLevel != LoadedLevel);

		// Level already exists but may have the wrong type due to being inactive before, so copy data over
		check(InPersistentWorld->IsGameWorld());
		CellWorld->WorldType = InPersistentWorld->WorldType;
		CellWorld->PersistentLevel->OwningWorld = InPersistentWorld;

		SetLoadedLevel(RuntimeLevel);

		// Broadcast level loaded event to blueprints
		OnLevelLoaded.Broadcast();
	}
	else if (bInAllowLevelLoadRequests)
	{
		// LODPackages not supported in this mode
		check(LODPackageNames.Num() == 0);
		if (RuntimeLevel == nullptr)
		{
			check(GetCurrentState() == ECurrentState::Unloaded);

			check(!RuntimeLevel);
			CreateRuntimeLevel();
			check(RuntimeLevel);

			UPackage* CellLevelPackage = RuntimeLevel->GetPackage();
			check(CellLevelPackage);
			check(UWorld::FindWorldInPackage(CellLevelPackage));
			check(RuntimeLevel->OwningWorld);
			check(RuntimeLevel->OwningWorld->WorldType == EWorldType::PIE || (IsRunningGame() && RuntimeLevel->OwningWorld->WorldType == EWorldType::Game));
	
			if (IssueLoadRequests())
			{
				// Editor immediately blocks on load and we also block if background level streaming is disabled.
				if (InBlockPolicy == AlwaysBlock || (ShouldBeAlwaysLoaded() && InBlockPolicy != NeverBlock))
				{
					if (IsAsyncLoading())
					{
						UE_LOG(LogStreaming, Display, TEXT("UWorldPartitionLevelStreamingDynamic::RequestLevel(%s) is flushing async loading"), *GetWorldAssetPackageName());
					}

					// Finish all async loading.
					FlushAsyncLoading();
				}
				else
				{
					CurrentState = ECurrentState::Loading;
				}
			}
		}
	}

	return true;
}

/**
 * Loads all objects of a runtime Level
 */
bool UWorldPartitionLevelStreamingDynamic::IssueLoadRequests()
{
	check(ShouldBeLoaded());
	check(!HasLoadedLevel());
	check(RuntimeLevel);
	check(!bLoadRequestInProgress);

	bLoadRequestInProgress = true;
	FLinkerInstancingContext InstancingContext;
	UPackage* RuntimePackage = RuntimeLevel->GetPackage();
	InstancingContext.AddMapping(OriginalLevelPackageName, RuntimePackage->GetFName());

	ChildPackagesToLoad.Reset(ChildPackages.Num());

	UWorld* World = GetWorld();
	for (FWorldPartitionRuntimeCellObjectMapping& ChildPackage : ChildPackages)
	{
		bool bNeedDup = false;
		if (ChildPackage.ContainerID == 0)
		{
			if (ActorContainer)
			{
				FString SubObjectName;
				FString SubObjectContext;
				if (ChildPackage.LoadedPath.ToString().Split(TEXT("."), &SubObjectContext, &SubObjectName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
				{
					if (AActor* ActorModifiedForPIE = ActorContainer->Actors.FindRef(*SubObjectName))
					{
						bNeedDup = true;
					}
				}
			}
		}

		if (!bNeedDup)
		{
			ChildPackagesToLoad.Add(ChildPackage);
		}
	}

	// Duplicate unsaved actors
	if (ActorContainer)
	{
		FObjectDuplicationParameters Parameters(ActorContainer, RuntimeLevel);
		Parameters.DestClass = ActorContainer->GetClass();
		Parameters.FlagMask = RF_AllFlags & ~(RF_MarkAsRootSet | RF_MarkAsNative | RF_HasExternalPackage);
		Parameters.InternalFlagMask = EInternalObjectFlags::AllFlags;
		Parameters.DuplicateMode = EDuplicateMode::PIE;
		Parameters.PortFlags = PPF_DuplicateForPIE;
		Parameters.DuplicationSeed.Add(World->PersistentLevel, RuntimeLevel);

		UActorContainer* ActorContainerDup = (UActorContainer*)StaticDuplicateObjectEx(Parameters);

		// Add the duplicated actors to the corresponding cell level
		for (auto& ActorPair : ActorContainerDup->Actors)
		{
			ActorPair.Value->Rename(nullptr, RuntimeLevel);
		}

		ActorContainerDup->MarkPendingKill();
	}

	auto FinalizeLoading = [this]()
	{
		check(bLoadRequestInProgress);
		bLoadRequestInProgress = false;
		FinalizeRuntimeLevel();
	};

	// Load saved actors
	if (ChildPackagesToLoad.Num())
	{
		FWorldPartitionLevelHelper::LoadActors(RuntimeLevel, ChildPackagesToLoad, PackageCache, [this, FinalizeLoading](bool bSucceeded)
		{
			if (!bSucceeded)
			{
				UE_LOG(LogLevelStreaming, Warning, TEXT("UWorldPartitionLevelStreamingDynamic::IssueLoadRequests failed %s"), *GetWorldAssetPackageName());
			}

			FinalizeLoading();
		},/*bLoadForPlay=*/true, /*bLoadAsync=*/true, &InstancingContext);
	}
	else
	{
		FinalizeLoading();
	}

	return bLoadRequestInProgress;
}

void UWorldPartitionLevelStreamingDynamic::FinalizeRuntimeLevel()
{
	check(!HasLoadedLevel());
	check(RuntimeLevel);
	check(!bLoadRequestInProgress);

	// For RuntimeLevel's world NetGUID to be valid, make sure to flag bIsNameStableForNetworking so that IsNameStableForNetworking() returns true. (see FNetGUIDCache::SupportsObject)
	UWorld* OuterWorld = RuntimeLevel->GetTypedOuter<UWorld>();
	OuterWorld->bIsNameStableForNetworking = true;

	UPackage* RuntimePackage = RuntimeLevel->GetPackage();
	RuntimePackage->MarkAsFullyLoaded();

	if (OuterWorld->IsPlayInEditor())
	{
		int32 PIEInstanceID = GetPackage()->PIEInstanceID;
		check(PIEInstanceID != INDEX_NONE);

		// PIE Fixup LazyObjectPtrs
		FTemporaryPlayInEditorIDOverride SetPlayInEditorID(PIEInstanceID);
		FFixupLazyObjectPtrForPIEArchive FixupLazyPointersAr;
		FixupLazyPointersAr << RuntimeLevel;

		// PIE Fixup SoftObjectPaths
		RuntimeLevel->FixupForPIE(PIEInstanceID, [&](int32 InPIEInstanceID, FSoftObjectPath& ObjectPath)
		{
			// Remap Runtime Level's SoftObjectPath before each PIE Fixup to avoid doing 2 passes of serialization
			OuterWorldPartition->RemapSoftObjectPath(ObjectPath);
		});
	}
	else if (OuterWorld->IsGameWorld())
	{
		check(IsRunningGame());
		// Remap Runtime Level's SoftObjectPaths
		FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(RuntimeLevel, OuterWorldPartition.Get());
	}

	SetLoadedLevel(RuntimeLevel);

	// Broadcast level loaded event to blueprints
	OnLevelLoaded.Broadcast();

	RuntimeLevel->HandleLegacyMapBuildData();

	// Notify the streamer to start building incrementally the level streaming data.
	IStreamingManager::Get().AddLevel(RuntimeLevel);

	// Make sure this level will start to render only when it will be fully added to the world
	check(ShouldRequireFullVisibilityToRender());
	RuntimeLevel->bRequireFullVisibilityToRender = true;
}

/**
 * Called by ULevel::CleanupLevel (which is callbed by FLevelStreamingGCHelper::PrepareStreamedOutLevelsForGC for this class)
 */
void UWorldPartitionLevelStreamingDynamic::OnCleanupLevel()
{
	if (RuntimeLevel)
	{
		PackageCache.UnloadPackages();

		RuntimeLevel->OnCleanupLevel.Remove(OnCleanupLevelDelegateHandle);

		auto TrashPackage = [](UPackage* Package)
		{
			// Clears RF_Standalone flag on objects in package (UMetaData)
			ForEachObjectWithPackage(Package, [](UObject* Object)
			{
				Object->ClearFlags(RF_Standalone);
				return true;
			}, false);

			// Rename package to avoid having to deal with pending kill objects in subsequent RequestLevel calls
			FName NewPackageName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), FName(*FString::Printf(TEXT("%s_Trashed"), *Package->GetName())));
			Package->Rename(*NewPackageName.ToString(), nullptr, REN_ForceNoResetLoaders | REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		};
		
		TrashPackage(RuntimeLevel->GetPackage());
		for (AActor* Actor : RuntimeLevel->Actors)
		{
			if (UPackage* ActorPackage = Actor ? Actor->GetExternalPackage() : nullptr)
			{
				TrashPackage(ActorPackage);
			}
		}

		RuntimeLevel = nullptr;
	}
}
#endif

/*
 * Load StreamingLevel without adding it to world 
 */
void UWorldPartitionLevelStreamingDynamic::Load()
{
	UE_LOG(LogLevelStreaming, Verbose, TEXT("UWorldPartitionLevelStreamingDynamic::Loading %s"), *GetWorldAssetPackageName());

	check(!ShouldBeLoaded());
	
	SetShouldBeLoaded(true);
	SetShouldBeVisible(false);
	SetIsRequestingUnloadAndRemoval(false);

	UWorld* PlayWorld = GetWorld();
	check(PlayWorld && PlayWorld->IsGameWorld());
	PlayWorld->AddUniqueStreamingLevel(this);
}

/*
 * Unload StreamingLevel
 */
void UWorldPartitionLevelStreamingDynamic::Unload()
{
	UE_LOG(LogLevelStreaming, Verbose, TEXT("UWorldPartitionLevelStreamingDynamic::Unloading %s"), *GetWorldAssetPackageName());

	check(ShouldBeLoaded());

	SetShouldBeLoaded(false);
	SetShouldBeVisible(false);
	SetIsRequestingUnloadAndRemoval(true);
}

/**
  * Activates StreamingLevel by making sure it's in the World's StreamingLevels and that it should be loaded & visible
  */
void UWorldPartitionLevelStreamingDynamic::Activate()
{
	UE_LOG(LogLevelStreaming, Verbose, TEXT("UWorldPartitionLevelStreamingDynamic::Activating %s"), *GetWorldAssetPackageName());

	check(!ShouldBeVisible());

	// Make sure we are in the correct state
	SetShouldBeLoaded(true);
	SetShouldBeVisible(true);
	SetIsRequestingUnloadAndRemoval(false);

	// Add ourself to the list of Streaming Level of the world
	UWorld* PlayWorld = GetWorld();
	check(PlayWorld && PlayWorld->IsGameWorld());
	PlayWorld->AddUniqueStreamingLevel(this);
}

/**
 * Deactivates StreamingLevel (Remove from world, keep loaded)
 */
void UWorldPartitionLevelStreamingDynamic::Deactivate()
{
	UE_LOG(LogLevelStreaming, Verbose, TEXT("UWorldPartitionLevelStreamingDynamic::Deactivating %s"), *GetWorldAssetPackageName());

	check(ShouldBeLoaded());
	check(ShouldBeVisible());

	SetShouldBeVisible(false);
}

UWorld* UWorldPartitionLevelStreamingDynamic::GetOuterWorld() const
{
	check(OuterWorldPartition.IsValid());
	return OuterWorldPartition->GetTypedOuter<UWorld>();
}

#undef LOCTEXT_NAMESPACE