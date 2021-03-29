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

DEFINE_LOG_CATEGORY_STATIC(LogHLODSubsystem, Log, All);

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
	check(InWorldPartition == GetWorld()->GetWorldPartition());
	check(CellsHLODMapping.IsEmpty());

	TSet<const UWorldPartitionRuntimeCell*> StreamingCells;
	InWorldPartition->RuntimeHash->GetAllStreamingCells(StreamingCells, /*bIncludeDataLayers*/ true);

	// Build cell to HLOD mapping
	for (const UWorldPartitionRuntimeCell* Cell : StreamingCells)
	{
		CellsHLODMapping.Emplace(Cell->GetFName());
	}
}

void UHLODSubsystem::OnWorldPartitionUnregistered(UWorldPartition* InWorldPartition)
{
	check(InWorldPartition == GetWorld()->GetWorldPartition());
	CellsHLODMapping.Reset();
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
	else
	{
		UE_LOG(LogHLODSubsystem, Warning, TEXT("Found HLOD referencing nonexistent cell '%s'"), *CellName.ToString());
		InWorldPartitionHLOD->SetVisibility(false);
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
	FCellHLODMapping& CellHLODs = CellsHLODMapping.FindChecked(InCell->GetFName());
	CellHLODs.bIsCellVisible = true;
	for (AWorldPartitionHLOD* HLODActor : CellHLODs.LoadedHLODs)
	{
		HLODActor->SetVisibility(false);
	}
}

void UHLODSubsystem::OnCellHidden(const UWorldPartitionRuntimeCell* InCell)
{
	FCellHLODMapping& CellHLODs = CellsHLODMapping.FindChecked(InCell->GetFName());
	CellHLODs.bIsCellVisible = false;
	for (AWorldPartitionHLOD* HLODActor : CellHLODs.LoadedHLODs)
	{
		HLODActor->SetVisibility(true);
	}
}

#undef LOCTEXT_NAMESPACE
