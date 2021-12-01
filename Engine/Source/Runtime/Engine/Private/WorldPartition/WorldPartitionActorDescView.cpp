// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDescView.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

FWorldPartitionActorDescView::FWorldPartitionActorDescView()
	: ActorDesc(nullptr)
	, GridPlacement(EActorGridPlacement::None)
	, bInvalidDataLayers(false)
{}

FWorldPartitionActorDescView::FWorldPartitionActorDescView(const FWorldPartitionActorDesc* InActorDesc)
	: ActorDesc(InActorDesc)
	, GridPlacement(InActorDesc->GetGridPlacement())
	, bInvalidDataLayers(false)
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
	return GridPlacement;
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
	static TArray<FName> EmptyDataLayers;
	return bInvalidDataLayers ? EmptyDataLayers : ActorDesc->GetDataLayers();
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

FString FWorldPartitionActorDescView::ToString() const
{
	return ActorDesc->ToString();
}

uint32 FWorldPartitionActorDescView::GetTag() const
{
	return ActorDesc->Tag;
}

bool FWorldPartitionActorDescView::GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	return ActorDesc->GetContainerInstance(OutLevelContainer, OutLevelTransform, OutClusterMode);
}

FName FWorldPartitionActorDescView::GetActorLabelOrName() const
{
	return ActorDesc->GetActorLabelOrName();
}

void FWorldPartitionActorDescView::SetGridPlacement(EActorGridPlacement InGridPlacement)
{
	if (GridPlacement != InGridPlacement)
	{
		GridPlacement = InGridPlacement;
		UE_LOG(LogWorldPartition, Verbose, TEXT("Actor '%s' grid placement changed to %s"), *GetActorLabel().ToString(), *StaticEnum<EActorGridPlacement>()->GetNameStringByValue((int64)InGridPlacement));
	}
}

void FWorldPartitionActorDescView::SetInvalidDataLayers()
{
	if (!bInvalidDataLayers)
	{
		bInvalidDataLayers = true;
		UE_LOG(LogWorldPartition, Verbose, TEXT("Actor '%s' data layers invalidated"), *GetActorLabel().ToString());
	}
}
#endif