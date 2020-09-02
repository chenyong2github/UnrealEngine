// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorCellPreviewActor.h"

#if WITH_EDITOR
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#endif

AWorldPartitionEditorCellPreview::AWorldPartitionEditorCellPreview(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	bVisible = false;

	bIsEditorOnlyActor = true;

	SetActorEnableCollision(false);

	RootComponent = CreateDefaultSubobject<USceneComponent>(USceneComponent::GetDefaultSceneRootVariableName());
	RootComponent->Mobility = EComponentMobility::Static;

	SetActorHiddenInGame(true);
#endif
}

#if WITH_EDITOR
EActorGridPlacement AWorldPartitionEditorCellPreview::GetDefaultGridPlacement() const
{
	return EActorGridPlacement::AlwaysLoaded;
}

void AWorldPartitionEditorCellPreview::SetVisibility(bool bInVisible)
{
	bVisible = bInVisible;

	if (HasValidRootComponent())
	{
		GetRootComponent()->SetVisibility(bVisible, true);
	}
}

bool AWorldPartitionEditorCellPreview::IsSelectable() const
{
	return false;
}

void AWorldPartitionEditorCellPreview::GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors) const
{
	Origin = CellBounds.GetCenter();
	BoxExtent = CellBounds.GetExtent();
}

void AWorldPartitionEditorCellPreview::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	SetVisibility(bVisible);
}

#endif // WITH_EDITOR

AWorldPartitionUnloadedCellPreviewPostProcessVolume::AWorldPartitionUnloadedCellPreviewPostProcessVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	if (!IsTemplate())
	{
		static ConstructorHelpers::FObjectFinder<UMaterial> UnloadedCellPreviewMat(TEXT("/Engine/EditorMaterials/WorldPartition/UnloadedCellPreview_PP"));
		if (ensure(UnloadedCellPreviewMat.Object))
		{
			SetFlags(RF_Transient);
			bIsEditorOnlyActor = true;
			bUnbound = true;
			Settings.WeightedBlendables.Array.Emplace(1.0f, UnloadedCellPreviewMat.Object);
		}
	}
#endif // WITH_EDITOR
}
