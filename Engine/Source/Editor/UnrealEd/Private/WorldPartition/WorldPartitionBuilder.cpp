// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionBuilder.h"

#include "CoreMinimal.h"
#include "Editor.h"
#include "EditorWorldUtils.h"
#include "EngineModule.h"
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

FCellInfo::FCellInfo()
	: Location(ForceInitToZero)
	, Bounds(ForceInitToZero)
	, EditorBounds(ForceInitToZero)
	, IterativeCellSize(102400)
{
}

UWorldPartitionBuilder::UWorldPartitionBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSubmit = FParse::Param(FCommandLine::Get(), TEXT("Submit"));
}

bool UWorldPartitionBuilder::RunBuilder(UWorld* World)
{
	// Load configuration file & builder configuration
	const FString WorldConfigFilename = FPackageName::LongPackageNameToFilename(World->GetPackage()->GetName(), TEXT(".ini"));
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*WorldConfigFilename))
	{
		LoadConfig(GetClass(), *WorldConfigFilename);
	}

	// Validate builder settings
	if (IsRunningCommandlet() && RequiresCommandletRendering() && !IsAllowCommandletRendering())
	{
		UE_LOG(LogWorldPartitionBuilder, Error, TEXT("The option \"-AllowCommandletRendering\" must be provided for the %s process to work"), *GetClass()->GetName());
		return false;
	}

	FPackageSourceControlHelper SCCHelper;

	// Perform builder pre world initialisation
	if (!PreWorldInitialization(SCCHelper))
	{
		UE_LOG(LogWorldPartitionBuilder, Error, TEXT("PreWorldInitialization failed"));
		return false;
	}

	bool bResult = true;
	// Setup the world
	{
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
			bResult = Run(World, SCCHelper);

			// Restore previous world
			WorldContext.SetCurrentWorld(PrevGWorld);
			GWorld = PrevGWorld;

			// Save default configuration
			if (bResult)
			{
				if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*WorldConfigFilename) ||
					!FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*WorldConfigFilename))
				{
					SaveConfig(CPF_Config, *WorldConfigFilename);
				}
			}
		}
		else
		{
			UE_LOG(LogWorldPartitionBuilder, Error, TEXT("WorldPartition builders only works on partitioned maps."));
			bResult = false;
		}
	}

	if (bResult)
	{
		bResult = PostWorldTeardown(SCCHelper);
	}

	return bResult;
}

static FIntVector GetCellCoord(const FVector& InPos, const int32 InCellSize)
{
	return FIntVector(
		FMath::FloorToInt(InPos.X / InCellSize),
		FMath::FloorToInt(InPos.Y / InCellSize),
		FMath::FloorToInt(InPos.Z / InCellSize)
	);
}

