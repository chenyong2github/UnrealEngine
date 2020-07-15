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
#include "MoviePipelineDeferredPasses.generated.h"

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

UCLASS(BlueprintType)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineDeferredPassBase : public UMoviePipelineRenderPass
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPassBase()
		: UMoviePipelineRenderPass()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("FinalImage");
	}
protected:

	// UMoviePipelineRenderPass API
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;
	virtual void SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void TeardownImpl() override;
	virtual void RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState) override;
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSettingDisplayName", "Deferred Rendering"); }
	// ~UMovieRenderPassAPI

	// FGCObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// ~FGCObject Interface

	FSceneView* GetSceneViewForSampleState(FSceneViewFamily* ViewFamily, const FMoviePipelineRenderPassMetrics& InSampleState);
	void OnBackbufferSampleReady(TArray<FFloat16Color>& InPixelData, FMoviePipelineRenderPassMetrics InSampleState);
private:
	void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const;
	void BlendPostProcessSettings(FSceneView* InView);
	void SetupViewForViewModeOverride(FSceneView* View);
private:
	TSharedPtr<FAccumulatorPool, ESPMode::ThreadSafe> AccumulatorPool;

	/** A queue of surfaces that the render targets can be copied to. If no surface is available the game thread should hold off on submitting more samples. */
	TSharedPtr<FMoviePipelineSurfaceQueue> SurfaceQueue;

	/** A temporary render target that we render the view to. */
	TWeakObjectPtr<UTextureRenderTarget2D> TileRenderTarget;

	/** The history for the view */
	FSceneViewStateReference ViewState;

	FMoviePipelinePassIdentifier PassIdentifier;

	/** Accessed by the Render Thread when starting up a new task. */
	FGraphEventArray OutstandingTasks;

	FGraphEventRef TaskPrereq;
};

struct FAccumulatorPool
{
	struct FAccumulatorInstance
	{
		FAccumulatorInstance();

		bool IsActive() const;
		void SetIsActive(const bool bInIsActive);

		TSharedPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> Accumulator;
		int32 ActiveFrameNumber;
		FThreadSafeBool bIsActive;
	};

	TArray<TSharedPtr<FAccumulatorInstance, ESPMode::ThreadSafe>> Accumulators;
	FCriticalSection CriticalSection;

	FAccumulatorPool(int32 InNumAccumulators);
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> BlockAndGetAccumulator_GameThread(int32 InFrameNumber);
};