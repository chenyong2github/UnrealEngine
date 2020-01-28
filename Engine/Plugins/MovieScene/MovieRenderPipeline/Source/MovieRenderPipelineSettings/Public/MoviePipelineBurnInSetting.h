// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineRenderPass.h"
#include "MovieRenderPipelineDataTypes.h"
#include "UObject/SoftObjectPath.h"
#include "MoviePipelineBurnInSetting.generated.h"

class FWidgetRenderer;
class SVirtualWindow;
class UTextureRenderTarget2D;
class UMoviePipelineBurnInWidget;

namespace MoviePipeline { struct FMoviePipelineRenderPassInitSettings; }

UCLASS(Blueprintable)
class UMoviePipelineBurnInSetting : public UMoviePipelineRenderPass
{
	GENERATED_BODY()

	UMoviePipelineBurnInSetting()
		: BurnInClass(TEXT("/MovieRenderPipeline/Blueprints/DefaultBurnIn.DefaultBurnIn_C"))
	{
	}

protected:
	// UMoviePipelineRenderPass Interface
	virtual void SetupImpl(TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>>& InEnginePasses, const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void TeardownImpl() override;
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;
	virtual void RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState) override;
	// ~UMoviePipelineRenderPass Interface


public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "BurnInSettingDisplayName", "Burn In"); }
	virtual FText GetCategoryText() const { return NSLOCTEXT("MovieRenderPipeline", "DefaultCategoryName_Text", "Settings"); }
#endif
	virtual bool IsValidOnShots() const override { return false; }
	virtual bool IsValidOnMaster() const override { return true; }
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MetaClass="MoviePipelineBurnInWidget"), Category = "Movie Pipeline")
	FSoftClassPath BurnInClass;

private:
	FIntPoint OutputResolution;
	TSharedPtr<FWidgetRenderer> WidgetRenderer;
	TSharedPtr<SVirtualWindow> VirtualWindow;

	UPROPERTY(Transient)
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(Transient)
	UMoviePipelineBurnInWidget* BurnInWidgetInstance;
};