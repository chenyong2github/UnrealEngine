// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineRenderPass.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineWidgetRenderSetting.generated.h"

class FWidgetRenderer;
class UTextureRenderTarget2D;
class UMoviePipelineBurnInWidget;

namespace MoviePipeline { struct FMoviePipelineRenderPassInitSettings; }

UCLASS(Blueprintable)
class UMoviePipelineWidgetRenderer : public UMoviePipelineRenderPass
{
	GENERATED_BODY()

protected:
	// UMoviePipelineRenderPass Interface
	virtual void SetupImpl(TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>>& InEnginePasses, const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void TeardownImpl() override;
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;
	virtual void RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState) override;
	virtual FText GetFooterText(UMoviePipelineExecutorJob* InJob) const override;
	// ~UMoviePipelineRenderPass Interface


public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "WidgetRendererSettingDisplayName", "UI Renderer (Non-Composited)"); }
	virtual FText GetCategoryText() const { return NSLOCTEXT("MovieRenderPipeline", "WidgetRendererSettingCategoryName", "Rendering"); }
#endif
	virtual bool IsValidOnShots() const override { return false; }
	virtual bool IsValidOnMaster() const override { return true; }
private:
	TSharedPtr<FWidgetRenderer> WidgetRenderer;

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RenderTarget;
};