// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassRepresentationProcessor.h"

#include "MassCrowdRepresentationProcessor.generated.h"

/**
 * Overridden representation processor to make it tied to the crowd via the requirements.
 * It is also the base class for all the different type of crowd representation (Visualization & ServerSideRepresentation)
 */
UCLASS(abstract)
class MASSCROWD_API UMassCrowdRepresentationProcessor : public UMassRepresentationProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdRepresentationProcessor();

protected:

	/**
	 * Initialization of this processor
	 * @param Owner of this processor
	 */
	virtual void Initialize(UObject& Owner) override;

	/** Configure the owned FMassEntityQuery instances to express processor's requirements */
	virtual void ConfigureQueries() override;
};