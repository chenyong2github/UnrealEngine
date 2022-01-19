// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCrowdRepresentationProcessor.h"

#include "MassCrowdVisualizationProcessor.generated.h"

/**
 * Overridden visualization processor to make it tied to the crowd via the requirements
 */
UCLASS(meta = (DisplayName = "Mass Crowd Visualization"))
class MASSCROWD_API UMassCrowdVisualizationProcessor : public UMassCrowdRepresentationProcessor
{
	GENERATED_BODY()
public:
	UMassCrowdVisualizationProcessor();

protected:

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;

	/**
	 * Execution method for this processor
	 * @param EntitySubsystem is the system to execute the lambdas on each entity chunk
	 * @param Context is the execution context to be passed when executing the lambdas
	 */
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
};