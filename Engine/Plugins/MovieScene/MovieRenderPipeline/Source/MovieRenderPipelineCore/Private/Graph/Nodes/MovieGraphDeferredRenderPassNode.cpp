// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Nodes/MovieGraphDeferredRenderPassNode.h"
#include "Graph/MovieGraphDataTypes.h"
#include "Graph/MovieGraphDefaultRenderer.h"
#include "Graph/MovieGraphPipeline.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MovieRenderOverlappedImage.h"
#include "MoviePipelineSurfaceReader.h"

#include "EngineModule.h"
#include "SceneManagement.h"
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LegacyScreenPercentageDriver.h"
#include "SceneViewExtensionContext.h"
#include "OpenColorIODisplayExtension.h"
#include "TextureResource.h"
#include "MovieRenderOverlappedImage.h"
#include "MoviePipelineSurfaceReader.h"

void UMovieGraphDeferredRenderPassNode::SetupImpl(const FMovieGraphRenderPassSetupData& InSetupData)
{
	// To make the implementation simpler, we make one instance of FMovieGraphDeferredRenderPas
	// per camera, and per render layer. These objects can pull from common pools to share state,
	// which gives us a better overview of how many resources are being used by MRQ.
	for (const FMovieGraphRenderPassLayerData& LayerData : InSetupData.Layers)
	{
		TUniquePtr<FMovieGraphDeferredRenderPass> RendererInstance = MakeUnique<FMovieGraphDeferredRenderPass>();
		RendererInstance->Setup(InSetupData.Renderer, this, LayerData);
		CurrentInstances.Add(MoveTemp(RendererInstance));
	}
}

void UMovieGraphDeferredRenderPassNode::TeardownImpl()
{
	// We don't need to flush the rendering commands as we assume the MovieGraph
	// Renderer has already done that once, so all data for all passes should
	// have been submitted to the GPU (and subsequently read back) by now.
	for (TUniquePtr<FMovieGraphDeferredRenderPass>& Instance : CurrentInstances)
	{
		Instance->Teardown();
	}
	CurrentInstances.Reset();
}


void UMovieGraphDeferredRenderPassNode::RenderImpl(FMovieGraphTraversalContext InFrameTraversalContext, const FMovieGraphTimeStepData& InTimeData)
{
	for (TUniquePtr<FMovieGraphDeferredRenderPass>& Instance : CurrentInstances)
	{
		Instance->Render(InFrameTraversalContext, InTimeData);
	}
}

void UMovieGraphDeferredRenderPassNode::GatherOutputPassesImpl(TArray<FMovieGraphRenderDataIdentifier>& OutExpectedPasses) const
{
	for (const TUniquePtr<FMovieGraphDeferredRenderPass>& Instance : CurrentInstances)
	{
		Instance->GatherOutputPassesImpl(OutExpectedPasses);
	}
}

void UMovieGraphDeferredRenderPassNode::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UMovieGraphDeferredRenderPassNode* This = CastChecked<UMovieGraphDeferredRenderPassNode>(InThis);
	for (TUniquePtr<FMovieGraphDeferredRenderPass>& Instance : This->CurrentInstances)
	{
		Instance->AddReferencedObjects(Collector);
	}
}

void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::Setup(TWeakObjectPtr<UMovieGraphDefaultRenderer> InRenderer, TWeakObjectPtr<UMovieGraphDeferredRenderPassNode> InRenderPassNode, const FMovieGraphRenderPassLayerData& InLayer)
{
	LayerData = InLayer;
	Renderer = InRenderer;
	RenderPassNode = InRenderPassNode;


	// Figure out how big each sub-region (tile) is.
	// const int32 TileSize = Graph->FindSetting(LayerData.LayerName, "highres.tileSize");
	//int32 TileCount = 1; // ToDo: We should do tile sizes (ie: 1024x512) instead
	//FIntPoint BackbufferResolution = FIntPoint(
	//	FMath::CeilToInt((float)OutputCameraResolution.X / (float)TileCount),
	//	FMath::CeilToInt((float)OutputCameraResolution.Y / (float)TileCount));

	// BackbufferResolution = HighResSettings->CalculatePaddedBackbufferSize(BackbufferResolution);
	
	// Create a view render target for our given resolution. This is require for the FCanvas/FSceneViewAPI. We pool these.
	// Renderer->GetOrCreateViewRenderTarget(BackbufferResolution);
	// CreateSurfaceQueueImpl(BackbufferResolution);

	SceneViewState.Allocate(Renderer->GetWorld()->GetFeatureLevel());

}

