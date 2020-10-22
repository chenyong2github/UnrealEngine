// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineDeferredPasses.h"
#include "MovieRenderOverlappedMask.h"
#include "MoviePipelineObjectIdPass.generated.h"


UCLASS(BlueprintType)
class UMoviePipelineObjectIdRenderPass : public UMoviePipelineImagePassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineObjectIdRenderPass();

	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "ObjectIdRenderPassSetting_DisplayName", "Object Ids (Limited)"); }
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const override;
	virtual void RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState) override;
	virtual void TeardownImpl() override;
	virtual void SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;
	virtual bool IsScreenPercentageSupported() const override { return false; }
	virtual int32 GetOutputFileSortingOrder() const override { return 10; }

protected:
	void PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState);

private:
	TSharedPtr<FAccumulatorPool, ESPMode::ThreadSafe> AccumulatorPool;
	TArray<FMoviePipelinePassIdentifier> ExpectedPassIdentifiers;
};