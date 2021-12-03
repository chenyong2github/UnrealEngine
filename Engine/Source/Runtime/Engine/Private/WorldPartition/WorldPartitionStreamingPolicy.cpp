// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionStreamingPolicy Implementation
 */

#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionReplay.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "WorldPartition/WorldPartitionDebugHelper.h"
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

#define LOCTEXT_NAMESPACE "WorldPartitionStreamingPolicy"

static int32 GUpdateStreamingSources = 1;
static FAutoConsoleVariableRef CVarUpdateStreamingSources(
	TEXT("wp.Runtime.UpdateStreamingSources"),
	GUpdateStreamingSources,
	TEXT("Set to 0 to stop updating (freeze) world partition streaming sources."));

static int32 GEnableSimulationStreamingSource = 1;
static FAutoConsoleVariableRef CVarEnableSimulationStreamingSource(
	TEXT("wp.Runtime.EnableSimulationStreamingSource"),
	GEnableSimulationStreamingSource,
	TEXT("Set to 0 to if you want to disable the simulation/ejected camera streaming source."));

static int32 GMaxLoadingStreamingCells = 4;
static FAutoConsoleVariableRef CMaxLoadingStreamingCells(
	TEXT("wp.Runtime.MaxLoadingStreamingCells"),
	GMaxLoadingStreamingCells,
	TEXT("Used to limit the number of concurrent loading world partition streaming cells."));

int32 GBlockOnSlowStreaming = 1;
static FAutoConsoleVariableRef CVarBlockOnSlowStreaming(
	TEXT("wp.Runtime.BlockOnSlowStreaming"),
	GBlockOnSlowStreaming,
	TEXT("Set if streaming needs to block when to slow to catchup."));


UWorldPartitionStreamingPolicy::UWorldPartitionStreamingPolicy(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer) 
, bCriticalPerformanceRequestedBlockTillOnWorld(false)
, CriticalPerformanceBlockTillLevelStreamingCompletedEpoch(0)
, DataLayersStatesServerEpoch(INT_MIN)
, StreamingPerformance(EWorldPartitionStreamingPerformance::Good)
#if !UE_BUILD_SHIPPING
, OnScreenMessageStartTime(0.0)
, OnScreenMessageStreamingPerformance(EWorldPartitionStreamingPerformance::Good)
#endif
{
	if (!IsTemplate())
	{
		WorldPartition = GetOuterUWorldPartition();
		check(WorldPartition);
	}
}

void UWorldPartitionStreamingPolicy::UpdateStreamingSources()
{
	if (!GUpdateStreamingSources)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::UpdateStreamingSources);

	StreamingSources.Reset();

	if (!WorldPartition->IsInitialized())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	bool bUseReplaySources = false;
	if (AWorldPartitionReplay* Replay = WorldPartition->Replay)
	{
		bUseReplaySources = Replay->GetReplayStreamingSources(StreamingSources);
	}

	if (!bUseReplaySources)