void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::GatherOutputPassesImpl(TArray<FMovieGraphRenderDataIdentifier>& OutExpectedPasses) const
{
	// ToDo: Store this when the render pass is created, so we don't create it in two places at once.
	// This is a problem right now due to the camera name not being pre-resolved.
	FMovieGraphRenderDataIdentifier RenderDataIdentifier;
	RenderDataIdentifier.RenderLayerName = LayerData.LayerName;
	RenderDataIdentifier.RendererName = RenderPassNode->GetRendererName();
	RenderDataIdentifier.SubResourceName = TEXT("beauty");
	RenderDataIdentifier.CameraName = TEXT("Unnamed_Camera");

	OutExpectedPasses.Add(RenderDataIdentifier);
}
void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::Teardown()
{
	FSceneViewStateInterface* Ref = SceneViewState.GetReference();
	if (Ref)
	{
		Ref->ClearMIDPool();
	}
	SceneViewState.Destroy();
}

void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::AddReferencedObjects(FReferenceCollector& Collector)
{
	FSceneViewStateInterface* Ref = SceneViewState.GetReference();
	if (Ref)
	{
		Ref->AddReferencedObjects(Collector);
	}
}

void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::Render(FMovieGraphTraversalContext InFrameTraversalContext, const FMovieGraphTimeStepData& InTimeData)
{
	// This is the size we actually render at.
	UE::MovieGraph::DefaultRenderer::FRenderTargetInitParams RenderTargetInitParams;
	RenderTargetInitParams.Size = FIntPoint(1920, 1080);
	
	// OCIO: Since this is a manually created Render target we don't need Gamma to be applied.
	// We use this render target to render to via a display extension that utilizes Display Gamma
	// which has a default value of 2.2 (DefaultDisplayGamma), therefore we need to set Gamma on this render target to 2.2 to cancel out any unwanted effects.
	RenderTargetInitParams.TargetGamma = FOpenColorIODisplayExtension::DefaultDisplayGamma;
	RenderTargetInitParams.PixelFormat = EPixelFormat::PF_FloatRGBA;

	UTextureRenderTarget2D* RenderTarget = Renderer->GetOrCreateViewRenderTarget(RenderTargetInitParams);

	FViewFamilyContextInitData InitData;
	InitData.RenderTarget = RenderTarget->GameThread_GetRenderTargetResource();
	InitData.World = Renderer->GetWorld();

	APlayerController* LocalPlayerController = Renderer->GetWorld()->GetFirstPlayerController();
	if (LocalPlayerController && LocalPlayerController->PlayerCameraManager)
	{
		InitData.MinimalViewInfo = LocalPlayerController->PlayerCameraManager->GetCameraCacheView();
		InitData.ViewActor = LocalPlayerController->GetViewTarget();
	}
	else
	{
		InitData.MinimalViewInfo = FMinimalViewInfo();
		InitData.ViewActor = nullptr;
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to find Local Player Controller/Camera Manager to get viewpoint!"));
	}

	// ToDo:
	InitData.TimeData = InTimeData;
	InitData.SceneCaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	InitData.bWorldIsPaused = false;
	InitData.GlobalScreenPercentageFraction = FLegacyScreenPercentageDriver::GetCVarResolutionFraction();
	InitData.OverscanFraction = 0.f;
	InitData.FrameIndex = 1;
	InitData.bCameraCut = false;
	InitData.AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
	InitData.SceneViewStateReference = SceneViewState.GetReference();


	// Allocate the view we want to render from, then a Family for the view to live in.
	// We apply a lot of MRQ-specific overrides to the ViewFamily and View, so do those next.
	// Allocating the Scene View automatically 
	TSharedRef<FSceneViewFamilyContext> ViewFamily = AllocateSceneViewFamilyContext(InitData);
	AllocateSceneView(ViewFamily, InitData);
	ApplyMoviePipelineOverridesToViewFamily(ViewFamily, InitData);

	
	FRenderTarget* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	check(RenderTarget);

	FHitProxyConsumer* HitProxyConsumer = nullptr;
	const float DPIScale = 1.0f;
	FCanvas Canvas = FCanvas(RenderTargetResource, HitProxyConsumer, Renderer->GetWorld(), Renderer->GetWorld()->GetFeatureLevel(), FCanvas::CDM_DeferDrawing, DPIScale);

	// Submit the renderer to be rendered
	GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.ToSharedPtr().Get());

	// If this was just to contribute to the history buffer, no need to go any further.
	//if (InSampleState.bDiscardResult)
	//{
	//	return;
	//}

	FMovieGraphRenderDataIdentifier RenderDataIdentifier;
	RenderDataIdentifier.RenderLayerName = LayerData.LayerName;
	RenderDataIdentifier.RendererName = RenderPassNode->GetRendererName();
	RenderDataIdentifier.SubResourceName = TEXT("beauty");
	RenderDataIdentifier.CameraName = TEXT("Unnamed_Camera");
	//if (InitData.ViewActor)
	//{
	//	RenderDataIdentifier.CameraName = InitData.ViewActor->GetName();
	//}

	// Take our per-frame Traversal Context and update it with context specific to this sample.
	FMovieGraphTraversalContext UpdatedTraversalContext = InFrameTraversalContext;
	UpdatedTraversalContext.Time = InTimeData;
	UpdatedTraversalContext.RenderDataIdentifier = RenderDataIdentifier;


	UE::MovieGraph::FMovieGraphSampleState SampleState;
	SampleState.TraversalContext = MoveTemp(UpdatedTraversalContext);
	SampleState.BackbufferResolution = RenderTargetInitParams.Size;

	// Readback + Accumulate.
	PostRendererSubmission(SampleState, RenderTargetInitParams, Canvas);
}

