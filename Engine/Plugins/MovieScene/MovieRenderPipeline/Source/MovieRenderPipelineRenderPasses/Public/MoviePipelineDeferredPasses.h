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
	virtual void RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState) override;
	virtual void SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void TeardownImpl() override;
	// ~UMovieRenderPassAPI

	// FGCObject Interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// ~FGCObject Interface

	FSceneView* GetSceneViewForSampleState(FSceneViewFamily* ViewFamily, FMoviePipelineRenderPassMetrics& InOutSampleState);


protected:
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const;
	virtual void BlendPostProcessSettings(FSceneView* InView);
	virtual void SetupViewForViewModeOverride(FSceneView* View);
	virtual void MoviePipelineRenderShowFlagOverride(FEngineShowFlags& OutShowFlag) {}
	virtual void RendererSubmission_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas, FSceneViewFamilyContext& InViewFamily);
	virtual void PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas) {}
	virtual bool IsScreenPercentageSupported() const { return true; }	
	virtual bool IsAntiAliasingSupported() const { return true; }
	virtual int32 GetOutputFileSortingOrder() const { return -1; }

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

	/** The lifetime of this SceneViewExtension is only during the rendering process. It is destroyed as part of TearDown. */
	TSharedPtr<FOpenColorIODisplayExtension, ESPMode::ThreadSafe> OCIOSceneViewExtension;
};

USTRUCT(BlueprintType)
struct MOVIERENDERPIPELINERENDERPASSES_API FMoviePipelinePostProcessPass
{
	GENERATED_BODY()

public:
	/** Additional passes add a significant amount of render time. May produce multiple output files if using Screen Percentage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bEnabled;

	/** 
	* Material should be set to Post Process domain, and Blendable Location = After Tonemapping. 
	* This will need bDisableMultisampleEffects enabled for pixels to line up(ie : no DoF, MotionBlur, TAA)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TSoftObjectPtr<UMaterialInterface> Material;
};

UCLASS(BlueprintType)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineDeferredPassBase : public UMoviePipelineImagePassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPassBase();
	
protected:
	// UMoviePipelineRenderPass API
	virtual void SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings) override;
	virtual void TeardownImpl() override;
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSetting_DisplayName_Lit", "Deferred Rendering"); }
#endif
	virtual void PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas) override;
	virtual void MoviePipelineRenderShowFlagOverride(FEngineShowFlags& OutShowFlag) override;
	virtual void RendererSubmission_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas, FSceneViewFamilyContext& InViewFamily) override;
	virtual void GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses) override;
	virtual bool IsAntiAliasingSupported() const { return !bDisableMultisampleEffects; }
	virtual int32 GetOutputFileSortingOrder() const override { return 0; }
	virtual bool IsAlphaInTonemapperRequiredImpl() const override { return bAccumulateMultisampleAlpha; }
	// ~UMoviePipelineRenderPass

	TFunction<void(TUniquePtr<FImagePixelData>&&)> MakeForwardingEndpoint(const FMoviePipelinePassIdentifier InPassIdentifier, const FMoviePipelineRenderPassMetrics& InSampleState);

public:
	/**
	* Should we accumulate the alpha channel when using temporal/spatial sampling? This requires r.PostProcessing.PropagateAlpha
	* to be set to 1 or 2 (see "Enable Alpha Channel Support in Post Processing" under Project Settings > Rendering). This adds
	* ~30% cost to the accumulation so you should not enable it unless necessary. You must delete both the sky and fog to ensure
	* that they do not make all pixels opaque.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bAccumulateMultisampleAlpha;

	/**
	* Certain passes don't support post-processing effects that blend pixels together. These include effects like
	* Depth of Field, Temporal Anti-Aliasing, Motion Blur and chromattic abberation. When these post processing
	* effects are used then each final output pixel is composed of the influence of many other pixels which is
	* undesirable when rendering out an object id pass (which does not support post processing). This checkbox lets
	* you disable them on a per-render basis instead of having to disable them in the editor as well.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Processing")
	bool bDisableMultisampleEffects;

	/**
	* An array of additional post-processing materials to run after the frame is rendered. Using this feature may add a notable amount of render time.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Deferred Renderer Data")
	TArray<FMoviePipelinePostProcessPass> AdditionalPostProcessMaterials;

protected:
	/** While rendering, store an array of the non-null valid materials loaded from AdditionalPostProcessMaterials. Cleared on teardown. */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<UMaterialInterface*> ActivePostProcessMaterials;

	TSharedPtr<FAccumulatorPool, ESPMode::ThreadSafe> AccumulatorPool;
};


UCLASS(BlueprintType)
class UMoviePipelineDeferredPass_Unlit : public UMoviePipelineDeferredPassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPass_Unlit() : UMoviePipelineDeferredPassBase()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("Unlit");
	}
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSetting_DisplayName_Unlit", "Deferred Rendering (Unlit)"); }
#endif
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const override
	{
		OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
		OutViewModeIndex = EViewModeIndex::VMI_Unlit;
	}
	virtual int32 GetOutputFileSortingOrder() const override { return 2; }

};