#endif
	{
#if WITH_EDITOR
		// We are in the SIE
		if (GEnableSimulationStreamingSource && UWorldPartition::IsSimulating())
		{
			const FVector ViewLocation = GCurrentLevelEditingViewportClient->GetViewLocation();
			const FRotator ViewRotation = GCurrentLevelEditingViewportClient->GetViewRotation();
			static const FName NAME_SIE(TEXT("SIE"));
			StreamingSources.Add(FWorldPartitionStreamingSource(NAME_SIE, ViewLocation, ViewRotation, EStreamingSourceTargetState::Activated, /*bBlockOnSlowLoading=*/false, EStreamingSourcePriority::Default));
		}
		else
#endif
		{
			UWorld* World = WorldPartition->GetWorld();
			const ENetMode NetMode = World->GetNetMode();
			if (NetMode == NM_Standalone || NetMode == NM_Client || AWorldPartitionReplay::IsEnabled(World))
			{
				const int32 NumPlayers = GEngine->GetNumGamePlayers(World);
				for (int32 PlayerIndex = 0; PlayerIndex < NumPlayers; ++PlayerIndex)
				{
					ULocalPlayer* Player = GEngine->GetGamePlayer(World, PlayerIndex);
					if (Player && Player->PlayerController && Player->PlayerController->IsStreamingSourceEnabled())
					{
						FVector ViewLocation;
						FRotator ViewRotation;
						Player->PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

				
						const EStreamingSourceTargetState TargetState = Player->PlayerController->StreamingSourceShouldActivate() ? EStreamingSourceTargetState::Activated : EStreamingSourceTargetState::Loaded;
						const bool bBlockOnSlowLoading = Player->PlayerController->StreamingSourceShouldBlockOnSlowStreaming();
						StreamingSources.Add(FWorldPartitionStreamingSource(Player->PlayerController->GetFName(), ViewLocation, ViewRotation, TargetState, bBlockOnSlowLoading, EStreamingSourcePriority::Default));
					}
				}
			}
		}
	
		for (IWorldPartitionStreamingSourceProvider* StreamingSourceProvider : WorldPartition->StreamingSourceProviders)
		{
			FWorldPartitionStreamingSource StreamingSource;
			// Default Streaming Source provider's priority to be less than those based on player controllers
			StreamingSource.Priority = EStreamingSourcePriority::Low;
			if (StreamingSourceProvider->GetStreamingSource(StreamingSource))
			{
				StreamingSources.Add(StreamingSource);
			}
		}
	}
		
	// Update streaming sources velocity
	const float CurrentTime = WorldPartition->GetWorld()->GetTimeSeconds();
	TSet<FName, DefaultKeyFuncs<FName>, TInlineSetAllocator<8>> ValidStreamingSources;
	for (FWorldPartitionStreamingSource& StreamingSource : StreamingSources)
	{
		if (!StreamingSource.Name.IsNone())
		{
			ValidStreamingSources.Add(StreamingSource.Name);
			FStreamingSourceVelocity& SourceVelocity = StreamingSourcesVelocity.FindOrAdd(StreamingSource.Name, FStreamingSourceVelocity(StreamingSource.Name));
			StreamingSource.Velocity = SourceVelocity.GetAverageVelocity(StreamingSource.Location, CurrentTime);
		}
	}

	// Cleanup StreamingSourcesVelocity
	for (auto It(StreamingSourcesVelocity.CreateIterator()); It; ++It)
	{
		if (!ValidStreamingSources.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

#define WORLDPARTITION_LOG_UPDATESTREAMINGSTATE(Verbosity)\
UE_SUPPRESS(LogWorldPartition, Verbosity, \
{ \
	if (ToActivateCells.Num() > 0 || ToLoadCells.Num() > 0 || ToUnloadCells.Num() > 0) \
	{ \
		UE_LOG(LogWorldPartition, Verbosity, TEXT("UWorldPartitionStreamingPolicy: CellsToActivate(%d), CellsToLoad(%d), CellsToUnload(%d)"), ToActivateCells.Num(), ToLoadCells.Num(), ToUnloadCells.Num()); \
		for (int i = 0; i < StreamingSources.Num(); ++i) \
		{ \
			FVector ViewLocation = StreamingSources[i].Location; \
			FRotator ViewRotation = StreamingSources[i].Rotation; \
			UE_LOG(LogWorldPartition, Verbosity, TEXT("UWorldPartitionStreamingPolicy: Sources[%d] = %s,%s"), i, *ViewLocation.ToString(), *ViewRotation.ToString()); \
		} \
	} \
}) \

bool UWorldPartitionStreamingPolicy::IsInBlockTillLevelStreamingCompleted(bool bIsCausedByBadStreamingPerformance /* = false*/) const
{
	const UWorld* World = WorldPartition->GetWorld();
	const bool bIsInBlockTillLevelStreamingCompleted = World->GetIsInBlockTillLevelStreamingCompleted();
	if (bIsCausedByBadStreamingPerformance)
	{
		return bIsInBlockTillLevelStreamingCompleted &&
				(StreamingPerformance != EWorldPartitionStreamingPerformance::Good) &&
				(CriticalPerformanceBlockTillLevelStreamingCompletedEpoch == World->GetBlockTillLevelStreamingCompletedEpoch());
	}
	return bIsInBlockTillLevelStreamingCompleted;
}

void UWorldPartitionStreamingPolicy::UpdateStreamingState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::UpdateStreamingState);

	UWorld* World = WorldPartition->GetWorld();
	check(World && World->IsGameWorld());

	// Dermine if the World's BlockTillLevelStreamingCompleted was triggered by WorldPartitionStreamingPolicy
	if (bCriticalPerformanceRequestedBlockTillOnWorld && IsInBlockTillLevelStreamingCompleted())
	{
		bCriticalPerformanceRequestedBlockTillOnWorld = false;
		CriticalPerformanceBlockTillLevelStreamingCompletedEpoch = World->GetBlockTillLevelStreamingCompletedEpoch();
	}
	
	// Update streaming sources
	UpdateStreamingSources();
		
	TSet<const UWorldPartitionRuntimeCell*>& ActivateStreamingCells = FrameActivateCells.GetCells();
	TSet<const UWorldPartitionRuntimeCell*>& LoadStreamingCells = FrameLoadCells.GetCells();
	check(ActivateStreamingCells.IsEmpty());
	check(LoadStreamingCells.IsEmpty());

	const ENetMode NetMode = World->GetNetMode();
	if (NetMode == NM_Standalone || NetMode == NM_Client || AWorldPartitionReplay::IsEnabled(World))
	{
		// When uninitializing, UpdateStreamingState is called, but we don't want any cells to be loaded
		if (WorldPartition->IsInitialized())
		{
			UWorldPartitionRuntimeCell::DirtyStreamingSourceCacheEpoch();
			WorldPartition->RuntimeHash->GetStreamingCells(StreamingSources, FrameActivateCells, FrameLoadCells);

			// Activation superseeds Loading
			LoadStreamingCells = LoadStreamingCells.Difference(ActivateStreamingCells);
		}
	}
	else 
	{
		// Server will activate all non data layer cells at first and then load/activate/unload data layer cells only when the data layer states change
		if (DataLayersStatesServerEpoch == AWorldDataLayers::GetDataLayersStateEpoch())
		{
			// Server as nothing to do early out
			return; 
		}

		const UDataLayerSubsystem* DataLayerSubsystem = WorldPartition->GetWorld()->GetSubsystem<UDataLayerSubsystem>();
		DataLayersStatesServerEpoch = AWorldDataLayers::GetDataLayersStateEpoch();

		// Non Data Layer Cells + Active Data Layers
		WorldPartition->RuntimeHash->GetAllStreamingCells(ActivateStreamingCells, /*bAllDataLayers=*/ false, /*bDataLayersOnly=*/ false, DataLayerSubsystem->GetEffectiveActiveDataLayerNames());

		// Loaded Data Layers Cells only
		if (DataLayerSubsystem->GetEffectiveLoadedDataLayerNames().Num())
		{
			WorldPartition->RuntimeHash->GetAllStreamingCells(LoadStreamingCells, /*bAllDataLayers=*/ false, /*bDataLayersOnly=*/ true, DataLayerSubsystem->GetEffectiveLoadedDataLayerNames());
		}
	}

	if (!UHLODSubsystem::IsHLODEnabled())
	{
		// Remove all HLOD cells from the Activate & Load cells
		auto RemoveHLODCells = [](TSet<const UWorldPartitionRuntimeCell*>& Cells)
		{
			for (auto It = Cells.CreateIterator(); It; ++It)
			{
				if ((*It)->GetIsHLOD())
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

#if !UE_BUILD_SHIPPING
	UpdateDebugCellsStreamingPriority(ActivateStreamingCells, LoadStreamingCells);
#endif

	if(World->bMatchStarted)
	{
		WORLDPARTITION_LOG_UPDATESTREAMINGSTATE(Verbose);
	}
	else
	{
		WORLDPARTITION_LOG_UPDATESTREAMINGSTATE(Log);
	}
	
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

	// Sort cells and update streaming priority 
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::SortCellsAndUpdateStreamingPriority);
		TSet<const UWorldPartitionRuntimeCell*> AddToWorldCells;
		for (const UWorldPartitionRuntimeCell* ActivatedCell : ActivatedCells)
		{
			if (!ActivatedCell->IsAddedToWorld() && !ActivatedCell->IsAlwaysLoaded())
			{
				AddToWorldCells.Add(ActivatedCell);
			}
		}
		WorldPartition->RuntimeHash->SortStreamingCellsByImportance(AddToWorldCells, StreamingSources, SortedAddToWorldCells);

		// Update level streaming priority so that UWorld::UpdateLevelStreaming will naturally process the levels in the correct order
		const int32 MaxPrio = SortedAddToWorldCells.Num();
		int32 Prio = MaxPrio;
		const ULevel* CurrentPendingVisibility = GetWorld()->GetCurrentLevelPendingVisibility();
		for (const UWorldPartitionRuntimeCell* Cell : SortedAddToWorldCells)
		{
			// Current pending visibility level is the most important
			const bool bIsCellPendingVisibility = CurrentPendingVisibility && (Cell->GetLevel() == CurrentPendingVisibility);
			const int32 SortedPriority = bIsCellPendingVisibility ? MaxPrio + 1 : Prio--;
			Cell->SetStreamingPriority(SortedPriority);
		}
	}

	// Evaluate streaming performance based on cells that should be activated
	UpdateStreamingPerformance(ActivateStreamingCells);
	
	// Reset frame StreamingSourceCells (optimization to avoid reallocation at every call to UpdateStreamingState)
	FrameActivateCells.Reset();
	FrameLoadCells.Reset();
}

#if !UE_BUILD_SHIPPING
void UWorldPartitionStreamingPolicy::UpdateDebugCellsStreamingPriority(const TSet<const UWorldPartitionRuntimeCell*>& ActivateStreamingCells, const TSet<const UWorldPartitionRuntimeCell*>& LoadStreamingCells)
{
	if (FWorldPartitionDebugHelper::IsRuntimeSpatialHashCellStreamingPriotityShown())
	{
		TSet<const UWorldPartitionRuntimeCell*> CellsToSort;
		CellsToSort.Append(ActivateStreamingCells);
		CellsToSort.Append(LoadStreamingCells);

		TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedCells;
		WorldPartition->RuntimeHash->SortStreamingCellsByImportance(CellsToSort, StreamingSources, SortedCells);
		const int32 CellCount = SortedCells.Num();
		int32 CellPrio = 0;
		for (const UWorldPartitionRuntimeCell* SortedCell : SortedCells)
		{
			const_cast<UWorldPartitionRuntimeCell*>(SortedCell)->SetDebugStreamingPriority(float(CellPrio++) / CellCount);
		}
	}
}
#endif

void UWorldPartitionStreamingPolicy::UpdateStreamingPerformance(const TSet<const UWorldPartitionRuntimeCell*>& CellsToActivate)
{		
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::UpdateStreamingPerformance);
	UWorld* World = GetWorld();
	// If we are currently in a blocked loading just reset the on screen message time and return
	if (StreamingPerformance == EWorldPartitionStreamingPerformance::Critical && IsInBlockTillLevelStreamingCompleted())
	{
#if !UE_BUILD_SHIPPING
		OnScreenMessageStartTime = FPlatformTime::Seconds();
#endif
		return;
	}

	EWorldPartitionStreamingPerformance NewStreamingPerformance = WorldPartition->RuntimeHash->GetStreamingPerformance(CellsToActivate);
		
	if (StreamingPerformance != NewStreamingPerformance)
	{
		UE_LOG(LogWorldPartition, Log, TEXT("Streaming performance changed: %s -> %s"),
			*StaticEnum<EWorldPartitionStreamingPerformance>()->GetDisplayNameTextByValue((int64)StreamingPerformance).ToString(),
			*StaticEnum<EWorldPartitionStreamingPerformance>()->GetDisplayNameTextByValue((int64)NewStreamingPerformance).ToString());
		
		StreamingPerformance = NewStreamingPerformance;
	}

#if !UE_BUILD_SHIPPING
	if (StreamingPerformance != EWorldPartitionStreamingPerformance::Good)
	{
		// performance still bad keep message alive
		OnScreenMessageStartTime = FPlatformTime::Seconds();
		OnScreenMessageStreamingPerformance = StreamingPerformance;
	}
#endif
	
	if (StreamingPerformance == EWorldPartitionStreamingPerformance::Critical)
	{
		// This is a first very simple standalone implementation of handling of critical streaming conditions.
		if (GBlockOnSlowStreaming && !IsInBlockTillLevelStreamingCompleted() && World->GetNetMode() == NM_Standalone)
		{
			World->bRequestedBlockOnAsyncLoading = true;
			bCriticalPerformanceRequestedBlockTillOnWorld = true;
		}
	}
}

