// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineOutputBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SceneView.h"
#include "LegacyScreenPercentageDriver.h"
#include "MovieRenderPipelineDataTypes.h"
#include "GameFramework/PlayerController.h"
#include "MoviePipelineRenderPass.h"
#include "EngineModule.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget.h"
#include "MoviePipeline.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineShotConfig.h"
#include "MovieRenderOverlappedImage.h"
#include "MovieRenderPipelineCoreModule.h"
#include "ImagePixelData.h"
#include "MoviePipelineOutputBuilder.h"
#include "BufferVisualizationData.h"
#include "Containers/Array.h"
#include "FinalPostProcessSettings.h"
#include "Materials/Material.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineOutputSetting.h"
#include "MovieRenderPipelineCoreModule.h"
#include "SceneViewExtension.h"
#include "OpenColorIODisplayExtension.h"

// For Cine Camera Variables in Metadata
#include "CineCameraActor.h"
#include "CineCameraComponent.h"

DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_AccumulateSample_TT"), STAT_AccumulateSample_TaskThread, STATGROUP_MoviePipeline);

// Forward Declare
namespace MoviePipeline
{
	static void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const MoviePipeline::FImageSampleAccumulationArgs& InParams);
}

void UMoviePipelineImagePassBase::GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const
{
	OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	OutViewModeIndex = EViewModeIndex::VMI_Lit;
}

void UMoviePipelineImagePassBase::RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	Super::RenderSample_GameThreadImpl(InSampleState);
	 
	FMoviePipelineRenderPassMetrics InOutSampleState = InSampleState;
	const FMoviePipelineFrameOutputState::FTimeData& TimeData = InOutSampleState.OutputState.TimeData;

	FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	EViewModeIndex  ViewModeIndex;
	GetViewShowFlags(ShowFlags, ViewModeIndex);
	MoviePipelineRenderShowFlagOverride(ShowFlags);
	FRenderTarget* RenderTarget = TileRenderTarget->GameThread_GetRenderTargetResource();

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		GetPipeline()->GetWorld()->Scene,
		ShowFlags)
		.SetWorldTimes(TimeData.WorldSeconds, TimeData.FrameDeltaTime, TimeData.WorldSeconds)
		.SetRealtimeUpdate(true));

	ViewFamily.SceneCaptureSource = InOutSampleState.SceneCaptureSource;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, IsScreenPercentageSupported() ? InOutSampleState.GlobalScreenPercentageFraction : 1.f, IsScreenPercentageSupported()));
	ViewFamily.bWorldIsPaused = InOutSampleState.bWorldIsPaused;
	ViewFamily.ViewMode = ViewModeIndex;
	EngineShowFlagOverride(ESFIM_Game, ViewFamily.ViewMode, ViewFamily.EngineShowFlags, false);

	// View is added as a child of the ViewFamily. 
	FSceneView* View = GetSceneViewForSampleState(&ViewFamily, /*InOut*/ InOutSampleState);
#if RHI_RAYTRACING
	View->SetupRayTracedRendering();
