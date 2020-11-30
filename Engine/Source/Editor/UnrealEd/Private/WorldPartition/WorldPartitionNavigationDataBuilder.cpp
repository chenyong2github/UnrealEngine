// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionNavigationDataBuilder.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Logging/LogMacros.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/NavigationData/NavigationDataChunkActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionNavigationDataBuilder, Log, All);

UWorldPartitionNavigationDataBuilder::UWorldPartitionNavigationDataBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UWorldPartitionNavigationDataBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	check(WorldPartitionSubsystem);

	UWorldPartition* WorldPartition = World->GetWorldPartition();
	check(WorldPartition);
	
	TSet<UPackage*> NavigationDataChunkActorPackages;

	// Gather all packages before any navigation data chunk actors are deleted
	for (TActorIterator<ANavigationDataChunkActor> ItActor(World); ItActor; ++ItActor)
	{
		NavigationDataChunkActorPackages.Add(ItActor->GetPackage());
	}

	// Rebuild ANavigationDataChunkActor for the whole world
	WorldPartition->GenerateNavigationData();

	// Gather all packages again to include newly created ANavigationDataChunkActor actors
	for (TActorIterator<ANavigationDataChunkActor> ItActor(World); ItActor; ++ItActor)
	{
		NavigationDataChunkActorPackages.Add(ItActor->GetPackage());
	}

	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;

	for (UPackage* ActorPackage : NavigationDataChunkActorPackages)
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
		UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Deleting %d packages."), PackagesToDelete.Num());
		if (!PackageHelper.Delete(PackagesToDelete))
		{
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error deleting packages."));
			return 1;
		}
	}

	// Save packages
	if (!PackagesToSave.IsEmpty())
	{
		// Checkout packages to save
		UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Checking out %d actor packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.Checkout(Package))
			{
				UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error checking out package %s."), *Package->GetName());
				return 1;
			}
		}

		// Save packages
		UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Verbose, TEXT("Saving package  %s."), *Package->GetName());
			FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
			if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
			{
				UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error saving package %s."), *Package->GetName());
				return 1;
			}
		}

		// Add new packages to source control
		UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Adding packages to source control."));
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.AddToSourceControl(Package))
			{
				UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error adding package %s to source control."), *Package->GetName());
				return 1;
			}
		}

		UPackage::WaitForAsyncFileWrites();
	}

	return true;
}