void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::PostRendererSubmission(
	const UE::MovieGraph::FMovieGraphSampleState& InSampleState,
	const UE::MovieGraph::DefaultRenderer::FRenderTargetInitParams& InRenderTargetInitParams, FCanvas& InCanvas)
{
	// ToDo: Draw Letterboxing
	

	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	FMoviePipelineAccumulatorPoolPtr SampleAccumulatorPool = Renderer->GetOrCreateAccumulatorPool<FImageOverlappedAccumulator>();
	UE::MovieGraph::DefaultRenderer::FSurfaceAccumulatorPool::FInstancePtr AccumulatorInstance = nullptr;
	{
		// SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		AccumulatorInstance = SampleAccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.TraversalContext.Time.OutputFrameNumber, InSampleState.TraversalContext.RenderDataIdentifier);
	}

	FMoviePipelineSurfaceQueuePtr LocalSurfaceQueue = Renderer->GetOrCreateSurfaceQueue(InRenderTargetInitParams);
	LocalSurfaceQueue->BlockUntilAnyAvailable();

	UE::MovieGraph::FMovieGraphRenderDataAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = Renderer->GetOwningGraph()->GetOutputMerger();
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(AccumulatorInstance->Accumulator);
		AccumulationArgs.bIsFirstSample = InSampleState.TraversalContext.Time.bIsFirstTemporalSampleForFrame; // FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample()
		AccumulationArgs.bIsLastSample = InSampleState.TraversalContext.Time.bIsLastTemporalSampleForFrame; // FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample()
		AccumulationArgs.TaskPrerequisite = AccumulatorInstance->TaskPrereq;
	}

	auto OnSurfaceReadbackFinished = [this, InSampleState, AccumulationArgs, AccumulatorInstance](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		UE::MovieGraph::DefaultRenderer::FMovieGraphAccumulationTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = AccumulationArgs.TaskPrerequisite;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(InPixelData), InSampleState, AccumulationArgs, AccumulatorInstance]() mutable
			{
				// Enqueue a encode for this frame onto our worker thread.
				UE::MovieGraph::AccumulateSample_TaskThread(MoveTemp(PixelData), InSampleState, AccumulationArgs);

				// We have to defer clearing the accumulator until after sample accumulation has finished
				if (AccumulationArgs.bIsLastSample)
				{
					// Final sample has now been executed, break the pre-req chain and free the accumulator for reuse.
					AccumulatorInstance->bIsActive = false;
					AccumulatorInstance->TaskPrereq = nullptr;
				}
			});
		AccumulatorInstance->TaskPrereq = Event;

		this->Renderer->AddOutstandingRenderTask_AnyThread(Event);
	};

	FRenderTarget* RenderTarget = InCanvas.GetRenderTarget();

	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[LocalSurfaceQueue, OnSurfaceReadbackFinished, RenderTarget](FRHICommandListImmediate& RHICmdList) mutable
		{
			// The legacy surface reader takes the payload just so it can shuffle it into our callback, but we can just include the data
			// directly in the callback, so this is just a dummy payload.
			TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
			LocalSurfaceQueue->OnRenderTargetReady_RenderThread(RenderTarget->GetRenderTargetTexture(), FramePayload, MoveTemp(OnSurfaceReadbackFinished));
		});
		
}

