// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionLevelHelper implementation
 */

#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "WorldPartition/WorldPartitionPackageCache.h"

#if WITH_EDITOR

#include "FileHelpers.h"
#include "Model.h"
#include "UnrealEngine.h"
#include "UObject/UObjectHash.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartition.h"
#include "StaticMeshCompiler.h"
#include "LevelUtils.h"
#include "Templates/SharedPointer.h"

 /**
  * Defaults World's initialization values for World Partition StreamingLevels
  */
UWorld::InitializationValues FWorldPartitionLevelHelper::GetWorldInitializationValues()
{
	return UWorld::InitializationValues()
		.InitializeScenes(false)
		.AllowAudioPlayback(false)
		.RequiresHitProxies(false)
		.CreatePhysicsScene(false)
		.CreateNavigation(false)
		.CreateAISystem(false)
		.ShouldSimulatePhysics(false)
		.EnableTraceCollision(false)
		.SetTransactional(false)
		.CreateFXSystem(false);
}

/**
 * Moves external actors into the given level
 */
void FWorldPartitionLevelHelper::MoveExternalActorsToLevel(const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages, ULevel* InLevel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionLevelHelper::MoveExternalActorsToLevel);

	// We can't have async compilation still going on while we move actors as this is going to ResetLoaders which will move bulkdata around that
	// might still be used by async compilation. 
	// #TODO_DC Revisit once virtualbulkdata are enabled
	FStaticMeshCompilingManager::Get().FinishAllCompilation();

	check(InLevel);
	UPackage* LevelPackage = InLevel->GetPackage();
	
	// Move all actors to Cell level
	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InChildPackages)
	{
		// We assume actor failed to duplicate if LoadedPath equals NAME_None (warning already logged we can skip this mapping)
		if (PackageObjectMapping.LoadedPath == NAME_None && !PackageObjectMapping.ContainerID.IsMainContainer())
		{
			continue;
		}

		AActor* Actor = FindObject<AActor>(nullptr, *PackageObjectMapping.LoadedPath.ToString());
		if (Actor)
		{
			UPackage* ActorExternalPackage = Actor->IsPackageExternal() ? Actor->GetPackage() : nullptr;

			const bool bSameOuter = (InLevel == Actor->GetOuter());
			Actor->SetPackageExternal(false, false);
						
			// Avoid calling Rename on the actor if it's already outered to InLevel as this will cause it's name to be changed. 
			// (UObject::Rename doesn't check if Rename is being called with existing outer and assigns new name)
			if (!bSameOuter)
			{
				Actor->Rename(nullptr, InLevel, REN_ForceNoResetLoaders);
			}
			
			check(Actor->GetPackage() == LevelPackage);
			if (bSameOuter && !InLevel->Actors.Contains(Actor))
			{
				InLevel->AddLoadedActor(Actor);
			}

			// Include objects found in the source actor package in the destination level package
			// @todo_ow: Generalize this case and handle properly the case of LevelInstance (LI actor's package is the duplicated LI package, i.e. not an external actor package)
			if (ActorExternalPackage)
			{
				TArray<UObject*> Objects;
				const bool bIncludeNestedSubobjects = false;
				GetObjectsWithOuter(ActorExternalPackage, Objects, bIncludeNestedSubobjects);
				for (UObject* Object : Objects)
				{
					if (Object->GetFName() != NAME_PackageMetaData)
					{
						Object->Rename(nullptr, LevelPackage, REN_ForceNoResetLoaders);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogEngine, Warning, TEXT("Can't find actor %s."), *PackageObjectMapping.Path.ToString());
		}
	}
}

void FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(ULevel* InLevel, UWorldPartition* InWorldPartition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths);

	check(InLevel);
	check(InWorldPartition);

	FSoftObjectPathFixupArchive FixupSerializer([InWorldPartition](FSoftObjectPath& Value)
	{
		if(!Value.IsNull())
		{
			InWorldPartition->RemapSoftObjectPath(Value);
		}
	});
	FixupSerializer.Fixup(InLevel);
}

FString FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(const FActorContainerID& InContainerID, const FString& InSubPathString)
{
	if (!InContainerID.IsMainContainer())
	{
		constexpr const TCHAR PersistenLevelName[] = TEXT("PersistentLevel.");
		constexpr const int32 DotPos = UE_ARRAY_COUNT(PersistenLevelName);
		if (InSubPathString.StartsWith(PersistenLevelName))
		{
			const int32 SubObjectPos = InSubPathString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, DotPos);
			if (SubObjectPos == INDEX_NONE)
			{
				return InSubPathString + TEXT("_") + InContainerID.ToString();
			}
			else
			{
				return InSubPathString.Mid(0, SubObjectPos) + TEXT("_") + InContainerID.ToString() + InSubPathString.Mid(SubObjectPos);
			}
		}
	}

	return InSubPathString;
}

