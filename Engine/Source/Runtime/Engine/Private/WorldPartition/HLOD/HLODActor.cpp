// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "Modules/ModuleManager.h"
#include "IWorldPartitionHLODUtilities.h"
#include "WorldPartitionHLODUtilitiesModule.h"
#endif

AWorldPartitionHLOD::AWorldPartitionHLOD(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

#if WITH_EDITORONLY_DATA
	HLODHash = 0;
	HLODBounds = FBox(EForceInit::ForceInit);
#endif
}

UPrimitiveComponent* AWorldPartitionHLOD::GetHLODComponent()
{
	return Cast<UPrimitiveComponent>(RootComponent);
}

void AWorldPartitionHLOD::SetVisibility(bool bInVisible)
{
	if (GetRootComponent())
	{
		GetRootComponent()->SetVisibility(bInVisible, /*bPropagateToChildren*/ true);
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

void AWorldPartitionHLOD::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	Super::Serialize(Ar);

#if WITH_EDITOR
	if(Ar.IsLoading() && Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::WorldPartitionStreamingCellsNamingShortened)
	{
		SourceCell = SourceCell.ToString().Replace(TEXT("WPRT_"), TEXT(""), ESearchCase::CaseSensitive).Replace(TEXT("Cell_"), TEXT(""), ESearchCase::CaseSensitive);
	}
#endif
}

void AWorldPartitionHLOD::RerunConstructionScripts()
{}

#if WITH_EDITOR

bool AWorldPartitionHLOD::IsHiddenEd() const
{
	return true;
}

EActorGridPlacement AWorldPartitionHLOD::GetGridPlacement() const
{
	return SubActorsHLODLayer && SubActorsHLODLayer->IsAlwaysLoaded() ? EActorGridPlacement::AlwaysLoaded : EActorGridPlacement::Bounds;
}

EActorGridPlacement AWorldPartitionHLOD::GetDefaultGridPlacement() const
{
	// Overriden as AActor::GetDefaultGridPlacement() will mark all actors that are not placeable as AlwaysLoaded...
	return EActorGridPlacement::None;
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

void AWorldPartitionHLOD::SetSubActors(const TArray<FGuid>& InSubActors)
{
	SubActors = InSubActors;
}

const TArray<FGuid>& AWorldPartitionHLOD::GetSubActors() const
{
	return SubActors;
}

void AWorldPartitionHLOD::SetSourceCell(const TSoftObjectPtr<UWorldPartitionRuntimeCell>& InSourceCell)
{
	SourceCell = InSourceCell;
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
	HLODBounds.GetCenterAndExtents(Origin, BoxExtent);
}

FBox AWorldPartitionHLOD::GetStreamingBounds() const
{
	return HLODBounds;
}

uint32 AWorldPartitionHLOD::GetHLODHash() const
{
	return HLODHash;
}

void AWorldPartitionHLOD::BuildHLOD(bool bForceBuild)
{
	IWorldPartitionHLODUtilities* WPHLODUtilities = FModuleManager::Get().LoadModuleChecked<IWorldPartitionHLODUtilitiesModule>("WorldPartitionHLODUtilities").GetUtilities();
	if (WPHLODUtilities)
	{
		if (bForceBuild)
		{
			HLODHash = 0;
		}

		HLODHash = WPHLODUtilities->BuildHLOD(this);
	}
	else
	{

	}
}

#endif // WITH_EDITOR


UWorldPartitionRuntimeHLODCellData::UWorldPartitionRuntimeHLODCellData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

