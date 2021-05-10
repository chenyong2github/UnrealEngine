// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HLODBuilder.h"

/**
 * Build an approximated mesh using geometry from the provided actors
 */
class FHLODBuilder_MeshApproximate : public FHLODBuilder
{
	virtual TArray<UPrimitiveComponent*> CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) override;
};
