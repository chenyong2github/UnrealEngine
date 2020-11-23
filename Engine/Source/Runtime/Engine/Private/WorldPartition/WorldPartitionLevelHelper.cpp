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
 * Creates and populates a Level used in World Partition
 */
bool FWorldPartitionLevelHelper::CreateAndFillLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, UPackage* InPackage, const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages)
{
	check(IsRunningCommandlet());
	check(InWorld && !InWorld->IsGameWorld());
	check(InPackage);

	// Create streaming cell Level package
	ULevel* NewLevel = FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(InWorld, InWorldAssetName, InPackage);
	UPackage* NewLevelPackage = NewLevel->GetPackage();
	check(NewLevelPackage == InPackage);
	UWorld* NewWorld = UWorld::FindWorldInPackage(NewLevelPackage);

	// Move all actors to Cell level
	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InChildPackages)
	{
		AActor* Actor = FindObject<AActor>(nullptr, *PackageObjectMapping.Path.ToString());
		if (ensure(Actor))
		{
			Actor->SetPackageExternal(false, false);
			Actor->Rename(nullptr, NewLevel);
			check(Actor->GetPackage() == NewLevelPackage);
		}
		else
		{
			UE_LOG(LogEngine, Warning, TEXT("Can't find actor %s."), *PackageObjectMapping.Path.ToString());
		}
	}
	return true;
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

#endif