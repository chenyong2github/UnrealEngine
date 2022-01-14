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
};