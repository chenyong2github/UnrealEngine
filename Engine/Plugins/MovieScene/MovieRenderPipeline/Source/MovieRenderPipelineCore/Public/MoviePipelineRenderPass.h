// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineSetting.h"
#include "ImageWriteStream.h"
#include "MoviePipelineRenderPass.generated.h"

UCLASS(Blueprintable, Abstract)
class MOVIERENDERPIPELINECORE_API UMoviePipelineRenderPass : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	void Setup(TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>>& InEnginePasses, const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
	{
		SetupImpl(InEnginePasses, InPassInitSettings);
	}

	void Teardown()
	{
		TeardownImpl();
	}

	/** An array of identifiers for the output buffers expected as a result of this render pass. */
	void GatherOutputPasses(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
	{
		GatherOutputPassesImpl(ExpectedRenderPasses);
	}

	/** The required engine passes this pass needs to operate. */
	void GetRequiredEnginePasses(TSet<FMoviePipelinePassIdentifier>& RequiredEnginePasses)
	{
		GetRequiredEnginePassesImpl(RequiredEnginePasses);
	}

	/** This will called for each requested sample. This should only be used if you're not trying to share engine passes with other things. */
	void RenderSample_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState)
	{
		RenderSample_GameThreadImpl(InSampleState);
	}

protected:
	virtual bool IsValidOnShots() const override { return true; }
	virtual bool IsValidOnMaster() const override { return true; }
#if WITH_EDITOR
	virtual FText GetCategoryText() const override { return NSLOCTEXT("MovieRenderPipeline", "RenderingCategoryName_Text", "Rendering"); }
#endif
protected:
	virtual void GetRequiredEnginePassesImpl(TSet<FMoviePipelinePassIdentifier>& RequiredEnginePasses) {}

	virtual void SetupImpl(TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>>& InEnginePasses, const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) {}

	virtual void TeardownImpl() {}

	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) {}

	virtual void RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState) {}
};