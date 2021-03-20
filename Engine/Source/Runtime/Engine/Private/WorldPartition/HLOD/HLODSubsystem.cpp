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
