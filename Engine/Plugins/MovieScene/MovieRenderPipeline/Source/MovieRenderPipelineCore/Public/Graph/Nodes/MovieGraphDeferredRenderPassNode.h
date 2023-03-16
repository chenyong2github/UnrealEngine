// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/Nodes/MovieGraphRenderPassNode.h"
#include "Graph/MovieGraphDataTypes.h"
#include "SceneTypes.h"
#include "Camera/CameraTypes.h"
#include "MovieRenderPipelineDataTypes.h"
#include "Engine/EngineTypes.h"
#include "Async/TaskGraphFwd.h"
#include "MovieGraphDeferredRenderPassNode.generated.h"

// Forward Declares
class UMovieGraphDefaultRenderer;
namespace UE::MovieGraph::DefaultRenderer { struct FRenderTargetInitParams; }
struct FMovieGraphRenderPassSetupData;
struct FMovieGraphRenderPassLayerData;
struct FImageOverlappedAccumulator;

// For FViewFamilyContextInitData
class FRenderTarget;
class UWorld;
class FSceneView;
class AActor;
class FSceneViewFamilyContext;
class FCanvas;

namespace UE::MovieGraph
{
	struct FMovieGraphRenderDataAccumulationArgs
	{
	public:
		TWeakPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> ImageAccumulator;
		// TWeakPtr<IMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputMerger;

		// If it's the first sample then we will reset the accumulator to a clean slate before accumulating into it.
		bool bIsFirstSample;
		// If it's the last sample, then we will trigger moving the data to the output merger. It can be both the first and last sample at the same time.
		bool bIsLastSample;
		// Does this accumulation event depend on a previous accumulation event before it can be executed?
		FGraphEventRef TaskPrerequisite;
	};

	// void MOVIERENDERPIPELINERENDERPASSES_API AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const MoviePipeline::FImageSampleAccumulationArgs& InParams);
}

UCLASS()
class UMovieGraphDeferredRenderPassNode : public UMovieGraphRenderPassNode
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FText GetMenuDescription() const override
	{
		return NSLOCTEXT("MovieGraphNodes", "DeferredRenderPassGraphNode_Description", "Deferred Renderer");
	}
#endif
protected:
	// UMovieGraphRenderPassNode Interface
	virtual FString GetRendererNameImpl() const override { return TEXT("Deferred"); }
	virtual void SetupImpl(const FMovieGraphRenderPassSetupData& InSetupData) override;
	virtual void TeardownImpl() override;
	virtual void RenderImpl(const FMovieGraphTimeStepData& InTimeData) override;
	// ~UMovieGraphRenderPassNode Interface

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	struct FViewFamilyContextInitData
	{
		FViewFamilyContextInitData()
			: RenderTarget(nullptr)
			, World(nullptr)
			, SceneCaptureSource(ESceneCaptureSource::SCS_MAX)
			, bWorldIsPaused(false)
			, GlobalScreenPercentageFraction(1.0f)
			, OverscanFraction(0.f)
			, FrameIndex(-1)
			, bCameraCut(false)
			, AntiAliasingMethod(EAntiAliasingMethod::AAM_None)
			, View(nullptr)
			, SceneViewStateReference(nullptr)
			, ViewActor(nullptr)
		{
		}

		class FRenderTarget* RenderTarget;
		class UWorld* World;
		FMovieGraphTimeStepData TimeData;
		ESceneCaptureSource SceneCaptureSource;
		bool bWorldIsPaused;
		float GlobalScreenPercentageFraction;
		float OverscanFraction;
		int32 FrameIndex;
		bool bCameraCut;
		EAntiAliasingMethod AntiAliasingMethod;

		// Ownership of this will be passed to the FSceneViewFamilyContext
		FSceneView* View;

		FSceneViewStateInterface* SceneViewStateReference;

		// Camera Setup
		FMinimalViewInfo MinimalViewInfo;
		class AActor* ViewActor;
	};

	struct FMovieGraphDeferredRenderPass
	{
	public:
		void Setup(TWeakObjectPtr<UMovieGraphDefaultRenderer> InRenderer, TWeakObjectPtr<UMovieGraphDeferredRenderPassNode> InRenderPassNode, const FMovieGraphRenderPassLayerData& InLayer);
		void Teardown();
		void Render(const FMovieGraphTimeStepData& InTimeData);
		void AddReferencedObjects(FReferenceCollector& Collector);

	protected:
		TSharedRef<FSceneViewFamilyContext> AllocateSceneViewFamilyContext(const FViewFamilyContextInitData& InInitData);
		FSceneView* AllocateSceneView(TSharedPtr<FSceneViewFamilyContext> InViewFamilyContext, FViewFamilyContextInitData& InInitData) const;
		void ApplyMoviePipelineOverridesToViewFamily(TSharedRef<FSceneViewFamilyContext> InOutFamily, const FViewFamilyContextInitData& InInitData);
		void PostRendererSubmission(const UE::MovieGraph::FMovieGraphSampleState& InSampleState, const UE::MovieGraph::DefaultRenderer::FRenderTargetInitParams& InRenderTargetInitParams, FCanvas& InCanvas);


	protected:
		FMovieGraphRenderPassLayerData LayerData;

		// Scene View history used by the renderer 
		FSceneViewStateReference SceneViewState;

		TWeakObjectPtr<class UMovieGraphDefaultRenderer> Renderer;
		TWeakObjectPtr<class UMovieGraphDeferredRenderPassNode> RenderPassNode;
	};

	TArray<TUniquePtr<FMovieGraphDeferredRenderPass>> CurrentInstances;
};