// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorDescView.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

FWorldPartitionActorDescView::FWorldPartitionActorDescView()
	: FWorldPartitionActorDescView(nullptr)
{}

FWorldPartitionActorDescView::FWorldPartitionActorDescView(const FWorldPartitionActorDesc* InActorDesc)
	: ActorDesc(InActorDesc)
	, bIsForcedNonSpatiallyLoaded(false)
	, bInvalidDataLayers(false)
	, bInvalidRuntimeGrid(false)
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

FName FWorldPartitionActorDescView::GetRuntimeGrid() const
{
	if (bInvalidRuntimeGrid)
	{
		return FName();
	}

	return ActorDesc->GetRuntimeGrid();
}

bool FWorldPartitionActorDescView::GetActorIsEditorOnly() const
{
	return ActorDesc->GetActorIsEditorOnly();
}

bool FWorldPartitionActorDescView::GetIsSpatiallyLoaded() const
{
	return bIsForcedNonSpatiallyLoaded ? false : ActorDesc->GetIsSpatiallyLoaded();
}

bool FWorldPartitionActorDescView::GetLevelBoundsRelevant() const
{
	return ActorDesc->GetLevelBoundsRelevant();
}

bool FWorldPartitionActorDescView::GetActorIsHLODRelevant() const
{
	return ActorDesc->GetActorIsHLODRelevant();
}

FName FWorldPartitionActorDescView::GetHLODLayer() const
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

FName FWorldPartitionActorDescView::GetActorName() const
{
	return ActorDesc->GetActorName();
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

const FGuid& FWorldPartitionActorDescView::GetParentActor() const
{
	return ActorDesc->GetParentActor();
}

const FGuid& FWorldPartitionActorDescView::GetFolderGuid() const
{
	return ActorDesc->GetFolderGuid();
}

bool FWorldPartitionActorDescView::GetContainerInstance(const UActorDescContainer*& OutLevelContainer, FTransform& OutLevelTransform, EContainerClusterMode& OutClusterMode) const
{
	return ActorDesc->GetContainerInstance(OutLevelContainer, OutLevelTransform, OutClusterMode);
}

FName FWorldPartitionActorDescView::GetActorLabelOrName() const
{
	return ActorDesc->GetActorLabelOrName();
}

void FWorldPartitionActorDescView::SetForcedNonSpatiallyLoaded()
{
	if (!bIsForcedNonSpatiallyLoaded)
	{
		bIsForcedNonSpatiallyLoaded = true;
		UE_LOG(LogWorldPartition, Verbose, TEXT("Actor '%s' forced to be non-spatially loaded"), *GetActorLabel().ToString());
	}
}

void FWorldPartitionActorDescView::SetInvalidRuntimeGrid()
{
	bInvalidRuntimeGrid = true;	
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