// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineSetting.h"
#include "MoviePipeline.h"

void UMoviePipelineSetting::OnInitializedForPipeline(UMoviePipeline* InPipeline)
{
	CachedPipeline = MakeWeakObjectPtr(InPipeline);
	OnInitializedForPipelineImpl(InPipeline);
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