TSharedRef<FSceneViewFamilyContext> UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::AllocateSceneViewFamilyContext(const FViewFamilyContextInitData& InInitData)
{
	FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	EViewModeIndex ViewModeIndex = EViewModeIndex::VMI_Lit;

	const bool bIsPerspective = InInitData.MinimalViewInfo.ProjectionMode == ECameraProjectionMode::Type::Perspective;

	// Allow the Engine Showflag system to override our engine showflags, based on our view mode index.
	// This is required for certain debug view modes (to have matching show flags set for rendering).
	ApplyViewMode(/*In*/ ViewModeIndex, bIsPerspective, /*InOut*/ShowFlags);

	// And then we have to let another system override them again (based on cvars, etc.)
	EngineShowFlagOverride(ESFIM_Game, ViewModeIndex, ShowFlags, false);

	TSharedRef<FSceneViewFamilyContext> OutViewFamily = MakeShared<FSceneViewFamilyContext>(FSceneViewFamily::ConstructionValues(
		InInitData.RenderTarget,
		InInitData.World->Scene,
		ShowFlags)
		.SetTime(FGameTime::CreateUndilated(InInitData.TimeData.WorldSeconds, InInitData.TimeData.FrameDeltaTime))
		.SetRealtimeUpdate(true));

	// Used to specify if the Tone Curve is being applied or not to our Linear Output data
	OutViewFamily->SceneCaptureSource = InInitData.SceneCaptureSource;
	OutViewFamily->bWorldIsPaused = InInitData.bWorldIsPaused;
	OutViewFamily->ViewMode = ViewModeIndex;
	OutViewFamily->bOverrideVirtualTextureThrottle = true;

	// ToDo: Let settings modify the ScreenPercentageInterface so third party screen percentages are supported.

	// If UMoviePipelineViewFamilySetting never set a Screen percentage interface we fallback to default.
	if (OutViewFamily->GetScreenPercentageInterface() == nullptr)
	{
		OutViewFamily->SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(*OutViewFamily, InInitData.GlobalScreenPercentageFraction));
	}

	return OutViewFamily;
}


FSceneView* UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::AllocateSceneView(TSharedPtr<FSceneViewFamilyContext> InViewFamilyContext, FViewFamilyContextInitData& InInitData) const
{
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = InViewFamilyContext.Get();
	ViewInitOptions.ViewOrigin = InInitData.MinimalViewInfo.Location;
	ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), FIntPoint(InInitData.RenderTarget->GetSizeXY().X, InInitData.RenderTarget->GetSizeXY().Y)));
	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(InInitData.MinimalViewInfo.Rotation);
	ViewInitOptions.ViewActor = InInitData.ViewActor;

	// Rotate the view 90 degrees to match the rest of the engine.
	ViewInitOptions.ViewRotationMatrix = ViewInitOptions.ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	float ViewFOV = InInitData.MinimalViewInfo.FOV;

	// Inflate our FOV to support the overscan 
	ViewFOV = 2.0f * FMath::RadiansToDegrees(FMath::Atan((1.0f + InInitData.OverscanFraction) * FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5f))));
	float DofSensorScale = 1.0f;

	// ToDo: This isn't great but this appears to be how the system works... ideally we could fetch this from the camera itself
	const EAspectRatioAxisConstraint AspectRatioAxisConstraint = GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint;
	FIntRect ViewportRect = FIntRect(FIntPoint(0, 0), FIntPoint(InInitData.RenderTarget->GetSizeXY().X, InInitData.RenderTarget->GetSizeXY().Y));
	FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(InInitData.MinimalViewInfo, AspectRatioAxisConstraint, ViewportRect, /*InOut*/ ViewInitOptions);

	// ToDo: High Res Tiling, Overscan support, letterboxing
	ViewInitOptions.SceneViewStateInterface = InInitData.SceneViewStateReference;
	ViewInitOptions.FOV = ViewFOV;
	ViewInitOptions.DesiredFOV = ViewFOV;

	FSceneView* View = new FSceneView(ViewInitOptions);
	InViewFamilyContext->Views.Add(View);

	//View->ViewLocation = CameraInfo.ViewInfo.Location;
	//View->ViewRotation = CameraInfo.ViewInfo.Rotation;
	// Override previous/current view transforms so that tiled renders don't use the wrong occlusion/motion blur information.
	//View->PreviousViewTransform = CameraInfo.ViewInfo.PreviousViewTransform;

	View->StartFinalPostprocessSettings(View->ViewLocation);
	// BlendPostProcessSettings(View, InOutSampleState, OptPayload);

	// Scaling sensor size inversely with the the projection matrix [0][0] should physically
	// cause the circle of confusion to be unchanged.
	View->FinalPostProcessSettings.DepthOfFieldSensorWidth *= DofSensorScale;
	// Modify the 'center' of the lens to be offset for high-res tiling, helps some effects (vignette) etc. still work.
	// View->LensPrincipalPointOffsetScale = (FVector4f)CalculatePrinciplePointOffsetForTiling(InOutSampleState); // LWC_TODO: precision loss. CalculatePrinciplePointOffsetForTiling() could return float, it's normalized?
	View->EndFinalPostprocessSettings(ViewInitOptions);
	return View;
}


