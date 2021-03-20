// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "HLODSubsystem"

UHLODSubsystem::UHLODSubsystem()
	: UWorldSubsystem()
{
}

UHLODSubsystem::~UHLODSubsystem()
{
}

bool UHLODSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	if (UWorld* WorldOuter = Cast<UWorld>(Outer))
	{
		return WorldOuter->GetWorldPartition() != nullptr;
	}
	return false;
}

void UHLODSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	if (!IsRunningCommandlet() && !GetWorld()->IsGameWorld())
	{
		GetWorld()->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddUObject(this, &UHLODSubsystem::OnActorLoaded);
		GetWorld()->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.AddUObject(this, &UHLODSubsystem::OnActorUnloaded);
	}
#endif

	if (GetWorld()->IsGameWorld())
	{
		TSet<const UWorldPartitionRuntimeCell*> StreamingCells;
		GetWorld()->GetWorldPartition()->RuntimeHash->GetAllStreamingCells(StreamingCells, /*bIncludeDataLayers*/ true);

		// Build cell to HLOD mapping
		for (const UWorldPartitionRuntimeCell* Cell : StreamingCells)
		{
			CellsHLODMapping.Emplace(Cell->GetFName());
		}
	}
}

#if WITH_EDITOR
void UHLODSubsystem::OnActorLoaded(AActor& Actor)
{
	// If loading an HLOD actor, keep a map of Actor -> HLODActors
	if (AWorldPartitionHLOD* HLODActor = Cast<AWorldPartitionHLOD>(&Actor))
	{
		for (const FGuid& SubActorGuid : HLODActor->GetSubActors())
		{
			// Keep track of unloaded subactors
			check(!ActorsToHLOD.Contains(SubActorGuid));
			ActorsToHLOD.Emplace(SubActorGuid, HLODActor);
		}
	}

	// If HLOD for this actor is already loaded, notify the HLOD that it should be hidden
	AWorldPartitionHLOD** HLODActorPtr = ActorsToHLOD.Find(Actor.GetActorGuid());
	if (HLODActorPtr)
	{
		(*HLODActorPtr)->OnSubActorLoaded(Actor);
	}
}

void UHLODSubsystem::OnActorUnloaded(AActor& Actor)
{
	UWorld* World = GetWorld();
	UWorldPartition* WorldPartition = World->GetWorldPartition();

	// If bound to an HLOD, clear reference
	{
		AWorldPartitionHLOD** HLODActorPtr = ActorsToHLOD.Find(Actor.GetActorGuid());
		if (HLODActorPtr)
		{
			(*HLODActorPtr)->OnSubActorUnloaded(Actor);
		}
	}

	// If unloading an HLOD actor, clear map of Actor -> HLODActors
	if (AWorldPartitionHLOD* HLODActor = Cast<AWorldPartitionHLOD>(&Actor))
	{
		for (const FGuid& SubActorGuid : HLODActor->GetSubActors())
		{
			int32 NumRemoved = ActorsToHLOD.Remove(SubActorGuid);
			check(NumRemoved == 1);
		}
	}
}
#endif

void UHLODSubsystem::RegisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::RegisterHLODActor);

	FName CellName = InWorldPartitionHLOD->GetCellName();
	FCellHLODMapping* CellHLODs = CellsHLODMapping.Find(CellName);
	if (CellHLODs)
	{
		CellHLODs->LoadedHLODs.Add(InWorldPartitionHLOD);
		InWorldPartitionHLOD->SetVisibility(!CellHLODs->bIsCellVisible);
	}
}

void UHLODSubsystem::UnregisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::UnregisterHLODActor);

	FName CellName = InWorldPartitionHLOD->GetCellName();
	FCellHLODMapping* CellHLODs = CellsHLODMapping.Find(CellName);
	if (CellHLODs)
	{
		int32 NumRemoved = CellHLODs->LoadedHLODs.Remove(InWorldPartitionHLOD);
		check(NumRemoved == 1);
	}
}

void UHLODSubsystem::OnCellShown(const UWorldPartitionRuntimeCell* InCell)
{
	FCellHLODMapping* CellHLODs = CellsHLODMapping.Find(InCell->GetFName());
	if (CellHLODs)
	{
		CellHLODs->bIsCellVisible = true;

		for (AWorldPartitionHLOD* HLODActor : CellHLODs->LoadedHLODs)
		{
			HLODActor->SetVisibility(false);
		}
	}
}

void UHLODSubsystem::OnCellHidden(const UWorldPartitionRuntimeCell* InCell)
{
	FCellHLODMapping* CellHLODs = CellsHLODMapping.Find(InCell->GetFName());
	if (CellHLODs)
	{
		CellHLODs->bIsCellVisible = false;

		for (AWorldPartitionHLOD* HLODActor : CellHLODs->LoadedHLODs)
		{
			HLODActor->SetVisibility(true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
