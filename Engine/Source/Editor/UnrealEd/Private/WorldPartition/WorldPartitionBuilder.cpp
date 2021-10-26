// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionBuilder.h"

#include "CoreMinimal.h"
#include "Editor.h"
#include "EditorWorldUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformFileManager.h"
#include "StaticMeshCompiler.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Math/IntVector.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuilder, Log, All);


UWorldPartitionBuilder::UWorldPartitionBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSubmit = FParse::Param(FCommandLine::Get(), TEXT("Submit"));
}

bool UWorldPartitionBuilder::RunBuilder(TSubclassOf<UWorldPartitionBuilder> BuilderClass, UWorld* World)
{
	// Create builder instance
	UWorldPartitionBuilder* Builder = NewObject<UWorldPartitionBuilder>(GetTransientPackage(), BuilderClass);
	if (!Builder)
	{
		UE_LOG(LogWorldPartitionBuilder, Error, TEXT("Failed to create builder."));
		return false;
	}

	Builder->AddToRoot();
	ON_SCOPE_EXIT
	{
		Builder->RemoveFromRoot();
	};

	return RunBuilder(Builder, World);
}

bool UWorldPartitionBuilder::RunBuilder(UWorldPartitionBuilder* Builder, UWorld* World)
{
	// Load configuration file & builder configuration
	FString WorldConfigFilename = FPackageName::LongPackageNameToFilename(World->GetPackage()->GetName(), TEXT(".ini"));
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*WorldConfigFilename))
	{
		Builder->LoadConfig(Builder->GetClass(), *WorldConfigFilename);
	}

	// Validate builder settings
	if (IsRunningCommandlet() && Builder->RequiresCommandletRendering() && !IsAllowCommandletRendering())
	{
		UE_LOG(LogWorldPartitionBuilder, Error, TEXT("The option \"-AllowCommandletRendering\" must be provided for the %s process to work"), *Builder->GetClass()->GetName());
		return false;
	}

	FPackageSourceControlHelper SCCHelper;

	// Perform builder pre world initialisation
	if (!Builder->PreWorldInitialization(SCCHelper))
	{
		UE_LOG(LogWorldPartitionBuilder, Error, TEXT("PreWorldInitialization failed"));
		return false;
	}

	// Setup the world
	UWorld::InitializationValues IVS;
	IVS.RequiresHitProxies(false);
	IVS.ShouldSimulatePhysics(false);
	IVS.EnableTraceCollision(false);
	IVS.CreateNavigation(false);
	IVS.CreateAISystem(false);
	IVS.AllowAudioPlayback(false);
	IVS.CreatePhysicsScene(true);
	FScopedEditorWorld EditorWorld(World, IVS);

	// Make sure the world is partitioned
	bool bResult = true;
	if (World->HasSubsystem<UWorldPartitionSubsystem>())
	{
		// Ensure the world has a valid world partition.
		UWorldPartition* WorldPartition = World->GetWorldPartition();
		check(WorldPartition);

		FWorldContext& WorldContext = GEditor->GetEditorWorldContext(true /*bEnsureIsGWorld*/);
		WorldContext.SetCurrentWorld(World);
		UWorld* PrevGWorld = GWorld;
		GWorld = World;

		// Run builder
		bResult = Builder->Run(World, SCCHelper);

		// Restore previous world
		WorldContext.SetCurrentWorld(PrevGWorld);
		GWorld = PrevGWorld;

		// Save default configuration
		if (bResult)
		{
			if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*WorldConfigFilename) ||
				!FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*WorldConfigFilename))
			{
				Builder->SaveConfig(CPF_Config, *WorldConfigFilename);
			}
		}
	}
	else
	{
		UE_LOG(LogWorldPartitionBuilder, Error, TEXT("WorldPartition builders only works on partitioned maps."));
		bResult = false;
	}

	return bResult;
}

