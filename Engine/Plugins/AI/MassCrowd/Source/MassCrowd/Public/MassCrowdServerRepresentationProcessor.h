// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCrowdRepresentationProcessor.h"

#include "MassCrowdServerRepresentationProcessor.generated.h"

/**
 * Overridden representation processor to make it tied to the crowd on the server via the requirements
 * It is the counter part of the crowd visualization processor on the client.
 */
UCLASS(meta = (DisplayName = "Mass Crowd Server Representation"))
class MASSCROWD_API UMassCrowdServerRepresentationProcessor : public UMassCrowdRepresentationProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdServerRepresentationProcessor();

protected:

	/**
	 * Execution method for this processor
	 * @param EntitySubsystem is the system to execute the lambdas on each entity chunk
	 * @param Context is the execution context to be passed when executing the lambdas
	 */
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;
};