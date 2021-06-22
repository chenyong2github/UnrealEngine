// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HLODBuilder.h"

/**
 * Build a AWorldPartitionHLOD whose components are ISMC
 */
class FHLODBuilder_Instancing : public FHLODBuilder
{
public:
	virtual bool RequiresCompiledAssets() const override { return false; }

	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) override;
};
