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

	GetWorld()->GetWorldPartition()->OnActorRegisteredEvent.AddUObject(this, &UHLODSubsystem::OnWorldPartitionActorRegistered);
#endif
}

#if WITH_EDITOR

void UHLODSubsystem::OnWorldPartitionActorRegistered(AActor& InActor, bool bInLoaded)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODSubsystem::OnWorldPartitionActorRegistered);

	const FGuid& ActorGuid = InActor.GetActorGuid();

	if (AWorldPartitionHLOD* HLODActor = Cast<AWorldPartitionHLOD>(&InActor))
	{
		if (bInLoaded)
		{
			UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition();

			TArray<FGuid> SubActors;
			PendingHLODAssignment.MultiFind(ActorGuid, SubActors);

			if (!SubActors.IsEmpty())
			{
				for (const FGuid& SubActorGuid : SubActors)
				{
					const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(SubActorGuid);
					check(ActorDesc);

					AActor* Actor = Cast<AWorldPartitionHLOD>(ActorDesc->GetActor());
					if (Actor)
					{
						HLODActor->UpdateLODParent(*Actor, !bInLoaded);
					}
				}

				PendingHLODAssignment.Remove(ActorGuid);
			}
		}

		return;
	}

	const FWorldPartitionActorDesc* HLODActorDesc = GetHLODActorForActor(&InActor);
	if (HLODActorDesc)
	{
		AWorldPartitionHLOD* HLODActor = Cast<AWorldPartitionHLOD>(HLODActorDesc->GetActor());
		if (HLODActor)
		{
			HLODActor->UpdateLODParent(InActor, !bInLoaded);
		}
		else if (bInLoaded)
		{
			PendingHLODAssignment.Add(HLODActorDesc->GetGuid(), ActorGuid);
		}
		else
		{
			PendingHLODAssignment.Remove(HLODActorDesc->GetGuid(), ActorGuid);
		}
	}
}

const FWorldPartitionActorDesc* UHLODSubsystem::GetHLODActorForActor(const AActor* InActor) const
{
	UHLODLayer* HLODLayer = UHLODLayer::GetHLODLayer(InActor);
	if (!HLODLayer)
	{
		return nullptr;
	}

	UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition();

	const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(InActor->GetActorGuid());
	check(ActorDesc);

	if (ActorDesc->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)
	{
		return nullptr;
	}

	FVector ActorLocation = ActorDesc->GetOrigin();
	FBox ActorBox(ActorLocation, ActorLocation);

	// Find all HLODActors at that location
	TArray<const FWorldPartitionActorDesc*> HLODActorsDescs = WorldPartitionSubsystem->GetIntersectingActorDescs(ActorBox, AWorldPartitionHLOD::StaticClass());

	// Only keep the HLODActors matching our HLODLayer
	HLODActorsDescs.RemoveAll([HLODLayer, ActorDesc](const FWorldPartitionActorDesc* InActorDesc)
	{
		const FHLODActorDesc* HLODActorDesc = (FHLODActorDesc*)InActorDesc;
		return ActorDesc == HLODActorDesc || HLODActorDesc->GetHLODLayer() != HLODLayer;
	});
	
	if (HLODActorsDescs.IsEmpty())
	{
		return nullptr;
	}

	// Sort by bounds size
	HLODActorsDescs.Sort([](const FWorldPartitionActorDesc& A, const FWorldPartitionActorDesc& B)
	{
		return A.GetBounds().GetExtent().Size() < B.GetBounds().GetExtent().Size();
	});

	const UWorldPartition::FActorCluster* ActorCluster = WorldPartition->GetClusterForActor(ActorDesc->GetGuid());
	switch (ActorCluster->GridPlacement)
	{
	case EActorGridPlacement::Location:
		return HLODActorsDescs[0];
	
	case EActorGridPlacement::Bounds:
		for (const FWorldPartitionActorDesc* HLODActorDesc : HLODActorsDescs)
		{
			if (HLODActorDesc->GetBounds().IsInsideXY(ActorCluster->Bounds))
			{
				return HLODActorDesc;
			}
		}
		break;
	
	default:
		check(0);
	}

	return nullptr;
}

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
