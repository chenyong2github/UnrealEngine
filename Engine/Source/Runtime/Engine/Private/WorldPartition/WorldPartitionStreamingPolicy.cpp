// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionStreamingPolicy Implementation
 */

#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartition.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "RenderUtils.h"

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
	StreamingSources.Reset();

	if (!WorldPartition->IsInitialized())
	{
		return;
	}

	UWorld* World = WorldPartition->GetWorld();
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		FTransform WorldToLocal = WorldPartition->GetInstanceTransform().Inverse();
		const int32 NumPlayers = GEngine->GetNumGamePlayers(World);
		for (int32 PlayerIndex = 0; PlayerIndex < NumPlayers; ++PlayerIndex)
		{
			ULocalPlayer* Player = GEngine->GetGamePlayer(World, PlayerIndex);
			if (Player && Player->PlayerController)
			{
				FVector ViewLocation;
				FRotator ViewRotation;
				Player->PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

				if (!ViewLocation.IsZero())
				{
					// Transform to Local
					ViewLocation = WorldToLocal.TransformPosition(ViewLocation);
					ViewRotation = WorldToLocal.TransformRotation(ViewRotation.Quaternion()).Rotator();
					StreamingSources.Add(FWorldPartitionStreamingSource(ViewLocation, ViewRotation));
				}
			}
		}
	}
}

void UWorldPartitionStreamingPolicy::UpdateStreamingState()
{
	TSet<const UWorldPartitionRuntimeCell*> StreamingCells;
	UWorld* World = WorldPartition->GetWorld();
	check(World && World->IsGameWorld());
	
	// Update streaming sources
	UpdateStreamingSources();

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		// Load all cells on server (do this once)
		if (!bIsServerLoadingDone)
		{
			WorldPartition->RuntimeHash->GetAllStreamingCells(StreamingCells);
			TSet<const UWorldPartitionRuntimeCell*> ToLoadCells = StreamingCells.Difference(LoadedCells);

			// Mark Runtime Cells to be Always Loaded
			for (const UWorldPartitionRuntimeCell* Cell : ToLoadCells)
			{
				UWorldPartitionRuntimeCell* MutableCell = const_cast<UWorldPartitionRuntimeCell*>(Cell);
				MutableCell->SetIsAlwaysLoaded(true);
			}

			LoadCells(ToLoadCells);
			bIsServerLoadingDone = true;
		}
	}
	else
	{
		// Early out if nothing loaded and no streaming source
		if ((StreamingSources.Num() == 0) && (LoadedCells.Num() == 0))
		{
			return;
		}

		// When uninitializing, UpdateStreamingState is called, but we don't want any cells to be loaded
		if (WorldPartition->IsInitialized())
		{
			WorldPartition->RuntimeHash->GetStreamingCells(StreamingSources, StreamingCells);
		}

		// Determine cells to load/unload
		TSet<const UWorldPartitionRuntimeCell*> ToLoadCells = StreamingCells.Difference(LoadedCells);
		TSet<const UWorldPartitionRuntimeCell*> ToUnloadCells = LoadedCells.Difference(StreamingCells);

		UE_SUPPRESS(LogWorldPartition, Verbose,
		{
			if (ToLoadCells.Num() > 0 || ToUnloadCells.Num() > 0)
			{
				UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy: CellsToLoad(%d), CellsToUnload(%d)"), ToLoadCells.Num(), ToUnloadCells.Num());
				
				FTransform WorldToLocal = WorldPartition->GetInstanceTransform();
				for (int i = 0; i < StreamingSources.Num(); ++i)
				{
					FVector ViewLocation = WorldToLocal.TransformPosition(StreamingSources[i].Location);
					FRotator ViewRotation = WorldToLocal.TransformRotation(StreamingSources[i].Rotation.Quaternion()).Rotator();
					UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy: Sources[%d] = %s,%s"), i, *ViewLocation.ToString(), *ViewRotation.ToString());
				}
			}
		});

		// Process unload first, so that LoadedCells is up-to-date when calling LoadCells
		if (ToUnloadCells.Num() > 0)
		{
			UnloadCells(ToUnloadCells);
		}

		if (ToLoadCells.Num() > 0)
		{
			LoadCells(ToLoadCells);
		}
	}
}

FVector2D UWorldPartitionStreamingPolicy::GetShowDebugDesiredFootprint(const FVector2D& CanvasSize)
{
	return WorldPartition->RuntimeHash->GetShowDebugDesiredFootprint(CanvasSize);
}

void UWorldPartitionStreamingPolicy::ShowDebugInfo(class UCanvas* Canvas, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize)
{
	if (StreamingSources.Num() > 0)
	{
		WorldPartition->RuntimeHash->ShowDebugInfo(Canvas, StreamingSources, PartitionCanvasOffset, PartitionCanvasSize);
	}
}

void UWorldPartitionStreamingPolicy::LoadCells(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells)
{
	for (const UWorldPartitionRuntimeCell* ToLoadCell : ToLoadCells)
	{
		LoadCell(ToLoadCell);
		LoadedCells.Add(ToLoadCell);
	}
}

void UWorldPartitionStreamingPolicy::UnloadCells(const TSet<const UWorldPartitionRuntimeCell*>& ToUnloadCells)
{
	for (const UWorldPartitionRuntimeCell* ToUnloadCell : ToUnloadCells)
	{
		UnloadCell(ToUnloadCell);
		LoadedCells.Remove(ToUnloadCell);
	}
}
