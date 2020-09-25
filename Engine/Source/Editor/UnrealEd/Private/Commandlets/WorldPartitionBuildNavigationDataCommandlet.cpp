// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/WorldPartitionBuildNavigationDataCommandlet.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Logging/LogMacros.h"

#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/NavigationData/NavigationDataChunkActor.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
extern UNREALED_API UEditorEngine* GEditor;
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuildNavigationDataCommandlet, Log, All);

UWorldPartitionBuildNavigationDataCommandlet::UWorldPartitionBuildNavigationDataCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UWorldPartitionBuildNavigationDataCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	// Retrieve map name from the command line
	FString MapName;
	const bool bMapProvided = FParse::Value(*Params, TEXT("Map="), MapName);
	if (!bMapProvided)
	{
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("No map specified."));
		return 1;
	}

	// Load the map package
	UPackage* MapPackage = LoadPackage(NULL, *MapName, LOAD_None); 
	if (!MapPackage)
	{
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("Couldn't load package %s."), *MapName);
		return 1;
	}

	// Find the world in the given package
	UWorld* World = UWorld::FindWorldInPackage(MapPackage);
	if (!World)
	{
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("No world in package %s."), *MapPackage->GetName());
		return 1;
	}

	// Setup the world.
	World->WorldType = EWorldType::Editor;
	World->AddToRoot();
	if (!World->bIsWorldInitialized)
	{
		UWorld::InitializationValues IVS;
		IVS.RequiresHitProxies(false);
		IVS.ShouldSimulatePhysics(false);
		IVS.EnableTraceCollision(false);
		IVS.CreateNavigation(true);
		IVS.CreateAISystem(false);
		IVS.AllowAudioPlayback(false);
		IVS.CreatePhysicsScene(true);

		World->InitWorld(UWorld::InitializationValues(IVS));
		World->PersistentLevel->UpdateModelComponents();
		World->UpdateWorldComponents(true /*bRerunConstructionScripts*/, false /*bCurrentLevelOnly*/);
	}

	// Retrieve the world partition.
	const UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	UWorldPartition* WorldPartition = nullptr;
	if (WorldPartitionSubsystem)
	{
		WorldPartition = World->GetWorldPartition();
		check(WorldPartition);
	}

	FWorldContext& WorldContext = GEditor->GetEditorWorldContext(true /*bEnsureIsGWorld*/);
	WorldContext.SetCurrentWorld(World);
	GWorld = World;
	
	// For now, load all cells
	// In the future, we'll want the commandlet to be able to perform partial updates of the map
	// to allow rebuild to be distributed on multiple machines.
	const FBox LoadBox(FVector(-WORLD_MAX, -WORLD_MAX, -WORLD_MAX), FVector(WORLD_MAX, WORLD_MAX, WORLD_MAX));
	WorldPartition->LoadEditorCells(LoadBox);

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

	TArray<UPackage*> PackagesToDelete;
	TArray<UPackage*> PackagesToSave;

	for (UPackage* Package : NavigationDataChunkActorPackages)
	{
		if (Package && Package->IsDirty())
		{
			if (UPackage::IsEmptyPackage(Package))
			{
				PackagesToDelete.Add(Package);
			}
			else
			{
				PackagesToSave.Add(Package);
			}
		}
	}

	// Delete packages
	if (!PackagesToDelete.IsEmpty())
	{
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Log, TEXT("Deleting %d packages."), PackagesToDelete.Num());
		if (!PackageHelper.Delete(PackagesToDelete))
		{
			UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("Error deleting packages."));
			return 1;
		}
	}

	// Save packages
	if (!PackagesToSave.IsEmpty())
	{
		// Checkout packages to save
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Log, TEXT("Checking out %d actor packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.Checkout(Package))
			{
				UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("Error checking out package %s."), *Package->GetName());
				return 1;
			}
		}

		// Save packages
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Verbose, TEXT("Saving package  %s."), *Package->GetName());
			FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
			if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_Async))
			{
				UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("Error saving package %s."), *Package->GetName());
				return 1;
			}
		}

		// Add new packages to source control
		UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Log, TEXT("Adding packages to source control."));
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.AddToSourceControl(Package))
			{
				UE_LOG(LogWorldPartitionBuildNavigationDataCommandlet, Error, TEXT("Error adding package %s to source control."), *Package->GetName());
				return 1;
			}
		}
	}

	// Cleanup
	World->RemoveFromRoot();
	WorldContext.SetCurrentWorld(nullptr);
	GWorld = nullptr;

	return 0;
}
