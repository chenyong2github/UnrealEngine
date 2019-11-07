// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineRenderPass.h"
#include "SceneTypes.h"
#include "MoviePipelineBackbufferPass.generated.h"

// Forward Declare
class FSceneViewport;
class UTextureRenderTarget2D;
class FCanvas;
class FSceneViewFamily;
class FSceneView;
class UWorld;
struct FImagePixelPipe;
struct FImageOverlappedAccumulator;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineBackbufferPass : public UMoviePipelineRenderPass
{
	GENERATED_BODY()

protected:

	virtual void SetupImpl(const FMoviePipelineRenderPassInitSettings& InInitSettings) override;
	virtual void TeardownImpl() override;
	virtual void CaptureFrameImpl(const FMoviePipelineRenderPassMetrics& OutputFrameMetrics) override;
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;

	FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const FMoviePipelineRenderPassMetrics& InPassMetrics);

private:
	FSceneViewStateReference ViewState;


	UPROPERTY(Transient)
	UTextureRenderTarget2D* TileRenderTarget;

	TSharedPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> ImageTileAccumulator;
};