void UMovieGraphDeferredRenderPassNode::FMovieGraphDeferredRenderPass::ApplyMoviePipelineOverridesToViewFamily(TSharedRef<FSceneViewFamilyContext> InOutFamily, const FViewFamilyContextInitData& InInitData)
{
	/*// A third set of overrides required to properly configure views to match the given showflags/etc.
	// ToDo: There's now five(!) identical implementations of this, we should unify them.
	// SetupViewForViewModeOverride(View);

	// Override the view's FrameIndex to be based on our progress through the sequence. This greatly increases
	// determinism with things like TAA.
	InInitData.View->OverrideFrameIndexValue = InInitData.FrameIndex;
	InInitData.View->bCameraCut = InInitData.bCameraCut;
	InInitData.View->bIsOfflineRender = true;
	InInitData.View->AntiAliasingMethod = InInitData.AntiAliasingMethod;

	// Add any view extensions that were added to the scene to this View too
	// OutViewFamily->ViewExtensions.Append(GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(GetWorld()->Scene)));
	// ToDo: Allow render passes to add additional view extensions not added to the scene. (AddViewExtensions(*OutViewFamily, InOutSampleState))

	// Set up the view extensions with this view family
	//for (auto ViewExt : OutViewFamily->ViewExtensions)
	//{
	//	ViewExt->SetupViewFamily(*OutViewFamily.Get());
	//}

	// Set up the view
	//for (int ViewExt = 0; ViewExt < OutViewFamily->ViewExtensions.Num(); ViewExt++)
	//{
	//	OutViewFamily->ViewExtensions[ViewExt]->SetupView(*OutViewFamily.Get(), *View);
	//}

	// Override the Motion Blur settings since these are controlled by the movie pipeline.
	{
		// FFrameRate OutputFrameRate = GetPipeline()->GetPipelinePrimaryConfig()->GetEffectiveFrameRate(GetPipeline()->GetTargetSequence());
		FFrameRate OutputFrameRate = FFrameRate(24, 1); // ToDo, get this from config.

		// We need to inversly scale the target FPS by time dilation to counteract slowmo. If scaling isn't applied then motion blur length
		// stays the same length despite the smaller delta time and the blur ends up too long.
		View->FinalPostProcessSettings.MotionBlurTargetFPS = FMath::RoundToInt(OutputFrameRate.AsDecimal() / FMath::Max(SMALL_NUMBER, InInitData.TimeData.TimeDilation));
		View->FinalPostProcessSettings.MotionBlurAmount = InInitData.TimeData.MotionBlurFraction;
		View->FinalPostProcessSettings.MotionBlurMax = 100.f;
		View->FinalPostProcessSettings.bOverride_MotionBlurAmount = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurTargetFPS = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurMax = true;

		// Skip the whole pass if they don't want motion blur.
		if (FMath::IsNearlyZero(InInitData.TimeData.MotionBlurFraction))
		{
			OutViewFamily->EngineShowFlags.SetMotionBlur(false);
		}
	}

	// Warn the user for invalid setting combinations / enforce hardware limitations
	{

		// Locked Exposure
		const bool bAutoExposureAllowed = IsAutoExposureAllowed(InOutSampleState);
		{
			// If the rendering pass doesn't allow autoexposure and they dont' have manual exposure set up, warn.
			if (!bAutoExposureAllowed && (View->FinalPostProcessSettings.AutoExposureMethod != EAutoExposureMethod::AEM_Manual))
			{
				// Skip warning if the project setting is disabled though, as exposure will be forced off in the renderer anyways.
				const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
				if (RenderSettings->bDefaultFeatureAutoExposure != false)
				{
					UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Camera Auto Exposure Method not supported by one or more render passes. Change the Auto Exposure Method to Manual!"));
					View->FinalPostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
				}
			}
		}

		// Orthographic cameras don't support anti-aliasing outside the path tracer (other than FXAA)
		const bool bIsOrthographicCamera = !View->IsPerspectiveProjection();
		if (bIsOrthographicCamera)
		{
			bool bIsSupportedAAMethod = View->AntiAliasingMethod == EAntiAliasingMethod::AAM_FXAA;
			bool bIsPathTracer = OutViewFamily->EngineShowFlags.PathTracing;
			bool bWarnJitters = InOutSampleState.ProjectionMatrixJitterAmount.SquaredLength() > SMALL_NUMBER;
			if ((!bIsPathTracer && !bIsSupportedAAMethod) || bWarnJitters)
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Orthographic Cameras are only supported with PathTracer or Deferred with FXAA Anti-Aliasing"));
			}
		}

		{
			bool bMethodWasUnsupported = false;
			if (View->AntiAliasingMethod == AAM_TemporalAA && !SupportsGen4TAA(View->GetShaderPlatform()))
			{
				UE_LOG(LogMovieRenderPipeline, Error, TEXT("TAA was requested but this hardware does not support it."));
				bMethodWasUnsupported = true;
			}
			else if (View->AntiAliasingMethod == AAM_TSR && !SupportsTSR(View->GetShaderPlatform()))
			{
				UE_LOG(LogMovieRenderPipeline, Error, TEXT("TSR was requested but this hardware does not support it."));
				bMethodWasUnsupported = true;
			}

			if (bMethodWasUnsupported)
			{
				View->AntiAliasingMethod = AAM_None;
			}
		}
	}


	// Anti Aliasing
	{
		// If we're not using TAA, TSR, or Path Tracing we will apply the View Matrix projection jitter. Normally TAA sets this
		// inside FSceneRenderer::PreVisibilityFrameSetup. Path Tracing does its own anti-aliasing internally.
		bool bApplyProjectionJitter = !bIsOrthographicCamera
			&& !OutViewFamily->EngineShowFlags.PathTracing
			&& !IsTemporalAccumulationBasedMethod(View->AntiAliasingMethod);
		if (bApplyProjectionJitter)
		{
			View->ViewMatrices.HackAddTemporalAAProjectionJitter(InOutSampleState.ProjectionMatrixJitterAmount);
		}
	}

	return OutViewFamily;*/
}