#if !UE_BUILD_SHIPPING
void UWorldPartitionStreamingPolicy::GetOnScreenMessages(FCoreDelegates::FSeverityMessageMap& OutMessages)
{
	// Keep displaying for 2 seconds (or more if health stays bad)
	double DisplayTime = FPlatformTime::Seconds() - OnScreenMessageStartTime;
	if (DisplayTime < 2.0)
	{
		switch (OnScreenMessageStreamingPerformance)
		{
		case EWorldPartitionStreamingPerformance::Critical:
			OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Error, LOCTEXT("WPStreamingCritical", "[Critical] WorldPartition Streaming Performance"));
			break;
		case EWorldPartitionStreamingPerformance::Slow:
			OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Warning, LOCTEXT("WPStreamingWarning", "[Slow] WorldPartition Streaming Performance"));
			break;
		default:
			break;
		}
	}
	else
	{
		OnScreenMessageStreamingPerformance = EWorldPartitionStreamingPerformance::Good;
	}
}
#endif

bool UWorldPartitionStreamingPolicy::ShouldSkipCellForPerformance(const UWorldPartitionRuntimeCell* Cell) const
{
	// When performance is degrading start skipping non blocking cells
	if (!Cell->GetBlockOnSlowLoading())
	{
		UWorld* World = WorldPartition->GetWorld();
		const ENetMode NetMode = World->GetNetMode();
		if (NetMode == NM_Standalone || NetMode == NM_Client)
		{
			return IsInBlockTillLevelStreamingCompleted(/*bIsCausedByBadStreamingPerformance*/true);
		}
	}
	return false;
}

