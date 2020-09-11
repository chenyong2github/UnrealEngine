// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "Components/PrimitiveComponent.h"

#if WITH_EDITOR
#include "ActorRegistry.h"
#include "AssetData.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#endif

UWorldPartitionRuntimeHLODCellData::UWorldPartitionRuntimeHLODCellData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

AWorldPartitionHLOD::AWorldPartitionHLOD(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
}

UPrimitiveComponent* AWorldPartitionHLOD::GetHLODComponent()
{
	return CastChecked<UPrimitiveComponent>(RootComponent);
}

void AWorldPartitionHLOD::LinkCell(FName InCellName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::LinkCell);

	for (const TSoftObjectPtr<UPrimitiveComponent>& SubPrimitiveComponent : SubPrimitivesComponents)
	{
		if (SubPrimitiveComponent.IsValid())
		{
			SubPrimitiveComponent->SetCachedLODParentPrimitive(GetHLODComponent());
		}
	}
}

void AWorldPartitionHLOD::UnlinkCell(FName InCellName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::UnlinkCell);

	for (const TSoftObjectPtr<UPrimitiveComponent>& SubPrimitiveComponent : SubPrimitivesComponents)
	{
		if (SubPrimitiveComponent.IsValid())
		{
			SubPrimitiveComponent->SetCachedLODParentPrimitive(nullptr);
		}
	}
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

const float HLODACTOR_DEFAULT_MIN_DRAW_DISTANCE = 5000.0f;

void AWorldPartitionHLOD::SetHLODPrimitive(UPrimitiveComponent* InHLODPrimitive)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::SetHLODPrimitive);

	USceneComponent* OldRootComponent = GetRootComponent();

	SetRootComponent(InHLODPrimitive);
	AddInstanceComponent(InHLODPrimitive);

	// Setup custom depth rendering to achieve a red tint using a post process material
	const int32 CellPreviewStencilValue = 180;
	InHLODPrimitive->bRenderCustomDepth = true;
	InHLODPrimitive->CustomDepthStencilValue = CellPreviewStencilValue;
	
	InHLODPrimitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InHLODPrimitive->SetMobility(EComponentMobility::Static);

	InHLODPrimitive->MinDrawDistance = HLODACTOR_DEFAULT_MIN_DRAW_DISTANCE;

	InHLODPrimitive->RegisterComponent();
	InHLODPrimitive->MarkRenderStateDirty();

	if (OldRootComponent)
	{
		OldRootComponent->DestroyComponent();
	}
}

void AWorldPartitionHLOD::SetChildrenPrimitives(const TArray<UPrimitiveComponent*>& InChildrenPrimitives)
{
	TSet<FGuid> SubActorsSet;

	for (UPrimitiveComponent* ChildPrimitive : InChildrenPrimitives)
	{
		SubPrimitivesComponents.Add(ChildPrimitive);
		SubActorsSet.Add(ChildPrimitive->GetOwner()->GetActorGuid());
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

void AWorldPartitionHLOD::OnWorldPartitionActorRegistered(AActor& InActor, bool bInLoaded)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::OnWorldPartitionActorRegistered);

	UpdateLODParent(InActor, !bInLoaded);
}

EActorGridPlacement AWorldPartitionHLOD::GetDefaultGridPlacement() const
{
	return EActorGridPlacement::Bounds;
}

void AWorldPartitionHLOD::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

	if (IsPackageExternal())
	{
		if (SubActors.Num())
		{
			FString SubActorsGUIDsStr;
			for (FGuid ActorGUID : SubActors)
			{
				SubActorsGUIDsStr += ActorGUID.ToString() + TEXT(";");
			}
			SubActorsGUIDsStr.RemoveFromEnd(TEXT(";"));

			static const FName NAME_HLODSubActors(TEXT("HLODSubActors"));
			FActorRegistry::SaveActorMetaData(NAME_HLODSubActors, SubActorsGUIDsStr, OutTags);
		}
	}
}

void AWorldPartitionHLOD::PostActorCreated()
{
	Super::PostActorCreated();
	HLODGuid = GetActorGuid();
}

void AWorldPartitionHLOD::RegisterAllComponents()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::RegisterAllComponents);

	Super::RegisterAllComponents();

	UWorld* World = GetWorld();
	if (!World->IsGameWorld())
	{
		UWorldPartition* WorldPartition = World->GetWorldPartition();
		check(WorldPartition);

		check(!ActorRegisteredDelegateHandle.IsValid());
		ActorRegisteredDelegateHandle = WorldPartition->OnActorRegisteredEvent.AddUObject(this, &AWorldPartitionHLOD::OnWorldPartitionActorRegistered);

		for (const FGuid& SubActorGuid : SubActors)
		{
			const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(SubActorGuid);
			if (ActorDesc != nullptr)
			{
				if (AActor* Actor = ActorDesc->GetActor())
				{
					SetLODParent(*Actor);
				}
			}
		}
	}
}

void AWorldPartitionHLOD::UnregisterAllComponents(const bool bForReregister)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AWorldPartitionHLOD::UnregisterAllComponents);

	UWorld* World = GetWorld();
	if (World && !World->IsPendingKillOrUnreachable())
	{
		if (!World->IsGameWorld() && ActorRegisteredDelegateHandle.IsValid())
		{
			UWorldPartition* WorldPartition = World->GetWorldPartition();
			check(WorldPartition);

			WorldPartition->OnActorRegisteredEvent.Remove(ActorRegisteredDelegateHandle);
			ActorRegisteredDelegateHandle.Reset();

			for (const FGuid& SubActorGuid : SubActors)
			{
				const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(SubActorGuid);
				if (ActorDesc != nullptr)
				{
					if (AActor* Actor = ActorDesc->GetActor())
					{
						ClearLODParent(*Actor);
					}
				}
			}
		}
	}

	Super::UnregisterAllComponents();
}

void UWorldPartitionRuntimeHLODCellData::SetReferencedHLODActors(TArray<FGuid>&& InReferencedHLODActors)
{
	ReferencedHLODActors = MoveTemp(InReferencedHLODActors);
}

#endif // WITH_EDITOR
