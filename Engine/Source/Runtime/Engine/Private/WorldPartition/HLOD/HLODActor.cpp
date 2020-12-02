// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "Components/PrimitiveComponent.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#endif

AWorldPartitionHLOD::AWorldPartitionHLOD(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

#if WITH_EDITORONLY_DATA
	bListedInSceneOutliner = false;
#endif
}

UPrimitiveComponent* AWorldPartitionHLOD::GetHLODComponent()
{
	return Cast<UPrimitiveComponent>(RootComponent);
}

void AWorldPartitionHLOD::OnCellShown(FName InCellName)
{
	GetRootComponent()->SetVisibility(false, true);
}

void AWorldPartitionHLOD::OnCellHidden(FName InCellName)
{
	GetRootComponent()->SetVisibility(true, true);
}

void AWorldPartitionHLOD::BeginPlay()
{
	Super::BeginPlay();
	GetWorld()->GetSubsystem<UHLODSubsystem>()->RegisterHLODActor(this);
}

void AWorldPartitionHLOD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetSubsystem<UHLODSubsystem>()->UnregisterHLODActor(this);
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR

void AWorldPartitionHLOD::OnSubActorLoaded(AActor& Actor)
{
	check(!Actor.GetRootComponent() || Actor.GetRootComponent()->IsRegistered());
	
	bool bIsAlreadyInSet = false;
	LoadedSubActors.Add(&Actor, &bIsAlreadyInSet);

	if (!bIsAlreadyInSet)
	{
		if (LoadedSubActors.Num() == 1)
		{
			UpdateVisibility();
		}
	}
}

void AWorldPartitionHLOD::OnSubActorUnloaded(AActor& Actor)
{
	LoadedSubActors.Remove(&Actor);

	// If HLOD has no more sub actors, ensure it is drawn at all time
	if (LoadedSubActors.IsEmpty())
	{
		UpdateVisibility();
	}
}

void AWorldPartitionHLOD::SetupLoadedSubActors()
{
	UWorld* World = GetWorld();
	if (World && World->IsEditorWorld() && !World->IsPlayInEditor())
	{
		LoadedSubActors.Empty();

		UWorldPartition* WorldPartition = World->GetWorldPartition();
		check(WorldPartition);

		for (const FGuid& SubActorGuid : SubActors)
		{
			const FWorldPartitionActorDesc* SubActorDesc = WorldPartition->GetActorDesc(SubActorGuid);
			AActor* SubActor = SubActorDesc ? SubActorDesc->GetActor() : nullptr;
			if (SubActor && SubActor->GetRootComponent() && SubActor->GetRootComponent()->IsRegistered())
			{
				OnSubActorLoaded(*SubActor);
			}
		}

		UpdateVisibility();
	}
}

void AWorldPartitionHLOD::ResetLoadedSubActors()
{
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		for (const TWeakObjectPtr<AActor>& SubActor : LoadedSubActors)
		{
			if (!SubActor.IsValid())
			{
				continue;
			}
		}

		LoadedSubActors.Empty();

		UpdateVisibility();
	}
}

void AWorldPartitionHLOD::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (!IsTemplate())
	{
		SetupLoadedSubActors();
	}
}

void AWorldPartitionHLOD::PostUnregisterAllComponents()
{
	if (!IsTemplate())
	{
		ResetLoadedSubActors();
	}

	Super::PostUnregisterAllComponents();
}

void AWorldPartitionHLOD::UpdateVisibility()
{
	SetIsTemporarilyHiddenInEditor(HasLoadedSubActors());
}

bool AWorldPartitionHLOD::HasLoadedSubActors() const
{
	return !LoadedSubActors.IsEmpty();
}

EActorGridPlacement AWorldPartitionHLOD::GetDefaultGridPlacement() const
{
	return EActorGridPlacement::Location;
}

TUniquePtr<FWorldPartitionActorDesc> AWorldPartitionHLOD::CreateClassActorDesc() const
{
	return TUniquePtr<FWorldPartitionActorDesc>(new FHLODActorDesc());
}

void AWorldPartitionHLOD::SetHLODPrimitives(const TArray<UPrimitiveComponent*>& InHLODPrimitives)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::SetHLODPrimitive);
	check(!InHLODPrimitives.IsEmpty());

	TArray<USceneComponent*> ComponentsToRemove;
	GetComponents<USceneComponent>(ComponentsToRemove);

	SetRootComponent(InHLODPrimitives[0]);

	for(UPrimitiveComponent* InHLODPrimitive : InHLODPrimitives)
	{
		ComponentsToRemove.Remove(InHLODPrimitive);

		AddInstanceComponent(InHLODPrimitive);

		if (InHLODPrimitive != RootComponent)
		{
			InHLODPrimitive->SetupAttachment(RootComponent);
		}
	
		InHLODPrimitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		InHLODPrimitive->SetMobility(EComponentMobility::Static);

		InHLODPrimitive->RegisterComponent();
		InHLODPrimitive->MarkRenderStateDirty();
	}

	for (USceneComponent* ComponentToRemove : ComponentsToRemove)
	{
		ComponentToRemove->DestroyComponent();
	}
}

void AWorldPartitionHLOD::SetChildrenPrimitives(const TArray<UPrimitiveComponent*>& InChildrenPrimitives)
{
	check(GetHLODComponent());

	ResetLoadedSubActors();
	SubActors.Empty();

	UPrimitiveComponent* HLODComponent = GetHLODComponent();
	check(HLODComponent);

	for (UPrimitiveComponent* ChildPrimitive : InChildrenPrimitives)
	{
		AActor* SubActor = ChildPrimitive->GetOwner();
		
		if (!LoadedSubActors.Contains(SubActor))
		{
			OnSubActorLoaded(*SubActor);
			SubActors.Add(SubActor->GetActorGuid());
		}
	}
}

const TArray<FGuid>& AWorldPartitionHLOD::GetSubActors() const
{
	return SubActors;
}

void AWorldPartitionHLOD::PostActorCreated()
{
	Super::PostActorCreated();
	HLODGuid = GetActorGuid();
}

#endif // WITH_EDITOR


UWorldPartitionRuntimeHLODCellData::UWorldPartitionRuntimeHLODCellData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

void UWorldPartitionRuntimeHLODCellData::SetReferencedHLODActors(TArray<FGuid>&& InReferencedHLODActors)
{
	ReferencedHLODActors = MoveTemp(InReferencedHLODActors);
}

#endif // WITH_EDITOR