bool UWorldPartitionBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	UWorldPartition* WorldPartition = World->GetWorldPartition();

	// Properly Setup DataLayers for Builder
	if (const AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers())
	{
		// Load Data Layers
		bool bUpdateEditorCells = false;
		WorldDataLayers->ForEachDataLayer([&bUpdateEditorCells, this](UDataLayer* DataLayer)
		{
			// Load all Non DynamicallyLoaded Data Layers + Initially Active Data Layers + Data Layers provided by builder
			const bool bLoadedInEditor = (bLoadNonDynamicDataLayers && !DataLayer->IsDynamicallyLoaded()) || 
										 (bLoadInitiallyActiveDataLayers && DataLayer->GetInitialState() == EDataLayerState::Activated) ||
										 DataLayerLabels.Contains(DataLayer->GetDataLayerLabel());
			if (DataLayer->IsDynamicallyLoadedInEditor() != bLoadedInEditor)
			{
				bUpdateEditorCells = true;
				DataLayer->SetIsDynamicallyLoadedInEditor(bLoadedInEditor, false);
				if (RequiresCommandletRendering() && bLoadedInEditor)
				{
					DataLayer->SetIsInitiallyVisible(true);
				}
			}
			
			UE_LOG(LogWorldPartitionBuilder, Display, TEXT("DataLayer '%s' Loaded: %d"), *UDataLayer::GetDataLayerText(DataLayer).ToString(), bLoadedInEditor ? 1 : 0);
			
			return true;
		});

		if (bUpdateEditorCells)
		{
			UE_LOG(LogWorldPartitionBuilder, Display, TEXT("DataLayer load state changed refreshing editor cells"));
			WorldPartition->RefreshLoadedEditorCells(false);
		}
	}
		
	ELoadingMode LoadingMode = GetLoadingMode();
	if (LoadingMode == ELoadingMode::IterativeCells)
	{
		// do partial loading loop that calls RunInternal
		FBox EditorBounds = WorldPartition->GetEditorWorldBounds();

		auto GetCellCoord = [](FVector InPos, int32 InCellSize)
		{
			return FIntVector(
				FMath::FloorToInt(InPos.X / InCellSize),
				FMath::FloorToInt(InPos.Y / InCellSize),
				FMath::FloorToInt(InPos.Z / InCellSize)
			);
		};

		const FIntVector MinCellCoords = GetCellCoord(EditorBounds.Min, IterativeCellSize);
		const FIntVector MaxCellCoords = GetCellCoord(EditorBounds.Max, IterativeCellSize);

		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Iterative Cell Mode"));
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Cell Size %d"), IterativeCellSize);
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Cell Overlap %d"), IterativeCellOverlapSize);
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("WorldBounds: Min %s, Max %s"), *EditorBounds.Min.ToString(), *EditorBounds.Max.ToString());
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Iteration Count: %d"), (MaxCellCoords.Z-MinCellCoords.Z) * (MaxCellCoords.Y-MinCellCoords.Y) * (MaxCellCoords.X - MinCellCoords.X));
		

		FBox LoadedBounds(ForceInit);
		for (int32 z = MinCellCoords.Z; z <= MaxCellCoords.Z; z++)
		{
			for (int32 y = MinCellCoords.Y; y <= MaxCellCoords.Y; y++)
			{
				for (int32 x = MinCellCoords.X; x <= MaxCellCoords.X; x++)
				{
					const FVector Min(x * IterativeCellSize, y * IterativeCellSize, z * IterativeCellSize);
					const FVector Max = Min + FVector(IterativeCellSize);
					FBox BoundsToLoad(Min, Max);
					BoundsToLoad = BoundsToLoad.ExpandBy(IterativeCellOverlapSize);

					UE_LOG(LogWorldPartitionBuilder, Verbose, TEXT("Loading Bounds: Min %s, Max %s"), *BoundsToLoad.Min.ToString(), *BoundsToLoad.Max.ToString());
					WorldPartition->LoadEditorCells(BoundsToLoad, false);
					LoadedBounds += BoundsToLoad;


					if (!RunInternal(World, BoundsToLoad, PackageHelper))
					{
						return false;
					}

					if (FWorldPartitionHelpers::HasExceededMaxMemory())
					{
						WorldPartition->UnloadEditorCells(LoadedBounds, false);
						// Reset Loaded Bounds
						LoadedBounds.Init();

						FWorldPartitionHelpers::DoCollectGarbage();
					}

					// When running with -AllowCommandletRendering we want to simulate an engine tick
					if (IsAllowCommandletRendering())
					{
						FWorldPartitionHelpers::FakeEngineTick(World);
					}
				}
			}
		}

		return true;
	}
	else
	{
		FBox BoundsToLoad(ForceInit);
		if (LoadingMode == ELoadingMode::EntireWorld)
		{
			BoundsToLoad += FBox(FVector(-WORLD_MAX, -WORLD_MAX, -WORLD_MAX), FVector(WORLD_MAX, WORLD_MAX, WORLD_MAX));
			WorldPartition->LoadEditorCells(BoundsToLoad, false);
		}

		return RunInternal(World, BoundsToLoad, PackageHelper);
	}
}