#endif
	
	SetupViewForViewModeOverride(View);

	// Override the view's FrameIndex to be based on our progress through the sequence. This greatly increases
	// determinism with things like TAA.
	View->OverrideFrameIndexValue = InOutSampleState.FrameIndex;
	View->bCameraCut = InOutSampleState.bCameraCut;
	View->bIsOfflineRender = true;
	View->AntiAliasingMethod = IsAntiAliasingSupported() ? InOutSampleState.AntiAliasingMethod : EAntiAliasingMethod::AAM_None;

	// Override the Motion Blur settings since these are controlled by the movie pipeline.
	{
		FFrameRate OutputFrameRate = GetPipeline()->GetPipelineMasterConfig()->GetEffectiveFrameRate(GetPipeline()->GetTargetSequence());

		// We need to inversly scale the target FPS by time dilation to counteract slowmo. If scaling isn't applied then motion blur length
		// stays the same length despite the smaller delta time and the blur ends up too long.
		View->FinalPostProcessSettings.MotionBlurTargetFPS = FMath::RoundToInt(OutputFrameRate.AsDecimal() / FMath::Max(SMALL_NUMBER, InOutSampleState.OutputState.TimeData.TimeDilation));
		View->FinalPostProcessSettings.MotionBlurAmount = InOutSampleState.OutputState.TimeData.MotionBlurFraction;
		View->FinalPostProcessSettings.MotionBlurMax = 100.f;
		View->FinalPostProcessSettings.bOverride_MotionBlurAmount = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurTargetFPS = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurMax = true;

		// Skip the whole pass if they don't want motion blur.
		if (FMath::IsNearlyZero(InOutSampleState.OutputState.TimeData.MotionBlurFraction))
		{
			ViewFamily.EngineShowFlags.SetMotionBlur(false);
		}
	}

	// Locked Exposure
	{
		if (InOutSampleState.GetTileCount() > 1 && (View->FinalPostProcessSettings.AutoExposureMethod != EAutoExposureMethod::AEM_Manual))
		{
			// Auto exposure is not allowed
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("AutoExposure Method should always be Manual when using tiling!"));
			View->FinalPostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		}
	}

	ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions();

	// OCIO Scene View Extension is a special case and won't be registered like other view extensions.
	if (InSampleState.OCIOConfiguration && InSampleState.OCIOConfiguration->bIsEnabled)
	{
		FOpenColorIODisplayConfiguration* OCIOConfigNew = const_cast<FMoviePipelineRenderPassMetrics&>(InSampleState).OCIOConfiguration;
		FOpenColorIODisplayConfiguration& OCIOConfigCurrent = OCIOSceneViewExtension->GetDisplayConfiguration();

		// We only need to set this once per render sequence.
		if (OCIOConfigNew->ColorConfiguration.ConfigurationSource && OCIOConfigNew->ColorConfiguration.ConfigurationSource != OCIOConfigCurrent.ColorConfiguration.ConfigurationSource)
		{
			OCIOSceneViewExtension->SetDisplayConfiguration(*OCIOConfigNew);
		}

		ViewFamily.ViewExtensions.Add(OCIOSceneViewExtension.ToSharedRef());
	}

	for (auto ViewExt : ViewFamily.ViewExtensions)
	{
		ViewExt->SetupViewFamily(ViewFamily);
	}

	for (int ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ViewExt++)
	{
		ViewFamily.ViewExtensions[ViewExt]->SetupView(ViewFamily, *View);
	}

	// Anti Aliasing
	{
		// If we're not using Temporal Anti-Aliasing we will apply the View Matrix projection jitter. Normally TAA sets this
		// inside FSceneRenderer::PreVisibilityFrameSetup..
		if (View->AntiAliasingMethod != EAntiAliasingMethod::AAM_TemporalAA)
		{
			View->ViewMatrices.HackAddTemporalAAProjectionJitter(InOutSampleState.ProjectionMatrixJitterAmount);
		}
	}

	// Object Occlusion/Histories
	{
		// If we're using tiling, we force the reset of histories each frame so that we don't use the previous tile's
		// object occlusion queries, as that causes things to disappear from some views.
		if (InOutSampleState.GetTileCount() > 1)
		{
			View->bForceCameraVisibilityReset = true;
		}
	}

	// Bias all mip-mapping to pretend to be working at our target resolution and not our tile resolution
	// so that the images don't end up soft.
	{
		float EffectivePrimaryResolutionFraction = 1.f / InOutSampleState.TileCounts.X;
		View->MaterialTextureMipBias = FMath::Log2(EffectivePrimaryResolutionFraction);

		// Add an additional bias per user settings. This allows them to choose to make the textures sharper if it
		// looks better with their particular settings.
		View->MaterialTextureMipBias += InOutSampleState.TextureSharpnessBias;
	}

	// Wait for a surface to be available to write to. This will stall the game thread while the RHI/Render Thread catch up.
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableSurface);
		SurfaceQueue->BlockUntilAnyAvailable();
	}

	FCanvas Canvas = FCanvas(RenderTarget, nullptr, GetPipeline()->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);

	RendererSubmission_GameThread(InOutSampleState, Canvas, ViewFamily);
}

void UMoviePipelineImagePassBase::RendererSubmission_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas, FSceneViewFamilyContext& InViewFamily)
{
	// Draw the world into this View Family
	GetRendererModule().BeginRenderingViewFamily(&InCanvas, &InViewFamily);

	PostRendererSubmission(InSampleState, InCanvas);
}

void UMoviePipelineDeferredPassBase::RendererSubmission_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas, FSceneViewFamilyContext& InViewFamily)
{
	FSceneView* View = const_cast<FSceneView*>(InViewFamily.Views[0]);
	View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Empty();
	View->FinalPostProcessSettings.BufferVisualizationPipes.Empty();

	for (UMaterialInterface* Material : ActivePostProcessMaterials)
	{
		if (Material)
		{
			View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Add(Material);
		}
	}

	for (UMaterialInterface* VisMaterial : View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials)
	{
		// If this was just to contribute to the history buffer, no need to go any further.
		if (InSampleState.bDiscardResult)
		{
			continue;
		}
		FMoviePipelinePassIdentifier LayerPassIdentifier = FMoviePipelinePassIdentifier(PassIdentifier.Name + VisMaterial->GetName());

		auto BufferPipe = MakeShared<FImagePixelPipe, ESPMode::ThreadSafe>();
		BufferPipe->AddEndpoint(MakeForwardingEndpoint(LayerPassIdentifier, InSampleState));
		
		View->FinalPostProcessSettings.BufferVisualizationPipes.Add(VisMaterial->GetFName(), BufferPipe);
	}


	int32 NumValidMaterials = View->FinalPostProcessSettings.BufferVisualizationPipes.Num();
	View->FinalPostProcessSettings.bBufferVisualizationDumpRequired = NumValidMaterials > 0;

	Super::RendererSubmission_GameThread(InSampleState, InCanvas, InViewFamily);
}

void UMoviePipelineDeferredPassBase::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	// Add the default backbuffer
	Super::GatherOutputPassesImpl(ExpectedRenderPasses);

	TArray<FString> RenderPasses;
	for (UMaterialInterface* Material : ActivePostProcessMaterials)
	{
		if (Material)
		{
			RenderPasses.Add(Material->GetName());
		}
	}


	for (const FString& Pass : RenderPasses)
	{
		ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifier.Name + Pass));
	}
}

