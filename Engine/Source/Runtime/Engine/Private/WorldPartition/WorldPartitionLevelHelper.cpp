// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionLevelHelper implementation
 */

#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "WorldPartition/WorldPartitionPackageCache.h"

#if WITH_EDITOR

#include "FileHelpers.h"
#include "UnrealEngine.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
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
	check(InLevel);
	UPackage* LevelPackage = InLevel->GetPackage();
	
	// Move all actors to Cell level
	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InChildPackages)
	{
		AActor* Actor = FindObject<AActor>(nullptr, *PackageObjectMapping.LoadedPath.ToString());
		if (ensure(Actor))
		{
			const bool bSameOuter = (InLevel == Actor->GetOuter());
			Actor->SetPackageExternal(false, false);
			Actor->Rename(nullptr, InLevel);
			check(Actor->GetPackage() == LevelPackage);
			if (bSameOuter && !InLevel->Actors.Contains(Actor))
			{
				InLevel->AddLoadedActor(Actor);
			}
		}
		else
		{
			UE_LOG(LogEngine, Warning, TEXT("Can't find actor %s."), *PackageObjectMapping.Path.ToString());
		}
	}
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
		CellPackage->PIEInstanceID = InWorld->GetPackage()->PIEInstanceID;
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
	NewLevel->bRequireFullVisibilityToRender = true;
	return NewLevel;
}

bool FWorldPartitionLevelHelper::LoadActors(ULevel* InDestLevel, TArrayView<FWorldPartitionRuntimeCellObjectMapping> InActorPackages, FWorldPartitionPackageCache& InPackageCache, FWorldPartitionLevelHelper::FOnLoadActorsCompleted InCompletionCallback, bool bLoadForPIE, bool bLoadAsync /*=true*/, FLinkerInstancingContext* InOutInstancingContext /*=nullptr*/)
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
	TMap<uint32, TArray<FWorldPartitionRuntimeCellObjectMapping*>> PackagesToDuplicate;
	check(!bLoadForPIE || InOutInstancingContext);

	for (FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InActorPackages)
	{
		if (PackageObjectMapping.ContainerID == 0)
		{
			if (bLoadForPIE)
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

		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, ActorName, bLoadForPIE, InDestLevel, InCompletionCallback](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
			{
				check(LoadProgress->NumPendingLoadRequests);
				LoadProgress->NumPendingLoadRequests--;
				if (LoadedPackage)
				{
					AActor* Actor = FindObject<AActor>(LoadedPackage, *ActorName.ToString());

					check(Actor);
					if (bLoadForPIE)
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
					InCompletionCallback.Execute(!LoadProgress->NumFailedLoadedRequests);
				}
			});


		FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(*ActorPackageName);
		if (bLoadForPIE)
		{
			check(DestPackage);
			::LoadPackageAsync(PackagePath, FName(*ActorPackageInstanceNames[ChildIndex]), CompletionCallback, nullptr, PKG_PlayInEditor, DestPackage->PIEInstanceID, 0, InOutInstancingContext);
		}
		else
		{
			::LoadPackageAsync(PackagePath, NAME_None, CompletionCallback, nullptr, PKG_None, INDEX_NONE, 0, InOutInstancingContext);
		}
	}

	for (auto& Pair : PackagesToDuplicate)
	{
		FString PackageToLoadFrom = Pair.Value[0]->ContainerPackage.ToString();
		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, LevelInstanceID = Pair.Key, Mappings = MoveTemp(Pair.Value), InDestLevel, &InPackageCache, InCompletionCallback](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			check(LoadProgress->NumPendingLoadRequests);
			LoadProgress->NumPendingLoadRequests--;

			if (LoadedPackage)
			{
				FName DuplicatePackageName(*FString::Printf(TEXT("%s_%08x"), *LoadedPackage->GetName(), LevelInstanceID));
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
					check(DuplicatedPackage)
						DuplicatedWorld = UWorld::FindWorldInPackage(DuplicatedPackage);
				}

				for (auto Mapping : Mappings)
				{
					FString ActorName = FPaths::GetExtension(Mapping->Path.ToString());
					AActor* DuplicatedActor = FindObject<AActor>(DuplicatedWorld->PersistentLevel, *ActorName);
					check(DuplicatedActor);

					DuplicatedActor->Rename(*FString::Printf(TEXT("%s_%08x"), *DuplicatedActor->GetName(), Mapping->ContainerID), InDestLevel, REN_NonTransactional | REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors);
					USceneComponent* RootComponent = DuplicatedActor->GetRootComponent();
					
					FLevelUtils::FApplyLevelTransformParams TransformParams(nullptr, Mapping->ContainerTransform);
					TransformParams.Actor = DuplicatedActor;
					TransformParams.bDoPostEditMove = false;
					FLevelUtils::ApplyLevelTransform(TransformParams);

					// Path to use when searching for this actor in MoveExternalActorsToLevel
					Mapping->LoadedPath = *DuplicatedActor->GetPathName();
					UE_LOG(LogEngine, Verbose, TEXT(" ==> Duplicated %s (remaining: %d)"), *DuplicatedActor->GetFullName(), LoadProgress->NumPendingLoadRequests);
				}
			}
			else
			{
				UE_LOG(LogEngine, Warning, TEXT("Failed to load %s"), *LoadedPackageName.ToString());
				LoadProgress->NumFailedLoadedRequests++;
			}

			if (!LoadProgress->NumPendingLoadRequests)
			{
				InCompletionCallback.Execute(!LoadProgress->NumFailedLoadedRequests);
			}
		});

		// PostFix the Package so that we don't load over the original package
		FName PackageDuplicateSeedName(*FString::Printf(TEXT("%s_DUP"), *PackageToLoadFrom));
		
		// Use Package Cache here because Level Instances can load the same levels across cells we want to avoid Loading the same package multiple times...
		InPackageCache.LoadWorldPackageAsync(PackageDuplicateSeedName, *PackageToLoadFrom, CompletionCallback);
	}

	if (!bLoadAsync)
	{
		// Finish all async loading.
		FlushAsyncLoading();
	}

	return (LoadProgress->NumPendingLoadRequests == 0);
}

#endif