namespace UE::MovieGraph
{
	void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const UE::MovieGraph::FMovieGraphSampleState InSampleState, const UE::MovieGraph::FMovieGraphRenderDataAccumulationArgs& InAccumulationParams)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MoviePipeline_AccumulateSample);
		
		TUniquePtr<FImagePixelData> SamplePixelData = MoveTemp(InPixelData);

		// Associate the sample state with the image as payload data, this allows downstream systems to fetch the values without us having to store the data
		// separately and ensure they stay paired the whole way down.
		TSharedPtr<UE::MovieGraph::FMovieGraphSampleState> SampleStatePayload = MakeShared<UE::MovieGraph::FMovieGraphSampleState>(InSampleState);
		SamplePixelData->SetPayload(StaticCastSharedPtr<IImagePixelDataPayload>(SampleStatePayload));

		TSharedPtr<IMovieGraphOutputMerger, ESPMode::ThreadSafe> AccumulatorPin = InAccumulationParams.OutputMerger.Pin();
		if (!AccumulatorPin.IsValid())
		{
			return;
		}

		const bool bIsWellFormed = SamplePixelData->IsDataWellFormed();
		check(bIsWellFormed);

		// ToDo:
		bool bWriteSampleToDisk = false;
		if (bWriteSampleToDisk)
		{
			// Debug Feature: Write the raw sample to disk for debugging purposes. We copy the data here,
			// as we don't want to disturb the memory flow below.
			TUniquePtr<FImagePixelData> SampleData = SamplePixelData->CopyImageData();
			AccumulatorPin->OnSingleSampleDataAvailable_AnyThread(MoveTemp(SampleData));
		}

		AccumulatorPin->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(SamplePixelData));
	}
}