TFunction<void(TUniquePtr<FImagePixelData>&&)> UMoviePipelineDeferredPassBase::MakeForwardingEndpoint(const FMoviePipelinePassIdentifier InPassIdentifier, const FMoviePipelineRenderPassMetrics& InSampleState)
{
	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, InPassIdentifier);
	}
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = InPassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = GetOutputFileSortingOrder() + 1;

	MoviePipeline::FImageSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.bAccumulateAlpha = bAccumulateMultisampleAlpha;
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		// Transfer the framePayload to the returned data
		TUniquePtr<FImagePixelData> PixelDataWithPayload = nullptr;
		switch (InPixelData->GetType())
		{
		case EImagePixelType::Color:
		{
			TImagePixelData<FColor>* SourceData = static_cast<TImagePixelData<FColor>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FColor>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		case EImagePixelType::Float16:
		{
			TImagePixelData<FFloat16Color>* SourceData = static_cast<TImagePixelData<FFloat16Color>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FFloat16Color>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		case EImagePixelType::Float32:
		{
			TImagePixelData<FLinearColor>* SourceData = static_cast<TImagePixelData<FLinearColor>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FLinearColor>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		default:
			checkNoEntry();
		}

		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();

		FMoviePipelineBackgroundAccumulateTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(PixelDataWithPayload), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});

		this->OutstandingTasks.Add(Event);
	};

	return Callback;
}

void UMoviePipelineDeferredPassBase::PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState, FCanvas& InCanvas)
{
	// If this was just to contribute to the history buffer, no need to go any further.
	if (InSampleState.bDiscardResult)
	{
		return;
	}
	
	// Draw letterboxing
	APlayerCameraManager* PlayerCameraManager = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
	if(PlayerCameraManager && PlayerCameraManager->GetCameraCachePOV().bConstrainAspectRatio)
	{
		const FMinimalViewInfo CameraCache = PlayerCameraManager->GetCameraCachePOV();
		UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
		check(OutputSettings);
		
		const FIntPoint FullOutputSize = OutputSettings->OutputResolution;
		const FIntPoint ConstrainedFullSize = CameraCache.AspectRatio > 1.0f ?
			FIntPoint(FullOutputSize.X, (1.0 / CameraCache.AspectRatio) * FullOutputSize.X) :
			FIntPoint(CameraCache.AspectRatio * FullOutputSize.Y, FullOutputSize.Y);

		const FIntPoint TileViewMin = InSampleState.OverlappedOffset;
		const FIntPoint TileViewMax = TileViewMin + InSampleState.BackbufferSize;

		// Camera ratio constrained rect, clipped by the tile rect
		FIntPoint ConstrainedViewMin = (FullOutputSize - ConstrainedFullSize) / 2;
		FIntPoint ConstrainedViewMax = ConstrainedViewMin + ConstrainedFullSize;
		ConstrainedViewMin = FIntPoint(FMath::Clamp(ConstrainedViewMin.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMin.Y, TileViewMin.Y, TileViewMax.Y));
		ConstrainedViewMax = FIntPoint(FMath::Clamp(ConstrainedViewMax.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMax.Y, TileViewMin.Y, TileViewMax.Y));

		// Difference between the clipped constrained rect and the tile rect
		const FIntPoint OffsetMin = ConstrainedViewMin - TileViewMin;
		const FIntPoint OffsetMax = TileViewMax - ConstrainedViewMax;

		// Clear left
		if (OffsetMin.X > 0)
		{
			InCanvas.DrawTile(0, 0, OffsetMin.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear right
		if (OffsetMax.X > 0)
		{
			InCanvas.DrawTile(InSampleState.BackbufferSize.X - OffsetMax.X, 0, InSampleState.BackbufferSize.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear top
		if (OffsetMin.Y > 0)
		{
			InCanvas.DrawTile(0, 0, InSampleState.BackbufferSize.X, OffsetMin.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear bottom
		if (OffsetMax.Y > 0)
		{
			InCanvas.DrawTile(0, InSampleState.BackbufferSize.Y - OffsetMax.Y, InSampleState.BackbufferSize.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}

		InCanvas.Flush_GameThread(true);
	}

	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, PassIdentifier);
	}
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = PassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = GetOutputFileSortingOrder();

	MoviePipeline::FImageSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.bAccumulateAlpha = bAccumulateMultisampleAlpha;
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();

		FMoviePipelineBackgroundAccumulateTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(InPixelData), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});

		this->OutstandingTasks.Add(Event);
	};

	FRenderTarget* RenderTarget = TileRenderTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[LocalSurfaceQueue, FramePayload, Callback, RenderTarget](FRHICommandListImmediate& RHICmdList) mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			LocalSurfaceQueue->OnRenderTargetReady_RenderThread(RenderTarget->GetRenderTargetTexture(), FramePayload, MoveTemp(Callback));
		});
}

