// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWorldPartitionHLODUtilities.h"

/**
* FWorldPartitionHLODUtilities implementation
*/
class FWorldPartitionHLODUtilities : public IWorldPartitionHLODUtilities
{
public:
	virtual TArray<AWorldPartitionHLOD*> CreateHLODActors(FHLODCreationContext& InCreationContext, const FHLODCreationParams& InCreationParams, const TSet<FActorInstance>& InActors, const TArray<const UDataLayer*>& InDataLayers) override;
	virtual uint32 BuildHLOD(AWorldPartitionHLOD* InHLODActor) override;
};
