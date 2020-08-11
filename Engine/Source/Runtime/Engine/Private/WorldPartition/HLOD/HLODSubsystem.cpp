// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
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

void UHLODSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	HLODActorDescFactory.Reset(new FHLODActorDescFactory());

	Collection.InitializeDependency(UWorldPartitionSubsystem::StaticClass());

	if (UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>())
	{
		RegisterActorDescFactories(WorldPartitionSubsystem);
	}
#endif
}

void UHLODSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

#if WITH_EDITOR
void UHLODSubsystem::RegisterActorDescFactories(UWorldPartitionSubsystem* WorldPartitionSubsystem)
{
	WorldPartitionSubsystem->RegisterActorDescFactory(AWorldPartitionHLOD::StaticClass(), HLODActorDescFactory.Get());
}
#endif

void UHLODSubsystem::RegisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::RegisterHLODActor);

	const FGuid& HLODActorGUID = InWorldPartitionHLOD->GetHLODGuid();
	RegisteredHLODActors.Emplace(HLODActorGUID, InWorldPartitionHLOD);

	TArray<FName> Cells;
	PendingTransitionsToHLOD.MultiFind(HLODActorGUID, Cells);
	if (Cells.Num())
	{
		for (FName Cell : Cells)
		{ 
			InWorldPartitionHLOD->LinkCell(Cell);
		}
		PendingTransitionsToHLOD.Remove(HLODActorGUID);
	}
}

void UHLODSubsystem::UnregisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::UnregisterHLODActor);

	const FGuid& HLODActorGUID = InWorldPartitionHLOD->GetHLODGuid();
	int32 NumRemoved = RegisteredHLODActors.Remove(HLODActorGUID);
	check(NumRemoved == 1);

	check(!PendingTransitionsToHLOD.Find(HLODActorGUID));
}

void UHLODSubsystem::TransitionToHLOD(const UWorldPartitionRuntimeCell* InCell)
{
	const UWorldPartitionRuntimeHLODCellData* HLODCellData = InCell->GetCellData<UWorldPartitionRuntimeHLODCellData>();
	if (!ensure(HLODCellData))
	{
		return;
	}

	for (const FGuid& HLODActorGUID : HLODCellData->ReferencedHLODActors)
	{
		AWorldPartitionHLOD* HLODActor = RegisteredHLODActors.FindRef(HLODActorGUID);
		if (HLODActor)
		{
			HLODActor->LinkCell(InCell->GetFName());
		}
		else
		{
			// Cell was loaded before the HLOD
			PendingTransitionsToHLOD.Add(HLODActorGUID, InCell->GetFName());
		}
	}
}

void UHLODSubsystem::TransitionFromHLOD(const UWorldPartitionRuntimeCell* InCell)
{
	const UWorldPartitionRuntimeHLODCellData* HLODCellData = InCell->GetCellData<UWorldPartitionRuntimeHLODCellData>();
	if (!ensure(HLODCellData))
	{
		return;
	}

	for (const FGuid& HLODActorGUID : HLODCellData->ReferencedHLODActors)
	{
		AWorldPartitionHLOD* HLODActor = RegisteredHLODActors.FindRef(HLODActorGUID);
		if (HLODActor)
		{
			HLODActor->UnlinkCell(InCell->GetFName());
		}
		else 
		{
			PendingTransitionsToHLOD.Remove(HLODActorGUID, InCell->GetFName());
		}
	}
}

#undef LOCTEXT_NAMESPACE
