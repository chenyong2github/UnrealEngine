// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionHandle.h"

class AWorldPartitionHLOD;
class UPrimitiveComponent;
class UHLODLayer;

DECLARE_LOG_CATEGORY_EXTERN(LogHLODBuilder, Log, All);


/**
 * Base class for all HLODBuilders
 */
class FHLODBuilder
{
public:
	virtual ~FHLODBuilder() {}

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) = 0;

	void Build(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<FWorldPartitionReference>& InSubActors);

	static TArray<UPrimitiveComponent*> GatherPrimitiveComponents(const TArray<FWorldPartitionReference>& InActors);

	void DisableCollisions(UPrimitiveComponent* Component);
};