int32 UWorldPartitionStreamingPolicy::GetMaxCellsToLoad() const
{
	// This policy limits the number of concurrent loading streaming cells, except if match hasn't started
	UWorld* World = WorldPartition->GetWorld();
	const ENetMode NetMode = World->GetNetMode();
	if (NetMode == NM_Standalone || NetMode == NM_Client)
	{
		return !IsInBlockTillLevelStreamingCompleted() ? (GMaxLoadingStreamingCells - GetCellLoadingCount()) : MAX_int32;
	}

	// Always allow max on server to make sure StreamingLevels are added before clients update the visibility
	return MAX_int32;
}

void UWorldPartitionStreamingPolicy::SetTargetStateForCells(EWorldPartitionRuntimeCellState TargetState, const TSet<const UWorldPartitionRuntimeCell*>& Cells)
{
	switch (TargetState)
	{
	case EWorldPartitionRuntimeCellState::Unloaded:
		SetCellsStateToUnloaded(Cells);
		break;
	case EWorldPartitionRuntimeCellState::Loaded:
		SetCellsStateToLoaded(Cells);
		break;
	case EWorldPartitionRuntimeCellState::Activated:
		SetCellsStateToActivated(Cells);
		break;
	default:
		check(false);
	}
}

void UWorldPartitionStreamingPolicy::SetCellsStateToLoaded(const TSet<const UWorldPartitionRuntimeCell*>& ToLoadCells)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::SetCellsStateToLoaded);

	int32 MaxCellsToLoad = GetMaxCellsToLoad();

	// Sort cells based on importance
	TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedCells;
	WorldPartition->RuntimeHash->SortStreamingCellsByImportance(ToLoadCells, StreamingSources, SortedCells);

	// Trigger cell loading. Depending on actual state of cell limit loading.
	for (const UWorldPartitionRuntimeCell* Cell : SortedCells)
	{
		if (ShouldSkipCellForPerformance(Cell))
		{
			continue;
		}

		UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy::LoadCells %s"), *Cell->GetName());
		if (ActivatedCells.Contains(Cell))
		{
			Cell->Deactivate();
			ActivatedCells.Remove(Cell);
			LoadedCells.Add(Cell);
		}
		else if (MaxCellsToLoad > 0)
		{
			Cell->Load();
			LoadedCells.Add(Cell);
			if (!Cell->IsAlwaysLoaded())
			{
				--MaxCellsToLoad;
			}
		}
	}
}

