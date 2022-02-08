// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/HLOD/HLODBuilder.h"
#include "HLODBuilderInstancing.generated.h"


/**
 * Build a AWorldPartitionHLOD whose components are ISMC
 */
UCLASS()
class WORLDPARTITIONHLODUTILITIES_API UHLODBuilderInstancing : public UHLODBuilder
{
	 GENERATED_UCLASS_BODY()

public:
	virtual bool RequiresCompiledAssets() const override { return false; }
	virtual bool RequiresWarmup() const override { return false; }

	virtual TArray<UActorComponent*> Build(const FHLODBuildContext& InHLODBuildContext, const TArray<UActorComponent*>& InSourceComponents) const override;
};
