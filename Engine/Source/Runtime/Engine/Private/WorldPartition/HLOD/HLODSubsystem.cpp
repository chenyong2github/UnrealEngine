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
	// Ensure the WorldPartitionSubsystem gets created before the HLODSubsystem
	Collection.InitializeDependency<UWorldPartitionSubsystem>();

	Super::Initialize(Collection);

	if (GetWorld()->IsGameWorld())
	{
		GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()->OnWorldPartitionRegistered.AddUObject(this, &UHLODSubsystem::OnWorldPartitionRegistered);
		GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()->OnWorldPartitionUnregistered.AddUObject(this, &UHLODSubsystem::OnWorldPartitionUnregistered);
	}
}

void UHLODSubsystem::OnWorldPartitionRegistered(UWorldPartition* InWorldPartition)
{
	TSet<const UWorldPartitionRuntimeCell*> StreamingCells;
	InWorldPartition->RuntimeHash->GetAllStreamingCells(StreamingCells, /*bIncludeDataLayers*/ true);

	check(!CellsHLODMapping.Contains(InWorldPartition));
	TMap<FName, FCellHLODMapping>& CellsMapping = CellsHLODMapping.Emplace(InWorldPartition);

	// Build cell to HLOD mapping
	for (const UWorldPartitionRuntimeCell* Cell : StreamingCells)
	{
		CellsMapping.Emplace(Cell->GetFName());
	}
}

void UHLODSubsystem::OnWorldPartitionUnregistered(UWorldPartition* InWorldPartition)
{
	check(CellsHLODMapping.Contains(InWorldPartition));
	CellsHLODMapping.Remove(InWorldPartition);
}


void UHLODSubsystem::RegisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::RegisterHLODActor);

	UWorldPartition* OwnerPartition = InWorldPartitionHLOD->GetWorld()->GetWorldPartition();
	check(OwnerPartition);

	FName CellName = InWorldPartitionHLOD->GetCellName();
	FCellHLODMapping* CellHLODs = CellsHLODMapping.FindChecked(OwnerPartition).Find(CellName);
	if (CellHLODs)
	{
		CellHLODs->LoadedHLODs.Add(InWorldPartitionHLOD);
		InWorldPartitionHLOD->SetVisibility(!CellHLODs->bIsCellVisible);
	}
}

void UHLODSubsystem::UnregisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::UnregisterHLODActor);

	UWorldPartition* OwnerPartition = InWorldPartitionHLOD->GetWorld()->GetWorldPartition();
	check(OwnerPartition);

	FName CellName = InWorldPartitionHLOD->GetCellName();
	FCellHLODMapping* CellHLODs = CellsHLODMapping.FindChecked(OwnerPartition).Find(CellName);
	if (CellHLODs)
	{
		int32 NumRemoved = CellHLODs->LoadedHLODs.Remove(InWorldPartitionHLOD);
		check(NumRemoved == 1);
	}
}

void UHLODSubsystem::OnCellShown(const UWorldPartitionRuntimeCell* InCell)
{
	UWorldPartition* OwnerPartition = InCell->GetTypedOuter<UWorldPartition>();
	check(OwnerPartition);

	FCellHLODMapping* CellHLODs = CellsHLODMapping.FindChecked(OwnerPartition).Find(InCell->GetFName());
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
	UWorldPartition* OwnerPartition = InCell->GetTypedOuter<UWorldPartition>();
	check(OwnerPartition);

	FCellHLODMapping* CellHLODs = CellsHLODMapping.FindChecked(OwnerPartition).Find(InCell->GetFName());
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
