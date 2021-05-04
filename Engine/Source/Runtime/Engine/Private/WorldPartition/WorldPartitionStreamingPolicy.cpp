// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionStreamingPolicy Implementation
 */

#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "RenderUtils.h"
#include "Algo/RemoveIf.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

UWorldPartitionStreamingPolicy::UWorldPartitionStreamingPolicy(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer) 
, bIsServerLoadingDone(false)
{
	if (!IsTemplate())
	{
		WorldPartition = GetOuterUWorldPartition();
		check(WorldPartition);
	}
}

void UWorldPartitionStreamingPolicy::UpdateStreamingSources()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::UpdateStreamingSources);

	StreamingSources.Reset();

	if (!WorldPartition->IsInitialized())
	{
		return;
	}

	const FTransform WorldToLocal = WorldPartition->GetInstanceTransform().Inverse();

#if WITH_EDITOR
	// We are in the SIE
	if (UWorldPartition::IsSimulating())
	{
		// Transform to Local
		const FVector ViewLocation = GCurrentLevelEditingViewportClient->GetViewLocation();
		const FRotator ViewRotation = GCurrentLevelEditingViewportClient->GetViewRotation();
		const FVector ViewLocationLocal = WorldToLocal.TransformPosition(ViewLocation);
		const FRotator ViewRotationLocal = WorldToLocal.TransformRotation(ViewRotation.Quaternion()).Rotator();
		static const FName NAME_SIE(TEXT("SIE"));
		StreamingSources.Add(FWorldPartitionStreamingSource(NAME_SIE, ViewLocationLocal, ViewRotationLocal, EStreamingSourceTargetState::Activated));
	}
	else
#endif
	{
		UWorld* World = WorldPartition->GetWorld();
		if (World->GetNetMode() != NM_DedicatedServer)
		{
			const int32 NumPlayers = GEngine->GetNumGamePlayers(World);
			for (int32 PlayerIndex = 0; PlayerIndex < NumPlayers; ++PlayerIndex)
			{
				ULocalPlayer* Player = GEngine->GetGamePlayer(World, PlayerIndex);
				if (Player && Player->PlayerController)
				{
					FVector ViewLocation;
					FRotator ViewRotation;
					Player->PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

					//@todo_ow: this test is to cover cases where GetPlayerViewPoint returns (0,0,0) when invalid. It should probably return a bool
					//          to indicate that the returned position is invalid.
					if (!ViewLocation.IsZero())
					{
						// Transform to Local
						ViewLocation = WorldToLocal.TransformPosition(ViewLocation);
						ViewRotation = WorldToLocal.TransformRotation(ViewRotation.Quaternion()).Rotator();
						StreamingSources.Add(FWorldPartitionStreamingSource(Player->GetFName(), ViewLocation, ViewRotation, EStreamingSourceTargetState::Activated));
					}
				}
			}
		}
	}

	for (IWorldPartitionStreamingSourceProvider* StreamingSourceProvider : WorldPartition->StreamingSourceProviders)
	{
		FWorldPartitionStreamingSource StreamingSource;
		if (StreamingSourceProvider->GetStreamingSource(StreamingSource))
		{
			// Transform to Local
			StreamingSource.Location = WorldToLocal.TransformPosition(StreamingSource.Location);
			StreamingSource.Rotation = WorldToLocal.TransformRotation(StreamingSource.Rotation.Quaternion()).Rotator();
			// If none is provided, default Streaming Source provider's priority to be less than those based on player controllers
			if (StreamingSource.Priority == EStreamingSourcePriority::Default)
			{
				StreamingSource.Priority = EStreamingSourcePriority::Low;
			}

			StreamingSources.Add(StreamingSource);
		}
	}
}

