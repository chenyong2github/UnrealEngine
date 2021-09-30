// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionNavigationDataBuilder.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "StaticMeshCompiler.h"
#include "Logging/LogMacros.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/NavigationData/NavigationDataChunkActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionNavigationDataBuilder, Log, All);

UWorldPartitionNavigationDataBuilder::UWorldPartitionNavigationDataBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Size of loaded cell. Set as big as your hardware can afford.
	// @todo: move to a config file.
	IterativeCellSize = 204800;
	
	// Extra padding around loaded cell.
	// @todo: set value programatically.
	IterativeCellOverlapSize = 2000;
}

bool UWorldPartitionNavigationDataBuilder::RunInternal(UWorld* World, const FBox& LoadedBounds, FPackageSourceControlHelper& PackageHelper)
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

	// Destroy any existing navigation data chunk actors within bounds we are generating, we will make new ones.
	const FBox GeneratingBounds = LoadedBounds.ExpandBy(-IterativeCellOverlapSize);
	for (TActorIterator<ANavigationDataChunkActor> It(World); It; ++It)
	{
		ANavigationDataChunkActor* Actor = *It;
		const FVector Location = Actor->GetActorLocation();
		if (GeneratingBounds.IsInside(Location))
		{
			World->DestroyActor(Actor);	
		}
	}

	// Make sure static meshes have compiled before generating navigation data
	FStaticMeshCompilingManager::Get().FinishAllCompilation();
	
	// Rebuild ANavigationDataChunkActor in loaded bounds
	WorldPartition->GenerateNavigationData(LoadedBounds);

	// Gather all packages again to include newly created ANavigationDataChunkActor actors
	for (TActorIterator<ANavigationDataChunkActor> ItActor(World); ItActor; ++ItActor)
	{
		NavigationDataChunkActorPackages.Add(ItActor->GetPackage());
	}

	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;

	for (UPackage* ActorPackage : NavigationDataChunkActorPackages)
	{
		// Only change package that have been dirtied
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
		for (const UPackage* Package : PackagesToDelete)
		{
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Verbose, TEXT("   Deleting package  %s."), *Package->GetName());	
		}
		
		if (!PackageHelper.Delete(PackagesToDelete))
		{
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error deleting packages."));
			return 1;
		}
	}

	// Save packages
	if (!PackagesToSave.IsEmpty())
	{
		{
			// Checkout packages to save
			TRACE_CPUPROFILER_EVENT_SCOPE(CheckoutPackages);
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Checking out %d packages."), PackagesToSave.Num());

			if (PackageHelper.UseSourceControl())
			{
				FEditorFileUtils::CheckoutPackages(PackagesToSave, /*OutPackagesCheckedOut*/nullptr, /*bErrorIfAlreadyCheckedOut*/false);
			}
			else
			{
				// Remove read-only
				for (const UPackage* Package : PackagesToSave)
				{
					const FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
					if (IPlatformFile::GetPlatformPhysical().FileExists(*PackageFilename))
					{
						if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackageFilename, /*bNewReadOnlyValue*/false))
						{
							UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error setting %s writable"), *PackageFilename);
							return 1;
						}
					}
				}
			}
		}
		
		{
			// Save packages
			TRACE_CPUPROFILER_EVENT_SCOPE(SavingPackages);
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Saving %d packages."), PackagesToSave.Num());

			for (UPackage* Package : PackagesToSave)
			{
				UE_LOG(LogWorldPartitionNavigationDataBuilder, Verbose, TEXT("   Saving package  %s."), *Package->GetName());
				FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
				if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
				{
					UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error saving package %s."), *Package->GetName());
					return 1;
				}
			}
		}
		
		{
			// Add new packages to source control
			TRACE_CPUPROFILER_EVENT_SCOPE(AddingToSourceControl);
			UE_LOG(LogWorldPartitionNavigationDataBuilder, Log, TEXT("Adding packages to source control."));

			for (UPackage* Package : PackagesToSave)
			{
				if (!PackageHelper.AddToSourceControl(Package))
				{
					UE_LOG(LogWorldPartitionNavigationDataBuilder, Error, TEXT("Error adding package %s to source control."), *Package->GetName());
					return 1;
				}
			}
		}

		UPackage::WaitForAsyncFileWrites();
	}

	return true;
}