FString FWorldPartitionLevelHelper::AddActorContainerIDToActorPath(const FActorContainerID& InContainerID, const FString& InActorPath)
{
	if (!InContainerID.IsMainContainer())
	{
		const FSoftObjectPath SoftObjectPath(InActorPath);
		const FString NewSubPathString = FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(InContainerID, SoftObjectPath.GetSubPathString());
		return FSoftObjectPath(SoftObjectPath.GetAssetPathName(), NewSubPathString).ToString();
	}

	return InActorPath;
}

/**
 * Creates an empty Level used in World Partition
 */
ULevel* FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, UPackage* InPackage)
{
	// Create or use given package
	UPackage* CellPackage = nullptr;
	if (InPackage)
	{
		check(FindObject<UPackage>(nullptr, *InPackage->GetName()));
		CellPackage = InPackage;
	}
	else
	{
		FString PackageName = FPackageName::ObjectPathToPackageName(InWorldAssetName);
		check(!FindObject<UPackage>(nullptr, *PackageName));
		CellPackage = CreatePackage(*PackageName);
		CellPackage->SetPackageFlags(PKG_NewlyCreated);
	}

	if (InWorld->IsPlayInEditor())
	{
		check(!InPackage);
		CellPackage->SetPackageFlags(PKG_PlayInEditor);
		CellPackage->SetPIEInstanceID(InWorld->GetPackage()->GetPIEInstanceID());
	}

	// Create World & Persistent Level
	UWorld::InitializationValues IVS = FWorldPartitionLevelHelper::GetWorldInitializationValues();
	const FName WorldName = FName(FPackageName::ObjectPathToObjectName(InWorldAssetName));
	UWorld* NewWorld = UWorld::CreateWorld(InWorld->WorldType, /*bInformEngineOfWorld*/false, WorldName, CellPackage, /*bAddToRoot*/false, InWorld->FeatureLevel, &IVS, /*bInSkipInitWorld*/true);
	check(NewWorld);
	NewWorld->SetFlags(RF_Public | RF_Standalone);
	check(NewWorld->GetWorldSettings());
	check(UWorld::FindWorldInPackage(CellPackage) == NewWorld);
	check(InPackage || (NewWorld->GetPathName() == InWorldAssetName));
	
	// Setup of streaming cell Runtime Level
	ULevel* NewLevel = NewWorld->PersistentLevel;
	check(NewLevel);
	check(NewLevel->GetFName() == InWorld->PersistentLevel->GetFName());
	check(NewLevel->OwningWorld == NewWorld);
	check(NewLevel->Model);
	check(!NewLevel->bIsVisible);

	// Mark the level as a runtime cell
	NewLevel->bIsWorldPartitionRuntimeCell = true;
	
	// Mark the level package as fully loaded
	CellPackage->MarkAsFullyLoaded();

	// Mark the level package as containing a map
	CellPackage->ThisContainsMap();

	// Set the guids on the constructed level to something based on the generator rather than allowing indeterminism by
	// constructing new Guids on every cook
	// @todo_ow: revisit for static lighting support. We need to base the LevelBuildDataId on the relevant information from the
	// actor's package.
	NewLevel->LevelBuildDataId = InWorld->PersistentLevel->LevelBuildDataId;
	check(InWorld->PersistentLevel->Model && NewLevel->Model);
	NewLevel->Model->LightingGuid = InWorld->PersistentLevel->Model->LightingGuid;

	return NewLevel;
}

