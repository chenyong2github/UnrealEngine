// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineShotConfig.h"
#include "MoviePipelineRenderPass.h"

#define LOCTEXT_NAMESPACE "MovieRenderShotConfig"

TArray<UMoviePipelineRenderPass*> UMoviePipelineShotConfig::GetRenderPasses() const
{
	TArray<UMoviePipelineRenderPass*> RenderPasses;

	for (UMoviePipelineSetting* Setting : GetSettings())
	{
		UMoviePipelineRenderPass* RenderPass = Cast<UMoviePipelineRenderPass>(Setting);
		if (RenderPass)
		{
			RenderPasses.Add(RenderPass);
		}
	}

	return RenderPasses;
}

#undef LOCTEXT_NAMESPACE // "MovieRenderShotConfig"