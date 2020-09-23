// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/WorldPartition.h"
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

	UWorldPartitionSubsystem* WorldPartitionSubsystem = Collection.InitializeDependency<UWorldPartitionSubsystem>();

#if WITH_EDITOR
	HLODActorDescFactory.Reset(new FHLODActorDescFactory());

	RegisterActorDescFactories(WorldPartitionSubsystem);
#endif
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
	PendingCellsShown.MultiFind(HLODActorGUID, Cells);
	if (Cells.Num())
	{
		for (FName Cell : Cells)
		{ 
			InWorldPartitionHLOD->OnCellShown(Cell);
		}
		PendingCellsShown.Remove(HLODActorGUID);
	}
}

void UHLODSubsystem::UnregisterHLODActor(AWorldPartitionHLOD* InWorldPartitionHLOD)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::UnregisterHLODActor);

	const FGuid& HLODActorGUID = InWorldPartitionHLOD->GetHLODGuid();
	int32 NumRemoved = RegisteredHLODActors.Remove(HLODActorGUID);
	check(NumRemoved == 1);
}

void UHLODSubsystem::OnCellShown(const UWorldPartitionRuntimeCell* InCell)
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
			HLODActor->OnCellShown(InCell->GetFName());
		}
		else
		{
			// Cell was shown before the HLOD
			PendingCellsShown.Add(HLODActorGUID, InCell->GetFName());
		}
	}
}

void UHLODSubsystem::OnCellHidden(const UWorldPartitionRuntimeCell* InCell)
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
			HLODActor->OnCellHidden(InCell->GetFName());
			check(!PendingCellsShown.Find(HLODActorGUID));
		}
		else
		{
			PendingCellsShown.Remove(HLODActorGUID, InCell->GetFName());
		}
	}
}

#undef LOCTEXT_NAMESPACE
