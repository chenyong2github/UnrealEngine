// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDescView.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDesc.h"

FWorldPartitionActorDescView::FWorldPartitionActorDescView()
	: ActorDesc(nullptr)
	, EffectiveGridPlacement(EActorGridPlacement::None)
{}

FWorldPartitionActorDescView::FWorldPartitionActorDescView(const FWorldPartitionActorDesc* InActorDesc)
	: ActorDesc(InActorDesc)
	, EffectiveGridPlacement(InActorDesc->GetGridPlacement())
{}

const FGuid& FWorldPartitionActorDescView::GetGuid() const
{
	return ActorDesc->GetGuid();
}

FName FWorldPartitionActorDescView::GetClass() const
{
	return ActorDesc->GetClass();
}

UClass* FWorldPartitionActorDescView::GetActorClass() const
{
	return ActorDesc->GetActorClass();
}

FVector FWorldPartitionActorDescView::GetOrigin() const
{
	return ActorDesc->GetOrigin();
}

EActorGridPlacement FWorldPartitionActorDescView::GetGridPlacement() const
{
	return EffectiveGridPlacement;
}

FName FWorldPartitionActorDescView::GetRuntimeGrid() const
{
	return ActorDesc->GetRuntimeGrid();
}

bool FWorldPartitionActorDescView::GetActorIsEditorOnly() const
{
	return ActorDesc->GetActorIsEditorOnly();
}

bool FWorldPartitionActorDescView::GetLevelBoundsRelevant() const
{
	return ActorDesc->GetLevelBoundsRelevant();
}

bool FWorldPartitionActorDescView::GetActorIsHLODRelevant() const
{
	return ActorDesc->GetActorIsHLODRelevant();
}

UHLODLayer* FWorldPartitionActorDescView::GetHLODLayer() const
{
	return ActorDesc->GetHLODLayer();
}

const TArray<FName>& FWorldPartitionActorDescView::GetDataLayers() const
{
	return ActorDesc->GetDataLayers();
}

FName FWorldPartitionActorDescView::GetActorPackage() const
{
	return ActorDesc->GetActorPackage();
}

FName FWorldPartitionActorDescView::GetActorPath() const
{
	return ActorDesc->GetActorPath();
}

FName FWorldPartitionActorDescView::GetActorLabel() const
{
	return ActorDesc->GetActorLabel();
}

FBox FWorldPartitionActorDescView::GetBounds() const
{
	return ActorDesc->GetBounds();
}

const TArray<FGuid>& FWorldPartitionActorDescView::GetReferences() const
{
	return ActorDesc->GetReferences();
}

bool FWorldPartitionActorDescView::GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	return ActorDesc->GetContainerInstance(OutLevelContainer, OutLevelTransform, OutClusterMode);
}

FGuid FWorldPartitionActorDescView::GetHLODParent() const
{
	return HLODParent;
}

void FWorldPartitionActorDescView::SetHLODParent(const FGuid& InHLODParent)
{
	check(!HLODParent.IsValid());
	check(InHLODParent.IsValid());
	HLODParent = InHLODParent;
}
#endif