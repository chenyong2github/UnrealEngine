// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassVisualizationLODProcessor.h"
#include "MassCrowdVisualizationLODProcessor.generated.h"

/*
 * Created a crowd version for visualization of the UMassVisualizationLODProcessor as we want to use a custom version of the LOD info
 * that combines the visualization and simulation LOD.
 */
UCLASS(meta=(DisplayName="Crowd visualization LOD"))
class MASSCROWD_API UMassCrowdVisualizationLODProcessor : public UMassVisualizationLODProcessor
{
	GENERATED_BODY()
public:
	UMassCrowdVisualizationLODProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;
};
