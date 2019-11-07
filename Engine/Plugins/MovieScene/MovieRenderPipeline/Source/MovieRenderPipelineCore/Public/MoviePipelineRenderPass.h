// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
	void Setup(const FMoviePipelineRenderPassInitSettings& InInitSettings)
	{
		InitSettings = InInitSettings;
		SetupImpl(InInitSettings);
	}

	void CaptureFrame(const FMoviePipelineRenderPassMetrics& OutputFrameMetrics)
	{
		CaptureFrameImpl(OutputFrameMetrics);
	}

	void Teardown()
	{
		TeardownImpl();
	}

	void GatherOutputPasses(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
	{
		GatherOutputPassesImpl(ExpectedRenderPasses);
	}

protected:
	virtual void SetupImpl(const FMoviePipelineRenderPassInitSettings& InInitSettings) {}

	virtual void CaptureFrameImpl(const FMoviePipelineRenderPassMetrics& OutputFrameMetrics) {}

	virtual void TeardownImpl() {}

	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) {}
		
protected:
	UPROPERTY()
	FMoviePipelineRenderPassInitSettings InitSettings;
};