// Copyright Epic Games, Inc. All Rights Reserved.

/*============================================================================================
 WorldPartitionBuildHLODsCommandlet.cpp: Commandlet to build HLODs for a partionned level
============================================================================================*/

#include "Commandlets/WorldPartitionBuildHLODsCommandlet.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Logging/LogMacros.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuildHLODsCommandlet, All, All);

UWorldPartitionBuildHLODsCommandlet::UWorldPartitionBuildHLODsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UWorldPartitionBuildHLODsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	// Retrieve map name from the commandline
	FString MapName;
	bool bMapProvided = FParse::Value(*Params, TEXT("Map="), MapName);
	if (!bMapProvided)
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("No map specified."));
		return 1;
	}

	if (!IsAllowCommandletRendering())
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("The option \"-AllowCommandletRendering\" must be provided for the HLOD rebuild process to work"));
		return 1;
	}

	// Load the map package
	UPackage* MapPackage = LoadPackage(NULL, *MapName, LOAD_None);
	if (!MapPackage)
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Couldn't load package %s."), *MapName);
		return 1;
	}

	// Find the world in the given package
	UWorld* World = UWorld::FindWorldInPackage(MapPackage);
	if (!World)
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("No world in specified package."));
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
		IVS.CreateNavigation(false);
		IVS.CreateAISystem(false);
		IVS.AllowAudioPlayback(false);
		IVS.CreatePhysicsScene(true);

		World->InitWorld(UWorld::InitializationValues(IVS));
		World->PersistentLevel->UpdateModelComponents();
		World->UpdateWorldComponents(true, false);
	}

	// Retrieve the world partition.
	UWorldPartitionSubsystem* WorldPartitionSubsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
	UWorldPartition* WorldPartition = WorldPartitionSubsystem && WorldPartitionSubsystem->IsEnabled() ? World->GetWorldPartition() : nullptr;
	if (!WorldPartition)
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Commandlet only works on partitioned maps."));
		return 1;
	}

	FWorldContext& WorldContext = GEditor->GetEditorWorldContext(true);
	WorldContext.SetCurrentWorld(World);
	GWorld = World;
	
	// For now, load all cells
	// In the future, we'll want the commandlet to be able to perform partial updates of the map
	// to allow HLOD rebuild to be distributed on multiple machines.
	const FBox LoadBox(FVector(-WORLD_MAX, -WORLD_MAX, -WORLD_MAX), FVector(WORLD_MAX, WORLD_MAX, WORLD_MAX));
	WorldPartition->LoadEditorCells(LoadBox);

	TSet<UPackage*> HLODActorPackages;

	// Gather all packages before any HLOD actor is deleted
	for (TActorIterator<AWorldPartitionHLOD> ItHLOD(World); ItHLOD; ++ItHLOD)
	{
		HLODActorPackages.Add(ItHLOD->GetPackage());
	}

	// Rebuild HLOD for the whole world
	WorldPartition->GenerateHLOD();
	
	// Gather all packages again to include newly created HLOD actors
	for (TActorIterator<AWorldPartitionHLOD> ItHLOD(World); ItHLOD; ++ItHLOD)
	{
		HLODActorPackages.Add(ItHLOD->GetPackage());
	}

	TArray<UPackage*> PackagesToSave;
	TArray<UPackage*> PackagesToDelete;

	for (UPackage* HLODActorPackage : HLODActorPackages)
	{
		if (HLODActorPackage && HLODActorPackage->IsDirty())
		{
			if (UPackage::IsEmptyPackage(HLODActorPackage))
			{
				PackagesToDelete.Add(HLODActorPackage);
			}
			else
			{
				PackagesToSave.Add(HLODActorPackage);
			}
		}
	}

	// Delete packages
	if (!PackagesToDelete.IsEmpty())
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Log, TEXT("Deleting %d packages."), PackagesToDelete.Num());
		if (!PackageHelper.Delete(PackagesToDelete))
		{
			UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Error deleting packages."));
			return 1;
		}
	}

	// Save packages
	if (!PackagesToSave.IsEmpty())
	{
		// Checkout packages
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Log, TEXT("Checking out %d actor packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.Checkout(Package))
			{
				UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Error checking out package %s."), *Package->GetName());
				return 1;
			}
		}

		// Save packages
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.Save(Package))
			{
				UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Error saving package %s."), *Package->GetName());
				return 1;
			}
		}

		// Add new packages to source control
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Log, TEXT("Adding packages to source control."));
		for (UPackage* Package : PackagesToSave)
		{
			if (!PackageHelper.AddToSourceControl(Package))
			{
				UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Error adding package %s to source control."), *Package->GetName());
				return 1;
			}
		}
	}

	// Save the map itself
	if (!PackageHelper.Save(MapPackage))
	{
		UE_LOG(LogWorldPartitionBuildHLODsCommandlet, Error, TEXT("Error saving map package %s."), *MapPackage->GetName());
		return 1;
	}

	// Cleanup
	World->RemoveFromRoot();
	WorldContext.SetCurrentWorld(nullptr);
	GWorld = nullptr;

	return 0;
}