bool UWorldPartitionBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	// Notify derived classes that partition building process starts
	bool bResult = PreRun(World, PackageHelper);

	UWorldPartition* WorldPartition = World->GetWorldPartition();

	// Properly Setup DataLayers for Builder
	if (const AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers())
	{
		// Load Data Layers
		bool bUpdateEditorCells = false;
		WorldDataLayers->ForEachDataLayer([&bUpdateEditorCells, this](UDataLayer* DataLayer)
		{
			// Load all Non DynamicallyLoaded Data Layers + Initially Active Data Layers + Data Layers provided by builder
			const bool bLoadedInEditor = (bLoadNonDynamicDataLayers && !DataLayer->IsRuntime()) ||
										 (bLoadInitiallyActiveDataLayers && DataLayer->GetInitialRuntimeState() == EDataLayerRuntimeState::Activated) ||
										 DataLayerLabels.Contains(DataLayer->GetDataLayerLabel());
			if (DataLayer->IsLoadedInEditor() != bLoadedInEditor)
			{
				bUpdateEditorCells = true;
				DataLayer->SetIsLoadedInEditor(bLoadedInEditor, /*bFromUserChange*/false);
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

	const ELoadingMode LoadingMode = GetLoadingMode();
	FCellInfo CellInfo;

	CellInfo.EditorBounds = WorldPartition->GetEditorWorldBounds();
	CellInfo.IterativeCellSize = IterativeCellSize;

	if ((LoadingMode == ELoadingMode::IterativeCells) || (LoadingMode == ELoadingMode::IterativeCells2D))
	{
		// do partial loading loop that calls RunInternal
		auto CanIterateZ = [](const bool bInResult, const ELoadingMode InLoadingMode, const int32 InZ, const int32 InMinZ, const int32 InMaxZ) -> bool
		{
			if (InLoadingMode == ELoadingMode::IterativeCells2D)
			{
				return bInResult && (InZ == InMinZ);
			}

			return bInResult && (InZ <= InMaxZ);
		};

		const FIntVector MinCellCoords = GetCellCoord(CellInfo.EditorBounds.Min, IterativeCellSize);
		const FIntVector MaxCellCoords = GetCellCoord(CellInfo.EditorBounds.Max, IterativeCellSize);

		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Iterative Cell Mode"));
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Cell Size %d"), IterativeCellSize);
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Cell Overlap %d"), IterativeCellOverlapSize);
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("WorldBounds: Min %s, Max %s"), *CellInfo.EditorBounds.Min.ToString(), *CellInfo.EditorBounds.Max.ToString());
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Iteration Count: %d"), ((LoadingMode == ELoadingMode::IterativeCells2D) ? 1 : (MaxCellCoords.Z-MinCellCoords.Z + 1)) * (MaxCellCoords.Y-MinCellCoords.Y + 1) * (MaxCellCoords.X - MinCellCoords.X + 1));
		
		FBox LoadedBounds(ForceInit);

		for (int32 z = MinCellCoords.Z; CanIterateZ(bResult, LoadingMode, z, MinCellCoords.Z, MaxCellCoords.Z); z++)
		{
			for (int32 y = MinCellCoords.Y; bResult && (y <= MaxCellCoords.Y); y++)
			{
				for (int32 x = MinCellCoords.X; bResult && (x <= MaxCellCoords.X); x++)
				{
					FVector Min(x * IterativeCellSize, y * IterativeCellSize, z * IterativeCellSize);
					FVector Max = Min + FVector(IterativeCellSize);

					if (LoadingMode == ELoadingMode::IterativeCells2D)
					{
						Min.Z = CellInfo.EditorBounds.Min.Z;
						Max.Z = CellInfo.EditorBounds.Max.Z;
					}

					FBox BoundsToLoad(Min, Max);
					BoundsToLoad = BoundsToLoad.ExpandBy(IterativeCellOverlapSize);

					CellInfo.Location = FIntVector(x, y, z);
					CellInfo.Bounds = BoundsToLoad;

					UE_LOG(LogWorldPartitionBuilder, Verbose, TEXT("Loading Bounds: Min %s, Max %s"), *BoundsToLoad.Min.ToString(), *BoundsToLoad.Max.ToString());
					WorldPartition->LoadEditorCells(BoundsToLoad, false);
					LoadedBounds += BoundsToLoad;

					bResult = RunInternal(World, CellInfo, PackageHelper);

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

						ENQUEUE_RENDER_COMMAND(VirtualTextureScalability_Release)([](FRHICommandList& RHICmdList)
						{
							GetRendererModule().ReleaseVirtualTexturePendingResources();
						});
					}
				}
			}
		}
	}
	else
	{
		FBox BoundsToLoad(ForceInit);
		if (LoadingMode == ELoadingMode::EntireWorld)
		{
			BoundsToLoad += FBox(FVector(-WORLDPARTITION_MAX, -WORLDPARTITION_MAX, -WORLDPARTITION_MAX), FVector(WORLDPARTITION_MAX, WORLDPARTITION_MAX, WORLDPARTITION_MAX));
			WorldPartition->LoadEditorCells(BoundsToLoad, false);
		}

		CellInfo.Bounds = BoundsToLoad;

		bResult = RunInternal(World, CellInfo, PackageHelper);
	}

	return PostRun(World, PackageHelper, bResult);
}