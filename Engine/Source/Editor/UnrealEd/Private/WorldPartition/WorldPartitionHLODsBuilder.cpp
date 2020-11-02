// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHLODsBuilder.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Logging/LogMacros.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionHLODsBuilder, All, All);

UWorldPartitionHLODsBuilder::UWorldPartitionHLODsBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UWorldPartitionHLODsBuilder::RequiresCommandletRendering() const 
{
	return true;
}

bool UWorldPartitionHLODsBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	check(WorldPartitionSubsystem);

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	check(WorldPartition);

	TSet<UPackage*> ActorPackages;

	// Gather all packages
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsPackageExternal())
		{
			ActorPackages.Add(It->GetPackage());
		}
	}

	// Rebuild HLOD for the whole world
	WorldPartition->GenerateHLOD();
	
	// Gather all packages again to include newly created actors
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsPackageExternal())
		{
			ActorPackages.Add(It->GetPackage());
		}
	}

	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;

	for (UPackage* ActorPackage : ActorPackages)
	{
		if (ActorPackage && ActorPackage->IsDirty())
		{
			if (UPackage::IsEmptyPackage(ActorPackage))
			{
				PackagesToDelete.Add(ActorPackage);
			}
			else
			{
				PackagesToSave.Add(ActorPackage);
			}
		}
	}

	// Delete packages
	if (!PackagesToDelete.IsEmpty())
	{
		UE_LOG(LogWorldPartitionHLODsBuilder, Log, TEXT("Deleting %d packages."), PackagesToDelete.Num());
		if (!PackageHelper.Delete(PackagesToDelete))
		{
			UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error deleting packages."));
			return 1;
		}
	}

	// Save packages
	if (!PackagesToSave.IsEmpty())
	{
		// Checkout packages
		UE_LOG(LogWorldPartitionHLODsBuilder, Log, TEXT("Checking out %d actor packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.Checkout(Package))
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error checking out package %s."), *Package->GetName());
				return 1;
			}
		}

		// Save packages
		UE_LOG(LogWorldPartitionHLODsBuilder, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
			if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error saving package %s."), *Package->GetName());
				return 1;
			}
		}

		// Add new packages to source control
		UE_LOG(LogWorldPartitionHLODsBuilder, Log, TEXT("Adding packages to source control."));
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.AddToSourceControl(Package))
			{
				UE_LOG(LogWorldPartitionHLODsBuilder, Error, TEXT("Error adding package %s to source control."), *Package->GetName());
				return 1;
			}
		}

		UPackage::WaitForAsyncFileWrites();
	}

	return true;
}