UCLASS(BlueprintType)
class UMoviePipelineDeferredPass_DetailLighting : public UMoviePipelineDeferredPassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPass_DetailLighting() : UMoviePipelineDeferredPassBase()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("DetailLightingOnly");
	}
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSetting_DisplayName_DetailLighting", "Deferred Rendering (Detail Lighting)"); }
#endif
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const override
	{
		OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
		OutShowFlag.SetLightingOnlyOverride(true);
		OutViewModeIndex = EViewModeIndex::VMI_Lit_DetailLighting;
	}
	virtual int32 GetOutputFileSortingOrder() const override { return 2; }

};

UCLASS(BlueprintType)
class UMoviePipelineDeferredPass_LightingOnly : public UMoviePipelineDeferredPassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPass_LightingOnly() : UMoviePipelineDeferredPassBase()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("LightingOnly");
	}
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSetting_DisplayName_LightingOnly", "Deferred Rendering (Lighting Only)"); }
#endif
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const override
	{
		OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
		OutShowFlag.SetLightingOnlyOverride(true);
		OutViewModeIndex = EViewModeIndex::VMI_LightingOnly;
	}
	virtual int32 GetOutputFileSortingOrder() const override { return 2; }

};

UCLASS(BlueprintType)
class UMoviePipelineDeferredPass_ReflectionsOnly : public UMoviePipelineDeferredPassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPass_ReflectionsOnly() : UMoviePipelineDeferredPassBase()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("ReflectionsOnly");
	}
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSetting_DisplayName_ReflectionsOnly", "Deferred Rendering (Reflections Only)"); }
#endif
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const override
	{
		OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
		OutShowFlag.SetReflectionOverride(true);
		OutViewModeIndex = EViewModeIndex::VMI_ReflectionOverride;
	}
	virtual int32 GetOutputFileSortingOrder() const override { return 2; }
};


UCLASS(BlueprintType)
class UMoviePipelineDeferredPass_PathTracer : public UMoviePipelineDeferredPassBase
{
	GENERATED_BODY()

public:
	UMoviePipelineDeferredPass_PathTracer() : UMoviePipelineDeferredPassBase()
	{
		PassIdentifier = FMoviePipelinePassIdentifier("PathTracer");
	}
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "DeferredBasePassSetting_DisplayName_PathTracer", "Deferred Rendering (Path Tracer)"); }
#endif
	virtual void GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const override
	{
		OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
		OutShowFlag.SetPathTracing(true);
		OutViewModeIndex = EViewModeIndex::VMI_PathTracing;
	}
	virtual int32 GetOutputFileSortingOrder() const override { return 2; }
};

struct MOVIERENDERPIPELINERENDERPASSES_API FAccumulatorPool : public TSharedFromThis<FAccumulatorPool>
{
	struct FAccumulatorInstance
	{
		FAccumulatorInstance(TSharedPtr<MoviePipeline::IMoviePipelineOverlappedAccumulator, ESPMode::ThreadSafe> InAccumulator)
		{
			Accumulator = InAccumulator;
			ActiveFrameNumber = INDEX_NONE;
			bIsActive = false;
		}


		bool IsActive() const;
		void SetIsActive(const bool bInIsActive);

		TSharedPtr<MoviePipeline::IMoviePipelineOverlappedAccumulator, ESPMode::ThreadSafe> Accumulator;
		int32 ActiveFrameNumber;
		FMoviePipelinePassIdentifier ActivePassIdentifier;
		FThreadSafeBool bIsActive;
		FGraphEventRef TaskPrereq;
	};

	TArray<TSharedPtr<FAccumulatorInstance, ESPMode::ThreadSafe>> Accumulators;
	FCriticalSection CriticalSection;


	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> BlockAndGetAccumulator_GameThread(int32 InFrameNumber, const FMoviePipelinePassIdentifier& InPassIdentifier);
};

template<typename AccumulatorType>
struct TAccumulatorPool : FAccumulatorPool
{
	TAccumulatorPool(int32 InNumAccumulators)
		: FAccumulatorPool()
	{
		for (int32 Index = 0; Index < InNumAccumulators; Index++)
		{
			// Create a new instance of the accumulator
			TSharedPtr<MoviePipeline::IMoviePipelineOverlappedAccumulator, ESPMode::ThreadSafe> Accumulator = MakeShared<AccumulatorType, ESPMode::ThreadSafe>();
			Accumulators.Add(MakeShared<FAccumulatorInstance, ESPMode::ThreadSafe>(Accumulator));
		}

	}
};

DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_WaitForAvailableAccumulator"), STAT_MoviePipeline_WaitForAvailableAccumulator, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_WaitForAvailableSurface"), STAT_MoviePipeline_WaitForAvailableSurface, STATGROUP_MoviePipeline);
