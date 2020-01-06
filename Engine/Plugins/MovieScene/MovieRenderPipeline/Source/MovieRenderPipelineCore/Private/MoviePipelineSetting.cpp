// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineSetting.h"
#include "MoviePipeline.h"

UMoviePipelineSetting::UMoviePipelineSetting()
	: bEnabled(true)
{
}
void UMoviePipelineSetting::OnMoviePipelineInitialized(UMoviePipeline* InPipeline)
{
	CachedPipeline = MakeWeakObjectPtr(InPipeline);
	SetupForPipelineImpl(InPipeline);
}

UMoviePipeline* UMoviePipelineSetting::GetPipeline() const
{
	// If this check trips then life cycles aren't as expected.
	UMoviePipeline* OutPipeline = CachedPipeline.Get();
	check(OutPipeline);

	return OutPipeline;
}

UWorld* UMoviePipelineSetting::GetWorld() const
{
	return GetPipeline()->GetWorld();
}