// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartitionRuntimeSpatialHashCell.generated.h"

UCLASS(Abstract, Within = WorldPartition)
class UWorldPartitionRuntimeSpatialHashCell : public UWorldPartitionRuntimeCell
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	int32 Level;

	UPROPERTY()
	int32 Priority;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UActorContainer* ActorContainer;
#endif
};