void UMoviePipelineImagePassBase::SetupViewForViewModeOverride(FSceneView* View)
{
	if (View->Family->EngineShowFlags.Wireframe)
	{
		// Wireframe color is emissive-only, and mesh-modifying materials do not use material substitution, hence...
		View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
		View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}
	else if (View->Family->EngineShowFlags.OverrideDiffuseAndSpecular)
	{
		View->DiffuseOverrideParameter = FVector4(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
		View->SpecularOverrideParameter = FVector4(.1f, .1f, .1f, 0.0f);
	}
	else if (View->Family->EngineShowFlags.LightingOnlyOverride)
	{
		View->DiffuseOverrideParameter = FVector4(GEngine->LightingOnlyBrightness.R, GEngine->LightingOnlyBrightness.G, GEngine->LightingOnlyBrightness.B, 0.0f);
		View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}
	else if (View->Family->EngineShowFlags.ReflectionOverride)
	{
		View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
		View->SpecularOverrideParameter = FVector4(1, 1, 1, 0.0f);
		View->NormalOverrideParameter = FVector4(0, 0, 1, 0.0f);
		View->RoughnessOverrideParameter = FVector2D(0.0f, 0.0f);
	}

	if (!View->Family->EngineShowFlags.Diffuse)
	{
		View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}

	if (!View->Family->EngineShowFlags.Specular)
	{
		View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
	}
	FName BufferVisualizationMode = "WorldNormal";
	View->CurrentBufferVisualizationMode = BufferVisualizationMode;
}

void UMoviePipelineDeferredPassBase::MoviePipelineRenderShowFlagOverride(FEngineShowFlags& OutShowFlag)
{
	if (bDisableMultisampleEffects)
	{
		OutShowFlag.SetAntiAliasing(false);
		OutShowFlag.SetDepthOfField(false);
		OutShowFlag.SetMotionBlur(false);
		OutShowFlag.SetBloom(false);
		OutShowFlag.SetSceneColorFringe(false);
	}
}

void UMoviePipelineImagePassBase::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);

	// Allocate 
	ViewState.Allocate();
}

UMoviePipelineDeferredPassBase::UMoviePipelineDeferredPassBase() 
	: UMoviePipelineImagePassBase()
{
	PassIdentifier = FMoviePipelinePassIdentifier("FinalImage");

	// To help user knowledge we pre-seed the additional post processing materials with an array of potentially common passes.
	TArray<FString> DefaultPostProcessMaterials;
	DefaultPostProcessMaterials.Add(TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_WorldDepth.MovieRenderQueue_WorldDepth"));
	DefaultPostProcessMaterials.Add(TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_MotionVectors.MovieRenderQueue_MotionVectors"));

	for (FString& MaterialPath : DefaultPostProcessMaterials)
	{
		FMoviePipelinePostProcessPass& NewPass = AdditionalPostProcessMaterials.AddDefaulted_GetRef();
		NewPass.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		NewPass.bEnabled = false;
	}
}

void UMoviePipelineDeferredPassBase::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);

	// Render Target that the GBuffer is copied to
	TileRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	TileRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// OCIO: Since this is a manually created Render target we don't need Gamma to be applied.
	// We use this render target to render to via a display extension that utilizes Display Gamma
	// which has a default value of 2.2 (DefaultDisplayGamma), therefore we need to set Gamma on this render target to 2.2 to cancel out any unwanted effects.
	TileRenderTarget->TargetGamma = FOpenColorIODisplayExtension::DefaultDisplayGamma;

	// Initialize to the tile size (not final size) and use a 16 bit back buffer to avoid precision issues when accumulating later
	TileRenderTarget->InitCustomFormat(InPassInitSettings.BackbufferResolution.X, InPassInitSettings.BackbufferResolution.Y, EPixelFormat::PF_FloatRGBA, false);

	if (GetPipeline()->GetPreviewTexture() == nullptr)
	{
		GetPipeline()->SetPreviewTexture(TileRenderTarget.Get());
	}

	for (FMoviePipelinePostProcessPass& AdditionalPass : AdditionalPostProcessMaterials)
	{
		if (AdditionalPass.bEnabled)
		{
			UMaterialInterface* Material = AdditionalPass.Material.LoadSynchronous();
			if (Material)
			{
				ActivePostProcessMaterials.Add(Material);
			}
		}
	}

	SurfaceQueue = MakeShared<FMoviePipelineSurfaceQueue>(InPassInitSettings.BackbufferResolution, EPixelFormat::PF_FloatRGBA, 3, true);

	// We must have at least enough accumulators to render all of the requested post process materials, because work doesn't begin
	// until they're actually submitted to the render thread (which happens all at once) but we tie up an accumulator as we get ready to submit.
	// If there aren't enough accumulators then we block until one is free but since submission hasn't gone through they'll never be free.
	int32 PoolSize = (ActivePostProcessMaterials.Num() + 1) * 3;
	AccumulatorPool = MakeShared<TAccumulatorPool<FImageOverlappedAccumulator>, ESPMode::ThreadSafe>(PoolSize);

	// This scene view extension will be released automatically as soon as Render Sequence is torn down.
	// One Extension per sequence, since each sequence has its own OCIO settings.
	OCIOSceneViewExtension = FSceneViewExtensions::NewExtension<FOpenColorIODisplayExtension>();
}

void UMoviePipelineImagePassBase::TeardownImpl()
{
	FSceneViewStateInterface* Ref = ViewState.GetReference();
	if (Ref)
	{
		Ref->ClearMIDPool();
	}
	ViewState.Destroy();

	Super::TeardownImpl();
}

void UMoviePipelineDeferredPassBase::TeardownImpl()
{
	GetPipeline()->SetPreviewTexture(nullptr);

	// This may call FlushRenderingCommands if there are outstanding readbacks that need to happen.
	SurfaceQueue->Shutdown();

	// Stall until the task graph has completed any pending accumulations.
	FTaskGraphInterface::Get().WaitUntilTasksComplete(OutstandingTasks, ENamedThreads::GameThread);
	OutstandingTasks.Reset();

	ActivePostProcessMaterials.Reset();
	
	OCIOSceneViewExtension.Reset();
	OCIOSceneViewExtension = nullptr;

	// Preserve our view state until the rendering thread has been flushed.
	Super::TeardownImpl();
}

void UMoviePipelineImagePassBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UMoviePipelineImagePassBase& This = *CastChecked<UMoviePipelineImagePassBase>(InThis);
	FSceneViewStateInterface* Ref = This.ViewState.GetReference();
	if (Ref)
	{
		Ref->AddReferencedObjects(Collector);
	}
}

void UMoviePipelineImagePassBase::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	Super::GatherOutputPassesImpl(ExpectedRenderPasses);
	ExpectedRenderPasses.Add(PassIdentifier);
}

FSceneView* UMoviePipelineImagePassBase::GetSceneViewForSampleState(FSceneViewFamily* ViewFamily, FMoviePipelineRenderPassMetrics& InOutSampleState)
{
	APlayerController* LocalPlayerController = GetPipeline()->GetWorld()->GetFirstPlayerController();

	int32 TileSizeX = InOutSampleState.BackbufferSize.X;
	int32 TileSizeY = InOutSampleState.BackbufferSize.Y;

	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.ViewOrigin = InOutSampleState.FrameInfo.CurrViewLocation;
	ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), FIntPoint(TileSizeX, TileSizeY)));
	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(InOutSampleState.FrameInfo.CurrViewRotation);

	// Rotate the view 90 degrees (reason: unknown)
	ViewInitOptions.ViewRotationMatrix = ViewInitOptions.ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	float ViewFOV = 90.f;
	if (GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
	{
		ViewFOV = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetFOVAngle();
	}

	float DofSensorScale = 1.0f;

	// Calculate a Projection Matrix
	{
		float XAxisMultiplier;
		float YAxisMultiplier;

		check(GetPipeline()->GetWorld());
		check(GetPipeline()->GetWorld()->GetFirstPlayerController());
		APlayerCameraManager* PlayerCameraManager = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
		
		// Stretch the fovs if the view is constrained to the camera's aspect ratio
		if (PlayerCameraManager && PlayerCameraManager->GetCameraCachePOV().bConstrainAspectRatio)
		{
			const FMinimalViewInfo CameraCache = PlayerCameraManager->GetCameraCachePOV();
			const float DestAspectRatio = ViewInitOptions.GetViewRect().Width() / (float)ViewInitOptions.GetViewRect().Height();

			// If the camera's aspect ratio has a thinner width, then stretch the horizontal fov more than usual to 
			// account for the extra with of (before constraining - after constraining)
			if (CameraCache.AspectRatio < DestAspectRatio)
			{
				const float ConstrainedWidth = ViewInitOptions.GetViewRect().Height() * CameraCache.AspectRatio;
				XAxisMultiplier = ConstrainedWidth / (float)ViewInitOptions.GetViewRect().Width();
				YAxisMultiplier = CameraCache.AspectRatio;
			}
			// Simplified some math here but effectively functions similarly to the above, the unsimplified code would look like:
			// const float ConstrainedHeight = ViewInitOptions.GetViewRect().Width() / CameraCache.AspectRatio;
			// YAxisMultiplier = (ConstrainedHeight / ViewInitOptions.GetViewRect.Height()) * CameraCache.AspectRatio;
			else
			{
				XAxisMultiplier = 1.0f;
				YAxisMultiplier = ViewInitOptions.GetViewRect().Width() / (float)ViewInitOptions.GetViewRect().Height();
			}
		}
		else
		{
			const int32 DestSizeX = ViewInitOptions.GetViewRect().Width();
			const int32 DestSizeY = ViewInitOptions.GetViewRect().Height();
			const EAspectRatioAxisConstraint AspectRatioAxisConstraint = GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint;
			if (((DestSizeX > DestSizeY) && (AspectRatioAxisConstraint == AspectRatio_MajorAxisFOV)) || (AspectRatioAxisConstraint == AspectRatio_MaintainXFOV))
			{
				//if the viewport is wider than it is tall
				XAxisMultiplier = 1.0f;
				YAxisMultiplier = ViewInitOptions.GetViewRect().Width() / (float)ViewInitOptions.GetViewRect().Height();
			}
			else
			{
				//if the viewport is taller than it is wide
				XAxisMultiplier = ViewInitOptions.GetViewRect().Height() / (float)ViewInitOptions.GetViewRect().Width();
				YAxisMultiplier = 1.0f;
			}
		}

		const float MinZ = GNearClippingPlane;
		const float MaxZ = MinZ;
		// Avoid zero ViewFOV's which cause divide by zero's in projection matrix
		const float MatrixFOV = FMath::Max(0.001f, ViewFOV) * (float)PI / 360.0f;

		FMatrix BaseProjMatrix;

		if ((bool)ERHIZBuffer::IsInverted)
		{
			BaseProjMatrix = FReversedZPerspectiveMatrix(
				MatrixFOV,
				MatrixFOV,
				XAxisMultiplier,
				YAxisMultiplier,
				MinZ,
				MaxZ
			);
		}
		else
		{
			BaseProjMatrix = FPerspectiveMatrix(
				MatrixFOV,
				MatrixFOV,
				XAxisMultiplier,
				YAxisMultiplier,
				MinZ,
				MaxZ
			);
		}

		// overlapped tile adjustment
		{
			float PadRatioX = 1.0f;
			float PadRatioY = 1.0f;

			if (InOutSampleState.OverlappedPad.X > 0 && InOutSampleState.OverlappedPad.Y > 0)
			{
				PadRatioX = float(InOutSampleState.OverlappedPad.X * 2 + InOutSampleState.TileSize.X) / float(InOutSampleState.TileSize.X);
				PadRatioY = float(InOutSampleState.OverlappedPad.Y * 2 + InOutSampleState.TileSize.Y) / float(InOutSampleState.TileSize.Y);
			}

			float ScaleX = PadRatioX / float(InOutSampleState.TileCounts.X);
			float ScaleY = PadRatioY / float(InOutSampleState.TileCounts.Y);

			BaseProjMatrix.M[0][0] /= ScaleX;
			BaseProjMatrix.M[1][1] /= ScaleY;
			DofSensorScale = ScaleX;

			// this offset would be correct with no pad
			float OffsetX = -((float(InOutSampleState.TileIndexes.X) + 0.5f - float(InOutSampleState.TileCounts.X) / 2.0f) * 2.0f);
			float OffsetY = ((float(InOutSampleState.TileIndexes.Y) + 0.5f - float(InOutSampleState.TileCounts.Y) / 2.0f) * 2.0f);

			BaseProjMatrix.M[2][0] += OffsetX / PadRatioX;
			BaseProjMatrix.M[2][1] += OffsetY / PadRatioX;
		}

		ViewInitOptions.ProjectionMatrix = BaseProjMatrix;
	}

	ViewInitOptions.SceneViewStateInterface = ViewState.GetReference();
	ViewInitOptions.FOV = ViewFOV;

	FSceneView* View = new FSceneView(ViewInitOptions);
	ViewFamily->Views.Add(View);
	View->ViewLocation = InOutSampleState.FrameInfo.CurrViewLocation;
	View->ViewRotation = InOutSampleState.FrameInfo.CurrViewRotation;
	// Override previous/current view transforms so that tiled renders don't use the wrong occlusion/motion blur information.
	View->PreviousViewTransform = FTransform(InOutSampleState.FrameInfo.PrevViewRotation, InOutSampleState.FrameInfo.PrevViewLocation);

	View->StartFinalPostprocessSettings(View->ViewLocation);
	BlendPostProcessSettings(View);

	// Scaling sensor size inversely with the the projection matrix [0][0] should physically
	// cause the circle of confusion to be unchanged.
	View->FinalPostProcessSettings.DepthOfFieldSensorWidth *= DofSensorScale;

	{
		// We need our final view parameters to be in the space of [-1,1], including all the tiles.
		// Starting with a single tile, the middle of the tile in offset screen space is:
		FVector2D TilePrincipalPointOffset;

		TilePrincipalPointOffset.X = (float(InOutSampleState.TileIndexes.X) + 0.5f - (0.5f * float(InOutSampleState.TileCounts.X))) * 2.0f;
		TilePrincipalPointOffset.Y = (float(InOutSampleState.TileIndexes.Y) + 0.5f - (0.5f * float(InOutSampleState.TileCounts.Y))) * 2.0f;

		// For the tile size ratio, we have to multiply by (1.0 + overlap) and then divide by tile num
		FVector2D OverlapScale;
		OverlapScale.X = (1.0f + float(2 * InOutSampleState.OverlappedPad.X) / float(InOutSampleState.TileSize.X));
		OverlapScale.Y = (1.0f + float(2 * InOutSampleState.OverlappedPad.Y) / float(InOutSampleState.TileSize.Y));

		TilePrincipalPointOffset.X /= OverlapScale.X;
		TilePrincipalPointOffset.Y /= OverlapScale.Y;

		FVector2D TilePrincipalPointScale;
		TilePrincipalPointScale.X = OverlapScale.X / float(InOutSampleState.TileCounts.X);
		TilePrincipalPointScale.Y = OverlapScale.Y / float(InOutSampleState.TileCounts.Y);

		TilePrincipalPointOffset.X *= TilePrincipalPointScale.X;
		TilePrincipalPointOffset.Y *= TilePrincipalPointScale.Y;

		View->LensPrincipalPointOffsetScale = FVector4(TilePrincipalPointOffset.X, -TilePrincipalPointOffset.Y, TilePrincipalPointScale.X, TilePrincipalPointScale.Y);
	}

	View->EndFinalPostprocessSettings(ViewInitOptions);


	// This metadata is per-file and not per-view, but we need the blended result from the view to actually match what we rendered.
	// To solve this, we'll insert metadata per renderpass, separated by render pass name.
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/fstop"), *PassIdentifier.Name), View->FinalPostProcessSettings.DepthOfFieldFstop);
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/fov"), *PassIdentifier.Name), ViewInitOptions.FOV);
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/focalDistance"), *PassIdentifier.Name), View->FinalPostProcessSettings.DepthOfFieldFocalDistance);
	InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorWidth"), *PassIdentifier.Name), View->FinalPostProcessSettings.DepthOfFieldSensorWidth);

	if (GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
	{
		// This only works if you use a Cine Camera (which is almost guranteed with Sequencer) and it's easier (and less human error prone) than re-deriving the information
		ACineCameraActor* CineCameraActor = Cast<ACineCameraActor>(GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetViewTarget());
		if (CineCameraActor)
		{
			UCineCameraComponent* CineCameraComponent = CineCameraActor->GetCineCameraComponent();
			if (CineCameraComponent)
			{
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorWidth"), *PassIdentifier.Name), CineCameraComponent->Filmback.SensorWidth);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorHeight"), *PassIdentifier.Name), CineCameraComponent->Filmback.SensorHeight);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/sensorAspectRatio"), *PassIdentifier.Name), CineCameraComponent->Filmback.SensorAspectRatio);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/minFocalLength"), *PassIdentifier.Name), CineCameraComponent->LensSettings.MinFocalLength);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/maxFocalLength"), *PassIdentifier.Name), CineCameraComponent->LensSettings.MaxFocalLength);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/minFStop"), *PassIdentifier.Name), CineCameraComponent->LensSettings.MinFStop);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/maxFStop"), *PassIdentifier.Name), CineCameraComponent->LensSettings.MaxFStop);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/dofDiaphragmBladeCount"), *PassIdentifier.Name), CineCameraComponent->LensSettings.DiaphragmBladeCount);
				InOutSampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("unreal/camera/%s/focalLength"), *PassIdentifier.Name), CineCameraComponent->CurrentFocalLength);
			}
		}
	}


	return View;
}

