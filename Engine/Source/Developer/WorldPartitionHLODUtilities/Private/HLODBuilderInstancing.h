// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/HLOD/HLODBuilder.h"
#include "HLODBuilderInstancing.generated.h"


/**
 * Build a AWorldPartitionHLOD whose components are ISMC
 */
UCLASS()
class UHLODBuilderInstancing : public UHLODBuilder
{
	 GENERATED_UCLASS_BODY()

public:
	virtual bool RequiresCompiledAssets() const override { return false; }

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) const override;
};