void UWorldPartitionStreamingPolicy::SetCellsStateToActivated(const TSet<const UWorldPartitionRuntimeCell*>& ToActivateCells)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::SetCellsStateToActivated);

	int32 MaxCellsToLoad = GetMaxCellsToLoad();

	// Sort cells based on importance
	TArray<const UWorldPartitionRuntimeCell*, TInlineAllocator<256>> SortedCells;
	WorldPartition->RuntimeHash->SortStreamingCellsByImportance(ToActivateCells, StreamingSources, SortedCells);

	// Trigger cell activation. Depending on actual state of cell limit loading.
	for (const UWorldPartitionRuntimeCell* Cell : SortedCells)
	{
		if (ShouldSkipCellForPerformance(Cell))
		{
			continue;
		}

		UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy::ActivateCells %s"), *Cell->GetName());
		if (LoadedCells.Contains(Cell))
		{
			LoadedCells.Remove(Cell);
			ActivatedCells.Add(Cell);
			Cell->Activate();
		}
		else if (MaxCellsToLoad > 0)
		{
			if (!Cell->IsAlwaysLoaded())
			{
				--MaxCellsToLoad;
			}
			ActivatedCells.Add(Cell);
			Cell->Activate();
		}
	}
}

void UWorldPartitionStreamingPolicy::SetCellsStateToUnloaded(const TSet<const UWorldPartitionRuntimeCell*>& ToUnloadCells)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::SetCellsStateToUnloaded);

	for (const UWorldPartitionRuntimeCell* Cell : ToUnloadCells)
	{
		if (Cell->CanUnload())
		{
			UE_LOG(LogWorldPartition, Verbose, TEXT("UWorldPartitionStreamingPolicy::UnloadCells %s"), *Cell->GetName());
			Cell->Unload();
			ActivatedCells.Remove(Cell);
			LoadedCells.Remove(Cell);
		}
	}
}