bool FWorldPartitionLevelHelper::LoadActors(ULevel* InDestLevel, TArrayView<FWorldPartitionRuntimeCellObjectMapping> InActorPackages, FWorldPartitionPackageCache& InPackageCache, TFunction<void(bool)> InCompletionCallback, bool bInLoadAsync, FLinkerInstancingContext* InOutInstancingContext /*=nullptr*/)
{
	UPackage* DestPackage = InDestLevel ? InDestLevel->GetPackage() : nullptr;
	FString ShortLevelPackageName = DestPackage? FPackageName::GetShortName(DestPackage->GetFName()) : FString();

	struct FLoadProgress
	{
		int32 NumPendingLoadRequests = 0;
		int32 NumFailedLoadedRequests = 0;
	};
	TSharedPtr<FLoadProgress> LoadProgress = MakeShared<FLoadProgress>();

	// Actors to load
	TArray<FString> ActorPackageInstanceNames;
	ActorPackageInstanceNames.Reserve(InActorPackages.Num());
	TArray<FWorldPartitionRuntimeCellObjectMapping*> ActorPackages;
	ActorPackages.Reserve(InActorPackages.Num());

	// Levels to Load with actors to duplicate
	TMap<FActorContainerID, TArray<FWorldPartitionRuntimeCellObjectMapping*>> PackagesToDuplicate;
	check(!InDestLevel || InOutInstancingContext);

	for (FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InActorPackages)
	{
		if (PackageObjectMapping.ContainerID.IsMainContainer())
		{
			if (InOutInstancingContext)
			{
				FString ObjectPath = PackageObjectMapping.Package.ToString();
				FString ActorPackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
				FString ActorPackageInstanceName = FString::Printf(TEXT("%s_%s"), *ActorPackageName, *ShortLevelPackageName);

				ActorPackageInstanceNames.Add(ActorPackageInstanceName);
				InOutInstancingContext->AddMapping(FName(*ActorPackageName), FName(*ActorPackageInstanceName));
			}
			ActorPackages.Add(&PackageObjectMapping);
		}
		else
		{
			// When loading LevelInstances we duplicate the package 
			PackagesToDuplicate.FindOrAdd(PackageObjectMapping.ContainerID).Add(&PackageObjectMapping);
		}
	}

	LoadProgress->NumPendingLoadRequests = ActorPackages.Num() + PackagesToDuplicate.Num();

	for (int32 ChildIndex = 0; ChildIndex < ActorPackages.Num(); ChildIndex++)
	{
		const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping = *ActorPackages[ChildIndex];
		const FString ActorPackageName = FPackageName::ObjectPathToPackageName(PackageObjectMapping.Package.ToString());
		FName ActorName = *FPaths::GetExtension(PackageObjectMapping.Path.ToString());

		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, ActorName, InDestLevel, InCompletionCallback](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			check(LoadProgress->NumPendingLoadRequests);
			LoadProgress->NumPendingLoadRequests--;

			AActor* Actor = LoadedPackage ? FindObject<AActor>(LoadedPackage, *ActorName.ToString()) : nullptr;

			if (Actor)
			{
				if (InDestLevel)
				{
					check(Actor->IsPackageExternal());
					InDestLevel->Actors.Add(Actor);
					check(Actor->GetLevel() == InDestLevel);
				}
								
				UE_LOG(LogEngine, Verbose, TEXT(" ==> Loaded %s (remaining: %d)"), *Actor->GetFullName(), LoadProgress->NumPendingLoadRequests);
			}
			else
			{
				UE_LOG(LogEngine, Warning, TEXT("Failed to load %s"), *LoadedPackageName.ToString());
				//@todo_ow: cumulate and process when NumPendingActorRequests == 0
				LoadProgress->NumFailedLoadedRequests++;
			}

			if (!LoadProgress->NumPendingLoadRequests)
			{
				InCompletionCallback(!LoadProgress->NumFailedLoadedRequests);
			}
		});

		if (bInLoadAsync)
		{
			FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(*ActorPackageName);
			check(DestPackage);
			const EPackageFlags PackageFlags = DestPackage->HasAnyPackageFlags(PKG_PlayInEditor) ? PKG_PlayInEditor : PKG_None;
			::LoadPackageAsync(PackagePath, FName(*ActorPackageInstanceNames[ChildIndex]), CompletionCallback, PackageFlags, DestPackage->GetPIEInstanceID(), 0, InOutInstancingContext);
		}
		else
		{
			UPackage* InstancingPackage = nullptr;
			if(InOutInstancingContext)
			{ 
				InstancingPackage = CreatePackage(*ActorPackageInstanceNames[ChildIndex]);
			}

			UPackage* Package = LoadPackage(InstancingPackage, *ActorPackageName, LOAD_None, nullptr, InOutInstancingContext);
			CompletionCallback.Execute(*ActorPackageName, Package, Package ? EAsyncLoadingResult::Succeeded : EAsyncLoadingResult::Failed);
		}
	}

	for (auto& Pair : PackagesToDuplicate)
	{
		FString PackageToLoadFrom = Pair.Value[0]->ContainerPackage.ToString();
		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, ContainerInstanceID = Pair.Key, Mappings = MoveTemp(Pair.Value), InDestLevel, &InPackageCache, InCompletionCallback](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			check(LoadProgress->NumPendingLoadRequests);
			LoadProgress->NumPendingLoadRequests--;

			if (LoadedPackage)
			{
				FName DuplicatePackageName(*FString::Printf(TEXT("%s_%s"), *LoadedPackage->GetName(), *ContainerInstanceID.ToString()));
				UPackage* DuplicatedPackage = InPackageCache.FindPackage(DuplicatePackageName);
				UWorld* DuplicatedWorld = nullptr;

				// Check that existing Package contains all actors (might be a previously used Dup package when loading unloaded cells)
				if (DuplicatedPackage)
				{
					DuplicatedWorld = UWorld::FindWorldInPackage(DuplicatedPackage);
					for (auto Mapping : Mappings)
					{
						FString ActorName = FPaths::GetExtension(Mapping->Path.ToString());
						if (!FindObject<AActor>(DuplicatedWorld->PersistentLevel, *ActorName))
						{
							InPackageCache.TrashPackage(DuplicatedPackage);
							DuplicatedPackage = nullptr;
							DuplicatedWorld = nullptr;
							break;
						}
					}
				}

				// Package wasn't found or was missing actors
				if (!DuplicatedPackage)
				{
					DuplicatedPackage = InPackageCache.DuplicateWorldPackage(LoadedPackage, DuplicatePackageName);
					check(DuplicatedPackage);
					DuplicatedWorld = UWorld::FindWorldInPackage(DuplicatedPackage);
				}

				// Get Source Path for this world (ReplaceFrom)
				UWorld* LoadedWorld = UWorld::FindWorldInPackage(LoadedPackage);
				FString SourceWorldPath, RemappedWorldPath;
				LoadedWorld->GetSoftObjectPathMapping(SourceWorldPath, RemappedWorldPath);
				
				for (auto Mapping : Mappings)
				{
					FString ActorName = FPaths::GetExtension(Mapping->Path.ToString());
					AActor* DuplicatedActor = FindObject<AActor>(DuplicatedWorld->PersistentLevel, *ActorName);
					// Possible actor isn't found if Actor package failed to load because of missing dependencies (ex: deleted Blueprint)
					if (DuplicatedActor)
					{
						DuplicatedActor->Rename(*FString::Printf(TEXT("%s_%s"), *DuplicatedActor->GetName(), *Mapping->ContainerID.ToString()), InDestLevel, REN_NonTransactional | REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors);
						USceneComponent* RootComponent = DuplicatedActor->GetRootComponent();

						FLevelUtils::FApplyLevelTransformParams TransformParams(nullptr, Mapping->ContainerTransform);
						TransformParams.Actor = DuplicatedActor;
						TransformParams.bDoPostEditMove = false;
						FLevelUtils::ApplyLevelTransform(TransformParams);

						// Path to use when searching for this actor in MoveExternalActorsToLevel
						Mapping->LoadedPath = *DuplicatedActor->GetPathName();
						
						// Fixup any FSoftObjectPath from this Actor (and its SubObjects) in this container to another object in the same container with a ContainerID suffix that can be remapped to
						// to a Cell in the StreamingPolicy (this relies on the fact that the _DUP package doesn't get fixed up)
						FSoftObjectPathFixupArchive FixupArchive([&](FSoftObjectPath& Value)
						{
							if (!Value.IsNull() && Value.GetAssetPathString().Equals(SourceWorldPath, ESearchCase::IgnoreCase))
							{
								Value.SetSubPathString(AddActorContainerIDToSubPathString(Mapping->ContainerID, Value.GetSubPathString()));
							}
						});
						FixupArchive.Fixup(DuplicatedActor);

						UE_LOG(LogEngine, Verbose, TEXT(" ==> Duplicated %s (remaining: %d)"), *DuplicatedActor->GetFullName(), LoadProgress->NumPendingLoadRequests);
					}
					else
					{
						Mapping->LoadedPath = NAME_None;
						LoadProgress->NumFailedLoadedRequests++;
						UE_LOG(LogEngine, Warning, TEXT("Failed to find actor %s in package %s"), *ActorName, *LoadedPackageName.ToString());
					}
				}
			}
			else
			{
				UE_LOG(LogEngine, Warning, TEXT("Failed to load %s"), *LoadedPackageName.ToString());
				LoadProgress->NumFailedLoadedRequests++;
			}

			if (!LoadProgress->NumPendingLoadRequests)
			{
				InCompletionCallback(!LoadProgress->NumFailedLoadedRequests);
			}
		});

		// Prefix with /Temp so package doesn't appear in Content, PostFix the Package so that we don't load over the original package
		FName PackageDuplicateSeedName(*FString::Printf(TEXT("/Temp%s_DUP"), *PackageToLoadFrom));
		
		// Use package cache here because level instances can load the same levels across cells we want to avoid Loading the same package multiple times...
		InPackageCache.LoadPackage(PackageDuplicateSeedName, *PackageToLoadFrom, CompletionCallback, /*bLoadAsync*/bInLoadAsync, /*bInWorldPackage*/true);
	}

	return (LoadProgress->NumPendingLoadRequests == 0);
}

#endif
