// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "Components/PrimitiveComponent.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODLayer.h"
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

EActorGridPlacement AWorldPartitionHLOD::GetDefaultGridPlacement() const
{
	return EActorGridPlacement::Location;
}

void AWorldPartitionHLOD::SetLODParent(AActor& InActor)
{
	UpdateLODParent(InActor, false);
}

void AWorldPartitionHLOD::ClearLODParent(AActor& InActor)
{
	UpdateLODParent(InActor, true);
}

void AWorldPartitionHLOD::UpdateLODParent(AActor& InActor, bool bInClear)
{
	for (UActorComponent* Component : InActor.GetComponents())
	{
		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
		{
			if (UHLODLayer::ShouldIncludeInHLOD(PrimitiveComponent, SubActorsHLODLevel))
			{
				PrimitiveComponent->SetCachedLODParentPrimitive(!bInClear ? GetHLODComponent() : nullptr);
			}
		}
	}
}

void AWorldPartitionHLOD::SetHLODPrimitives(const TArray<UPrimitiveComponent*>& InHLODPrimitives, float InFadeOutDistance)
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

		InHLODPrimitive->MinDrawDistance = InFadeOutDistance;

		InHLODPrimitive->RegisterComponent();
		InHLODPrimitive->MarkRenderStateDirty();
	}

	for (USceneComponent* ComponentToRemove : ComponentsToRemove)
	{
		ComponentToRemove->DestroyComponent();
	}
}

const FBox& AWorldPartitionHLOD::GetHLODBounds() const
{
	return HLODBounds;
}

void AWorldPartitionHLOD::SetHLODBounds(const FBox& InBounds)
{
	HLODBounds = InBounds;
}

void AWorldPartitionHLOD::GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	Super::GetActorBounds(bOnlyCollidingComponents, Origin, BoxExtent, bIncludeFromChildActors);

	FBox Bounds = FBox(Origin - BoxExtent, Origin + BoxExtent);
	Bounds += HLODBounds;
	Bounds.GetCenterAndExtents(Origin, BoxExtent);
}

void AWorldPartitionHLOD::GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	GetActorBounds(bOnlyCollidingComponents, Origin, BoxExtent, bIncludeFromChildActors);
}

void AWorldPartitionHLOD::SetChildrenPrimitives(const TArray<UPrimitiveComponent*>& InChildrenPrimitives)
{
	TSet<FGuid> SubActorsSet;

	UPrimitiveComponent* HLODComponent = GetHLODComponent();
	check(HLODComponent);

	for (UPrimitiveComponent* ChildPrimitive : InChildrenPrimitives)
	{
		SubActorsSet.Add(ChildPrimitive->GetOwner()->GetActorGuid());

		ChildPrimitive->SetCachedLODParentPrimitive(HLODComponent);
	}

	SubActors = SubActorsSet.Array();
}

const TArray<FGuid>& AWorldPartitionHLOD::GetSubActors() const
{
	return SubActors;
}

void AWorldPartitionHLOD::SetHLODLayer(const UHLODLayer* InSubActorsHLODLayer, int32 InSubActorsHLODLevel)
{
	SubActorsHLODLayer = InSubActorsHLODLayer;
	SubActorsHLODLevel = InSubActorsHLODLevel;
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