void UWorldPartitionStreamingPolicy::UpdateStreamingState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::UpdateStreamingState);

	UWorld* World = WorldPartition->GetWorld();
	check(World && World->IsGameWorld());
	
	// Update streaming sources
	UpdateStreamingSources();

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		// Load all cells on server (do this once)
		if (!bIsServerLoadingDone)
		{
			TSet<const UWorldPartitionRuntimeCell*> AllStreamingCells;
			WorldPartition->RuntimeHash->GetAllStreamingCells(AllStreamingCells);
			TSet<const UWorldPartitionRuntimeCell*> ToActivateCells = AllStreamingCells.Difference(ActivatedCells);

			// Mark Runtime Cells to be Always Loaded
			for (const UWorldPartitionRuntimeCell* Cell : ToActivateCells)
			{
				UWorldPartitionRuntimeCell* MutableCell = const_cast<UWorldPartitionRuntimeCell*>(Cell);
				MutableCell->SetIsAlwaysLoaded(true);
			}

			SetTargetStateForCells(EWorldPartitionRuntimeCellState::Activated, ToActivateCells);
			bIsServerLoadingDone = true;
		}
	}
	else
	{
		// Early out if nothing loaded and no streaming source
		if ((StreamingSources.Num() == 0) && (ActivatedCells.Num() == 0) && (LoadedCells.Num() == 0))
		{
			return;
		}

		UWorldPartitionRuntimeHash::FStreamingSourceCells ActivateCells;
		UWorldPartitionRuntimeHash::FStreamingSourceCells LoadCells;
		TSet<const UWorldPartitionRuntimeCell*>& LoadStreamingCells = LoadCells.GetCells();
		TSet<const UWorldPartitionRuntimeCell*>& ActivateStreamingCells = ActivateCells.GetCells();

		// When uninitializing, UpdateStreamingState is called, but we don't want any cells to be loaded
		if (WorldPartition->IsInitialized())
		{
			UWorldPartitionRuntimeCell::DirtyStreamingSourceCacheEpoch();
			WorldPartition->RuntimeHash->GetStreamingCells(StreamingSources, ActivateCells, LoadCells);

			// Activation superseeds Loading
			LoadStreamingCells = LoadStreamingCells.Difference(ActivateStreamingCells);
		}

		if (!UHLODSubsystem::IsHLODEnabled())
		{
			// Remove all HLOD cells from the Activate & Load cells
			auto RemoveHLODCells = [](TSet<const UWorldPartitionRuntimeCell*>& Cells)
			{
				for (auto It = Cells.CreateIterator(); It; ++It)
				{
					if ((*It)->HasCellData<UWorldPartitionRuntimeHLODCellData>())
					{
						It.RemoveCurrent();
					}
				}
			};
			RemoveHLODCells(ActivateStreamingCells);
			RemoveHLODCells(LoadStreamingCells);
		}

		// Determine cells to load/unload
		TSet<const UWorldPartitionRuntimeCell*> ToActivateCells = ActivateStreamingCells.Difference(ActivatedCells);
		TSet<const UWorldPartitionRuntimeCell*> ToLoadCells = LoadStreamingCells.Difference(LoadedCells);
		TSet<const UWorldPartitionRuntimeCell*> ToUnloadCells = ActivatedCells.Union(LoadedCells).Difference(ActivateStreamingCells.Union(LoadStreamingCells));

		UE_SUPPRESS(LogWorldPartition, Verbose,
		{
			if (ToActivateCells.Num() > 0 || ToUnloadCells.Num() > 0)
			{
				UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy: CellsToLoad(%d), CellsToUnload(%d)"), ToActivateCells.Num(), ToUnloadCells.Num());
				
				FTransform LocalToWorld = WorldPartition->GetInstanceTransform();
				for (int i = 0; i < StreamingSources.Num(); ++i)
				{
					FVector ViewLocation = LocalToWorld.TransformPosition(StreamingSources[i].Location);
					FRotator ViewRotation = LocalToWorld.TransformRotation(StreamingSources[i].Rotation.Quaternion()).Rotator();
					UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy: Sources[%d] = %s,%s"), i, *ViewLocation.ToString(), *ViewRotation.ToString());
				}
			}
		});

		// Process unload first, so that LoadedCells is up-to-date when calling LoadCells
		if (ToUnloadCells.Num() > 0)
		{
			SetTargetStateForCells(EWorldPartitionRuntimeCellState::Unloaded, ToUnloadCells);
		}

		// Do Activation State first as it is higher prio than Load State (if we have a limited number of loading cells per frame)
		if (ToActivateCells.Num() > 0)
		{
			SetTargetStateForCells(EWorldPartitionRuntimeCellState::Activated, ToActivateCells);
		}

		if (ToLoadCells.Num() > 0)
		{
			SetTargetStateForCells(EWorldPartitionRuntimeCellState::Loaded, ToLoadCells);
		}
	}
}

bool UWorldPartitionStreamingPolicy::IsStreamingCompleted(EWorldPartitionRuntimeCellState QueryState, const TArray<FWorldPartitionStreamingQuerySource>& QuerySources, bool bExactState) const
{
	const UDataLayerSubsystem* DataLayerSubsystem = WorldPartition->GetWorld()->GetSubsystem<UDataLayerSubsystem>();
	const bool bIsHLODEnabled = UHLODSubsystem::IsHLODEnabled();

	for (const FWorldPartitionStreamingQuerySource& QuerySource : QuerySources)
	{
		TSet<const UWorldPartitionRuntimeCell*> Cells;
		WorldPartition->RuntimeHash->GetStreamingCells(QuerySource, Cells);

		for (const UWorldPartitionRuntimeCell* Cell : Cells)
		{
			EWorldPartitionRuntimeCellState CellState = GetCurrentStateForCell(Cell);
			if (CellState != QueryState)
			{
				bool bSkipCell = false;

				// Don't consider HLOD cells if HLODs are disabled.
				if (!bIsHLODEnabled)
				{
					bSkipCell = Cell->HasCellData<UWorldPartitionRuntimeHLODCellData>();
				}

				// If we are querying for Unloaded/Loaded but a Cell is part of a data layer outside of the query that is activated do not consider it
				if (!bSkipCell && QueryState < CellState)
				{
					for (const FName& CellDataLayer : Cell->GetDataLayers())
					{
						if (!QuerySource.DataLayers.Contains(CellDataLayer) && DataLayerSubsystem->GetDataLayerStateByName(CellDataLayer) > EDataLayerState::Unloaded)
						{
							bSkipCell = true;
							break;
						}
					}
				}
								
				if (!bSkipCell && (bExactState || CellState < QueryState))
				{
					return false;
				}
			}
		}	
	}

	return true;
}

FVector2D UWorldPartitionStreamingPolicy::GetDrawRuntimeHash2DDesiredFootprint(const FVector2D& CanvasSize)
{
	return WorldPartition->RuntimeHash->GetDraw2DDesiredFootprint(CanvasSize);
}

void UWorldPartitionStreamingPolicy::DrawRuntimeHash2D(class UCanvas* Canvas, const FVector2D& PartitionCanvasSize, FVector2D& Offset)
{
	if (StreamingSources.Num() > 0)
	{
		WorldPartition->RuntimeHash->Draw2D(Canvas, StreamingSources, PartitionCanvasSize, Offset);
	}
}

void UWorldPartitionStreamingPolicy::DrawRuntimeHash3D()
{
	UWorld* World = WorldPartition->GetWorld();
	if (World->GetNetMode() != NM_DedicatedServer && WorldPartition->IsInitialized())
	{
		WorldPartition->RuntimeHash->Draw3D(StreamingSources);
	}
}
