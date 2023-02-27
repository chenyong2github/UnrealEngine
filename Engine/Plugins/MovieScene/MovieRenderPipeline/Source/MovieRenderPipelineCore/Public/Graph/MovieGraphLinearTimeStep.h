// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Graph/MovieGraphDataTypes.h"
#include "MovieGraphLinearTimeStep.generated.h"

/**
* This class 
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphLinearTimeStep : public UMovieGraphTimeStepBase
{
	GENERATED_BODY()
public:
	virtual void CalculateTimeStep(FMovieGraphTimeStepData& OutTimeData) override;
};