bool UWorldPartitionStreamingPolicy::CanAddLoadedLevelToWorld(ULevel* InLevel) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::CanAddLoadedLevelToWorld);

	check(WorldPartition->IsInitialized());
	UWorld* World = WorldPartition->GetWorld();

	// Always allow AddToWorld in DedicatedServer
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return true;
	}

	// Always allow AddToWorld when not inside UWorld::BlockTillLevelStreamingCompleted that was not triggered by bad streaming performance
	if (!IsInBlockTillLevelStreamingCompleted(/*bIsCausedByBadStreamingPerformance*/true))
	{
		return true;
	}

	const UWorldPartitionRuntimeCell** Cell = Algo::FindByPredicate(SortedAddToWorldCells, [InLevel](const UWorldPartitionRuntimeCell* ItCell) { return ItCell->GetLevel() == InLevel; });
	if (Cell && ShouldSkipCellForPerformance(*Cell))
	{
		return false;
	}
 
	return true;
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
			EWorldPartitionRuntimeCellState CellState = Cell->GetCurrentState();
			if (CellState != QueryState)
			{
				bool bSkipCell = false;

				// Don't consider HLOD cells if HLODs are disabled.
				if (!bIsHLODEnabled)
				{
					bSkipCell = Cell->GetIsHLOD();
				}

				// If we are querying for Unloaded/Loaded but a Cell is part of a data layer outside of the query that is activated do not consider it
				if (!bSkipCell && QueryState < CellState)
				{
					for (const FName& CellDataLayer : Cell->GetDataLayers())
					{
						if (!QuerySource.DataLayers.Contains(CellDataLayer) && DataLayerSubsystem->GetDataLayerEffectiveRuntimeStateByName(CellDataLayer) > EDataLayerRuntimeState::Unloaded)
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

/*
 * FStreamingSourceVelocity Implementation
 */

FStreamingSourceVelocity::FStreamingSourceVelocity(const FName& InSourceName)
: SourceName(InSourceName)
, LastIndex(INDEX_NONE)
, LastUpdateTime(-1.0)
, VelocitiesHistorySum(0.f)
{
	VelocitiesHistory.SetNumZeroed(VELOCITY_HISTORY_SAMPLE_COUNT);
}

float FStreamingSourceVelocity::GetAverageVelocity(const FVector& NewPosition, const float CurrentTime)
{
	const float TeleportDistance = 100.f;
	const float MaxDeltaSeconds = 5.f;
	const bool bIsFirstCall = (LastIndex == INDEX_NONE);
	const float DeltaSeconds = bIsFirstCall ? 0.f : (CurrentTime - LastUpdateTime);
	const float Distance = bIsFirstCall ? 0.f : ((NewPosition - LastPosition) * 0.01f).Size();
	if (bIsFirstCall)
	{
		UE_LOG(LogWorldPartition, Log, TEXT("New Streaming Source: %s -> Position: %s"), *SourceName.ToString(), *NewPosition.ToString());
		LastIndex = 0;
	}

	ON_SCOPE_EXIT
	{
		LastUpdateTime = CurrentTime;
		LastPosition = NewPosition;
	};

	// Handle invalid cases
	if (bIsFirstCall || (DeltaSeconds <= 0.f) || (DeltaSeconds > MaxDeltaSeconds) || (Distance > TeleportDistance))
	{
		UE_CLOG(Distance > TeleportDistance, LogWorldPartition, Log, TEXT("Detected Streaming Source Teleport: %s -> Last Position: %s -> New Position: %s"), *SourceName.ToString(), *LastPosition.ToString(), *NewPosition.ToString());
		return 0.f;
	}

	// Compute velocity (m/s)
	const float Velocity = Distance / DeltaSeconds;
	// Update velocities history buffer and sum
	LastIndex = (LastIndex + 1) % VELOCITY_HISTORY_SAMPLE_COUNT;
	VelocitiesHistorySum = FMath::Max<float>(0.f, (VelocitiesHistorySum + Velocity - VelocitiesHistory[LastIndex]));
	VelocitiesHistory[LastIndex] = Velocity;

	// return average
	return (VelocitiesHistorySum / VELOCITY_HISTORY_SAMPLE_COUNT);
}

#undef LOCTEXT_NAMESPACE