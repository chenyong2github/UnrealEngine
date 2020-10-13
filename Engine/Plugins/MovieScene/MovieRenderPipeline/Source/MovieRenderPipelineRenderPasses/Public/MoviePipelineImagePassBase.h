// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineRenderPass.h"
#include "MovieRenderPipelineDataTypes.h"
#include "SceneTypes.h"
#include "SceneView.h"
#include "MoviePipelineSurfaceReader.h"
#include "UObject/GCObject.h"
#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"
#include "Templates/Function.h"
#include "Stats/Stats.h"
#include "CanvasTypes.h"
#include "Stats/Stats2.h"
#include "MovieRenderPipelineCoreModule.h"
#include "OpenColorIODisplayExtension.h"

#include "MoviePipelineImagePassBase.generated.h"

class UTextureRenderTarget2D;
struct FImageOverlappedAccumulator;
class FSceneViewFamily;
class FSceneView;
struct FAccumulatorPool;

class FMoviePipelineBackgroundAccumulateTask
{
public:
	FGraphEventRef LastCompletionEvent;

public:
	FGraphEventRef Execute(TUniqueFunction<void()> InFunctor)
	{
		if (LastCompletionEvent)
		{
			LastCompletionEvent = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(InFunctor), GetStatId(), LastCompletionEvent);
		}
		else
		{
			LastCompletionEvent = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(InFunctor), GetStatId());
		}
		return LastCompletionEvent;
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMoviePipelineBackgroundAccumulateTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};

namespace MoviePipeline
{
	struct FImageSampleAccumulationArgs
	{
	public:
		TSharedPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> ImageAccumulator;
		TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputMerger;
		bool bAccumulateAlpha;
	};
}

UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImagePassBase : public UMoviePipelineRenderPass
{
	GENERATED_BODY()

public:
	UMoviePipelineImagePassBase()
		: UMoviePipelineRenderPass()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("ImagePassBase");
	}

protected:

	// UMoviePipelineRenderPass API
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;
	virtual void SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void TeardownImpl() override;
	// ~UMovieRenderPassAPI

	// FGCObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// ~FGCObject Interface

	FSceneView* GetSceneViewForSampleState(FSceneViewFamily* ViewFamily, FMoviePipelineRenderPassMetrics& InOutSampleState);


protected:
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const;
	virtual TSharedPtr<FSceneViewFamilyContext> CalculateViewFamily(FMoviePipelineRenderPassMetrics& InOutSampleState);
	virtual void BlendPostProcessSettings(FSceneView* InView);
	virtual void SetupViewForViewModeOverride(FSceneView* View);
	virtual void MoviePipelineRenderShowFlagOverride(FEngineShowFlags& OutShowFlag) {}
	virtual bool IsScreenPercentageSupported() const { return true; }	
	virtual bool IsAntiAliasingSupported() const { return true; }
	virtual int32 GetOutputFileSortingOrder() const { return -1; }
	virtual FSceneViewStateInterface* GetSceneViewStateInterface() { return ViewState.GetReference(); }
	virtual UTextureRenderTarget2D* GetViewRenderTarget() const { return TileRenderTarget.Get(); }
	virtual void AddViewExtensions(FSceneViewFamilyContext& InContext, FMoviePipelineRenderPassMetrics& InOutSampleState) { }
public:
	

protected:
	/** A temporary render target that we render the view to. */
	TWeakObjectPtr<UTextureRenderTarget2D> TileRenderTarget;

	/** The history for the view */
	FSceneViewStateReference ViewState;

	/** A queue of surfaces that the render targets can be copied to. If no surface is available the game thread should hold off on submitting more samples. */
	TSharedPtr<FMoviePipelineSurfaceQueue> SurfaceQueue;

	FMoviePipelinePassIdentifier PassIdentifier;

	/** Accessed by the Render Thread when starting up a new task. */
	FGraphEventArray OutstandingTasks;
};

DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_WaitForAvailableAccumulator"), STAT_MoviePipeline_WaitForAvailableAccumulator, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_WaitForAvailableSurface"), STAT_MoviePipeline_WaitForAvailableSurface, STATGROUP_MoviePipeline);