void UMoviePipelineImagePassBase::BlendPostProcessSettings(FSceneView* InView)
{
	check(InView);

	APlayerController* LocalPlayerController = GetPipeline()->GetWorld()->GetFirstPlayerController();
	// CameraAnim override
	if (LocalPlayerController->PlayerCameraManager)
	{
		TArray<FPostProcessSettings> const* CameraAnimPPSettings;
		TArray<float> const* CameraAnimPPBlendWeights;
		LocalPlayerController->PlayerCameraManager->GetCachedPostProcessBlends(CameraAnimPPSettings, CameraAnimPPBlendWeights);

		if (LocalPlayerController->PlayerCameraManager->bEnableFading)
		{
			InView->OverlayColor = LocalPlayerController->PlayerCameraManager->FadeColor;
			InView->OverlayColor.A = FMath::Clamp(LocalPlayerController->PlayerCameraManager->FadeAmount, 0.f, 1.f);
		}

		if (LocalPlayerController->PlayerCameraManager->bEnableColorScaling)
		{
			FVector ColorScale = LocalPlayerController->PlayerCameraManager->ColorScale;
			InView->ColorScale = FLinearColor(ColorScale.X, ColorScale.Y, ColorScale.Z);
		}

		FMinimalViewInfo ViewInfo = LocalPlayerController->PlayerCameraManager->GetCameraCachePOV();
		for (int32 PPIdx = 0; PPIdx < CameraAnimPPBlendWeights->Num(); ++PPIdx)
		{
			InView->OverridePostProcessSettings((*CameraAnimPPSettings)[PPIdx], (*CameraAnimPPBlendWeights)[PPIdx]);
		}

		InView->OverridePostProcessSettings(ViewInfo.PostProcessSettings, ViewInfo.PostProcessBlendWeight);
	}
}

