// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionLevelHelper implementation
 */

#include "WorldPartition/WorldPartitionLevelHelper.h"

#if WITH_EDITOR

#include "FileHelpers.h"
#include "UnrealEngine.h"

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
 * Creates, populates and saves a Level used in World Partition
 */
bool FWorldPartitionLevelHelper::CreateAndSaveLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages)
{
	check(IsRunningCommandlet());
	check(InWorld && !InWorld->IsGameWorld());

	// Create streaming cell Level package
	ULevel* NewLevel = FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(InWorld, InWorldAssetName);
	UPackage* NewPackage = NewLevel->GetPackage();
	UWorld* NewWorld = UWorld::FindWorldInPackage(NewPackage);

	// Move all actors to Cell level
	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InChildPackages)
	{
		AActor* Actor = FindObject<AActor>(nullptr, *PackageObjectMapping.Path.ToString());
		if (ensure(Actor))
		{
			Actor->SetPackageExternal(false, false);
			Actor->Rename(nullptr, NewLevel);
			check(Actor->GetPackage() == NewPackage);
		}
		else
		{
			UE_LOG(LogEngine, Warning, TEXT("Can't find actor %s."), *PackageObjectMapping.Path.ToString());
		}
	}

	// Save
	const FString PackageName = NewPackage->GetName();
	const bool bSuccess = UEditorLoadingAndSavingUtils::SaveMap(NewWorld, PackageName);
	UE_CLOG(!bSuccess, LogEngine, Error, TEXT("Error saving map %s."), *PackageName);

	// No need of this Level anymore
	NewLevel->MarkPendingKill();
	NewWorld->RemoveFromRoot();
	NewWorld->MarkPendingKill();

	return bSuccess;
}

/**
 * Creates an empty Level used in World Partition
 */
ULevel* FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName)
{
	// Create Level package
	FString PackageName = FPackageName::ObjectPathToPackageName(InWorldAssetName);
	check(!FindObject<UPackage>(nullptr, *PackageName));
	UPackage* NewPackage = CreatePackage(*PackageName);
	NewPackage->SetPackageFlags(PKG_NewlyCreated);
	if (InWorld->IsPlayInEditor())
	{
		NewPackage->SetPackageFlags(PKG_PlayInEditor);
		NewPackage->PIEInstanceID = InWorld->GetPackage()->PIEInstanceID;
		check(NewPackage);
	}

	// Create World & Persistent Level
	UWorld::InitializationValues IVS = FWorldPartitionLevelHelper::GetWorldInitializationValues();
	const FName WorldName = FName(FPackageName::ObjectPathToObjectName(InWorldAssetName));
	UWorld* NewWorld = UWorld::CreateWorld(InWorld->WorldType, /*bInformEngineOfWorld*/false, WorldName, NewPackage, /*bAddToRoot*/false, InWorld->FeatureLevel, &IVS, /*bInSkipInitWorld*/true);
	check(UWorld::FindWorldInPackage(NewPackage) == NewWorld);
	check(NewWorld);
	check(NewWorld->GetWorldSettings());
	check(NewWorld->GetPathName() == InWorldAssetName);
	
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

#endif