namespace MoviePipeline
{
	static void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const MoviePipeline::FImageSampleAccumulationArgs& InParams)
	{
		SCOPE_CYCLE_COUNTER(STAT_AccumulateSample_TaskThread);

		bool bIsWellFormed = InPixelData->IsDataWellFormed();

		if (!bIsWellFormed)
		{
			// figure out why it is not well formed, and print a warning.
			int64 RawSize = InPixelData->GetRawDataSizeInBytes();

			int64 SizeX = InPixelData->GetSize().X;
			int64 SizeY = InPixelData->GetSize().Y;
			int64 ByteDepth = int64(InPixelData->GetBitDepth() / 8);
			int64 NumChannels = int64(InPixelData->GetNumChannels());
			int64 ExpectedTotalSize = SizeX * SizeY * ByteDepth * NumChannels;
			int64 ActualTotalSize = InPixelData->GetRawDataSizeInBytes();

			UE_LOG(LogMovieRenderPipeline, Log, TEXT("AccumulateSample_RenderThread: Data is not well formed."));
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Image dimension: %lldx%lld, %lld, %lld"), SizeX, SizeY, ByteDepth, NumChannels);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Expected size: %lld"), ExpectedTotalSize);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Actual size:   %lld"), ActualTotalSize);
		}

		check(bIsWellFormed);

		FImagePixelDataPayload* FramePayload = InPixelData->GetPayload<FImagePixelDataPayload>();
		check(FramePayload);

		// Writing tiles can be useful for debug reasons. These get passed onto the output every frame.
		if (FramePayload->SampleState.bWriteSampleToDisk)
		{
			// Send the data to the Output Builder. This has to be a copy of the pixel data from the GPU, since
			// it enqueues it onto the game thread and won't be read/sent to write to disk for another frame. 
			// The extra copy is unfortunate, but is only the size of a single sample (ie: 1920x1080 -> 17mb)
			TUniquePtr<FImagePixelData> SampleData = InPixelData->CopyImageData();
			InParams.OutputMerger->OnSingleSampleDataAvailable_AnyThread(MoveTemp(SampleData));
		}

		// Optimization! If we don't need the accumulator (no tiling, no supersampling) then we'll skip it and just send it straight to the output stage.
		// This significantly improves performance in the baseline case.
		const bool bOneTile = FramePayload->IsFirstTile() && FramePayload->IsLastTile();
		const bool bOneTS = FramePayload->IsFirstTemporalSample() && FramePayload->IsLastTemporalSample();
		const bool bOneSS = FramePayload->SampleState.SpatialSampleCount == 1;

		if (bOneTile && bOneTS && bOneSS)
		{
			// Send the data directly to the Output Builder and skip the accumulator.
			InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(InPixelData));
			return;
		}

		// For the first sample in a new output, we allocate memory
		if (FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample())
		{
			int32 ChannelCount = InParams.bAccumulateAlpha ? 4 : 3;
			InParams.ImageAccumulator->InitMemory(FIntPoint(FramePayload->SampleState.TileSize.X * FramePayload->SampleState.TileCounts.X, FramePayload->SampleState.TileSize.Y * FramePayload->SampleState.TileCounts.Y), ChannelCount);
			InParams.ImageAccumulator->ZeroPlanes();
			InParams.ImageAccumulator->AccumulationGamma = FramePayload->SampleState.AccumulationGamma;
		}

		// Accumulate the new sample to our target
		{
			const double AccumulateBeginTime = FPlatformTime::Seconds();

			FIntPoint RawSize = InPixelData->GetSize();

			check(FramePayload->SampleState.TileSize.X + 2 * FramePayload->SampleState.OverlappedPad.X == RawSize.X);
			check(FramePayload->SampleState.TileSize.Y + 2 * FramePayload->SampleState.OverlappedPad.Y == RawSize.Y);

			// bool bSkip = FramePayload->SampleState.TileIndexes.X != 0 || FramePayload->SampleState.TileIndexes.Y != 1;
			// if (!bSkip)
			{
				InParams.ImageAccumulator->AccumulatePixelData(*InPixelData.Get(), FramePayload->SampleState.OverlappedOffset, FramePayload->SampleState.OverlappedSubpixelShift,
					FramePayload->SampleState.WeightFunctionX, FramePayload->SampleState.WeightFunctionY);
			}

			const double AccumulateEndTime = FPlatformTime::Seconds();
			const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime) * 1000.0f);

			UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("Accumulation time: %8.2fms"), ElapsedMs);

		}

		if (FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample())
		{
			int32 FullSizeX = InParams.ImageAccumulator->PlaneSize.X;
			int32 FullSizeY = InParams.ImageAccumulator->PlaneSize.Y;

			// We unfortunately can't share ownership of the payload from the last sample with the combined one so we have to create a new instance.
			TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> NewPayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
			NewPayload->PassIdentifier = FramePayload->PassIdentifier;
			NewPayload->SampleState = FramePayload->SampleState;
			NewPayload->SortingOrder = FramePayload->SortingOrder;

			// Now that a tile is fully built and accumulated we can notify the output builder that the
			// data is ready so it can pass that onto the output containers (if needed).
			if (InPixelData->GetType() == EImagePixelType::Float32)
			{
				// 32 bit FLinearColor
				TUniquePtr<TImagePixelData<FLinearColor> > FinalPixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(FullSizeX, FullSizeY), NewPayload);
				InParams.ImageAccumulator->FetchFinalPixelDataLinearColor(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			else if (InPixelData->GetType() == EImagePixelType::Float16)
			{
				// 32 bit FLinearColor
				TUniquePtr<TImagePixelData<FFloat16Color> > FinalPixelData = MakeUnique<TImagePixelData<FFloat16Color>>(FIntPoint(FullSizeX, FullSizeY), NewPayload);
				InParams.ImageAccumulator->FetchFinalPixelDataHalfFloat(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			else if (InPixelData->GetType() == EImagePixelType::Color)
			{
				// 8bit FColors
				TUniquePtr<TImagePixelData<FColor>> FinalPixelData = MakeUnique<TImagePixelData<FColor>>(FIntPoint(FullSizeX, FullSizeY), NewPayload);
				InParams.ImageAccumulator->FetchFinalPixelDataByte(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			else
			{
				check(0);
			}

			// Free the memory in the accumulator.
			InParams.ImageAccumulator->Reset();
		}
	}
}


TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> FAccumulatorPool::BlockAndGetAccumulator_GameThread(int32 InFrameNumber, const FMoviePipelinePassIdentifier& InPassIdentifier)
{
	FScopeLock ScopeLock(&CriticalSection);

	int32 AvailableIndex = INDEX_NONE;
	while (AvailableIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < Accumulators.Num(); Index++)
		{
			if (InFrameNumber == Accumulators[Index]->ActiveFrameNumber && InPassIdentifier == Accumulators[Index]->ActivePassIdentifier)
			{
				AvailableIndex = Index;
				break;
			}
		}

		if (AvailableIndex == INDEX_NONE)
		{
			// If we don't have an accumulator already working on it let's look for a free one.
			for (int32 Index = 0; Index < Accumulators.Num(); Index++)
			{
				if (!Accumulators[Index]->IsActive())
				{
					// Found a free one, tie it to this output frame.
					Accumulators[Index]->ActiveFrameNumber = InFrameNumber;
					Accumulators[Index]->ActivePassIdentifier = InPassIdentifier;
					Accumulators[Index]->bIsActive = true;
					Accumulators[Index]->TaskPrereq = nullptr;
					AvailableIndex = Index;
					break;
				}
			}
		}
	}

	return Accumulators[AvailableIndex];
}

bool FAccumulatorPool::FAccumulatorInstance::IsActive() const
{
	return bIsActive;
}

void FAccumulatorPool::FAccumulatorInstance::SetIsActive(const bool bInIsActive)
{
	bIsActive = bInIsActive;
}

