// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineHighResSetting.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ShowFlags.h"
#include "SceneView.h"
#include "LegacyScreenPercentageDriver.h"
#include "MovieRenderPipelineDataTypes.h"
#include "GameFramework/PlayerController.h"
#include "MoviePipelineRenderPass.h"
#include "EngineModule.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget.h"
#include "MoviePipeline.h"
#include "Misc/FrameRate.h"
#include "CanvasTypes.h"
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

DECLARE_CYCLE_STAT(TEXT("MoviePipeline_SampleReadback"), STAT_SampleReadback, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("MoviePipeline_SampleReadbackAlloc"), STAT_SampleReadbackAlloc, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("MoviePipeline_SampleReadbackRHI"), STAT_SampleReadbackRHI, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("MoviePipeline_SampleReadbackBroadcast"), STAT_SampleReadbackBroadcast, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("MoviePipeline_AccumulateSample_RT"), STAT_AccumulateSample_RenderThread, STATGROUP_MoviePipeline);
DECLARE_CYCLE_STAT(TEXT("MoviePipeline_OnBackbufferSampleReady_RT"), STAT_OnBackbufferSampleReady_RenderThread, STATGROUP_MoviePipeline);

DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_WaitForAvailableSurface"), STAT_MoviePipeline_WaitForAvailableSurface, STATGROUP_MoviePipeline);

namespace MoviePipeline
{
	// Forward Declare
	void AccumulateSample_RenderThread(TUniquePtr<FImagePixelData>&& InPixelData, TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData, MoviePipeline::FSampleRenderThreadParams& InParams);

	void FDeferredRenderEnginePass::Setup(TWeakObjectPtr<UMoviePipeline> InOwningPipeline, const FMoviePipelineRenderPassInitSettings& InInitSettings)
	{
		FMoviePipelineEnginePass::Setup(InOwningPipeline, InInitSettings);

		TileRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		TileRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		TileRenderTarget->AddToRoot();

		// Initialize to the tile size (not final size) and use a 16 bit back buffer to avoid precision issues when accumulating later
		TileRenderTarget->InitCustomFormat(InInitSettings.BackbufferResolution.X, InInitSettings.BackbufferResolution.Y, EPixelFormat::PF_FloatRGBA, false);

		GetPipeline()->SetPreviewTexture(TileRenderTarget.Get());


		// Allocate 
		ViewState.Allocate();

		SurfaceQueue = MakeShared<FMoviePipelineSurfaceQueue>(InInitSettings.BackbufferResolution, EPixelFormat::PF_FloatRGBA, 3);
	}

	void FDeferredRenderEnginePass::Teardown()
	{
		FMoviePipelineEnginePass::Teardown();

		GetPipeline()->SetPreviewTexture(nullptr);

		// This may call FlushRenderingCommands if there are outstanding readbacks that need to happen.
		SurfaceQueue->Shutdown();

		FSceneViewStateInterface* Ref = ViewState.GetReference();
		if (Ref)
		{
			Ref->ClearMIDPool();
		}
		ViewState.Destroy();

		if (TileRenderTarget.IsValid())
		{
			TileRenderTarget->RemoveFromRoot();
		}
	}
	
	void FDeferredRenderEnginePass::AddReferencedObjects(FReferenceCollector& Collector)
	{
		FSceneViewStateInterface* Ref = ViewState.GetReference();
		if (Ref)
		{
			Ref->AddReferencedObjects(Collector);
		}
	}

	void FDeferredRenderEnginePass::RenderSample_GameThread(const FMoviePipelineRenderPassMetrics& InSampleState)
	{
		FMoviePipelineEnginePass::RenderSample_GameThread(InSampleState);

		const FMoviePipelineFrameOutputState::FTimeData& TimeData = InSampleState.OutputState.TimeData;

		FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);

		static bool TestDisableDofAndMotionBlur = false;
		if (TestDisableDofAndMotionBlur)
		{
			// Disable these for now
			ShowFlags.SetDepthOfField(false);
			ShowFlags.SetMotionBlur(false);
		}

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			TileRenderTarget->GameThread_GetRenderTargetResource(),
			GetPipeline()->GetWorld()->Scene,
			ShowFlags)
			.SetWorldTimes(TimeData.WorldSeconds, TimeData.FrameDeltaTime, TimeData.WorldSeconds)
			.SetRealtimeUpdate(true));

		ViewFamily.SceneCaptureSource = InSampleState.SceneCaptureSource;
		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, InSampleState.GlobalScreenPercentageFraction, true));
		ViewFamily.bWorldIsPaused = InSampleState.bWorldIsPaused;

		// View is added as a child of the ViewFamily
		FSceneView* View = CalcSceneView(&ViewFamily, InSampleState);

		// Override the view's FrameIndex to be based on our progress through the sequence. This greatly increases
		// determinism with things like TAA.
		View->OverrideFrameIndexValue = InSampleState.FrameIndex;
		View->bCameraCut = InSampleState.bCameraCut;
		View->bIsOfflineRender = true;
		View->AntiAliasingMethod = InSampleState.AntiAliasingMethod;

		// Override the Motion Blur settings since these are controlled by the movie pipeline.
		{
			FFrameRate OutputFrameRate = GetPipeline()->GetPipelineMasterConfig()->GetEffectiveFrameRate(GetPipeline()->GetTargetSequence());
			View->FinalPostProcessSettings.MotionBlurTargetFPS = FMath::RoundToInt(OutputFrameRate.AsDecimal());
			View->FinalPostProcessSettings.MotionBlurAmount = InSampleState.OutputState.TimeData.MotionBlurFraction;
			View->FinalPostProcessSettings.MotionBlurMax = 100.f;
			View->FinalPostProcessSettings.bOverride_MotionBlurAmount = true;
			View->FinalPostProcessSettings.bOverride_MotionBlurTargetFPS = true;
			View->FinalPostProcessSettings.bOverride_MotionBlurMax = true;
		}

		// Locked Exposure
		{
			if (InSampleState.ExposureCompensation.IsSet())
			{
				View->FinalPostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
				View->FinalPostProcessSettings.AutoExposureBias = InSampleState.ExposureCompensation.GetValue();
			}
			else if (InSampleState.GetTileCount() > 1 && (View->FinalPostProcessSettings.AutoExposureMethod != EAutoExposureMethod::AEM_Manual))
			{
				// Auto exposure is not allowed
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("AutoExposure Method should always be Manual when using tiling!"));
				View->FinalPostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
			}
		}
		
		// Anti Aliasing
		{
			// If we're not using Temporal Anti-Aliasing we will apply the View Matrix projection jitter. Normally TAA sets this
			// inside FSceneRenderer::PreVisibilityFrameSetup..
			if(View->AntiAliasingMethod != EAntiAliasingMethod::AAM_TemporalAA)
			{
				View->ViewMatrices.HackAddTemporalAAProjectionJitter(InSampleState.ProjectionMatrixJitterAmount);
			}
		}
		
		// Object Occlusion/Histories
		{
			// If we're using tiling, we force the reset of histories each frame so that we don't use the previous tile's
			// object occlusion queries, as that causes things to disappear from some views.
			if(InSampleState.GetTileCount() > 1)
			{
				View->bForceCameraVisibilityReset = true;
			}
		}

		// Bias all mip-mapping to pretend to be working at our target resolution and not our tile resolution
		// so that the images don't end up soft.
		{
			float EffectivePrimaryResolutionFraction = 1.f / InSampleState.TileCounts.X;
			View->MaterialTextureMipBias = FMath::Log2(EffectivePrimaryResolutionFraction);

			// Add an additional bias per user settings. This allows them to choose to make the textures sharper if it
			// looks better with their particular settings.
			View->MaterialTextureMipBias += InSampleState.TextureSharpnessBias;
		}

		// Wait for a surface to be available to write to. This will stall the game thread while the RHI/Render Thread catch up.
		{
			SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableSurface);
			SurfaceQueue->BlockUntilAnyAvailable();
		}

		FCanvas Canvas = FCanvas(TileRenderTarget->GameThread_GetRenderTargetResource(), nullptr, GetPipeline()->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);
		// SetupViewDelegate.Broadcast(ViewFamily, *View, InSampleState);

		// Draw the world into this View Family
		GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

		// If this was just to contribute to the history buffer, no need to go any further.
		if (InSampleState.bDiscardResult)
		{
			return;
		}
		
		TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

		FRenderTarget* BackbufferRenderTarget = TileRenderTarget->GameThread_GetRenderTargetResource();
		FMoviePipelineSampleReady& OnBackbufferReadDelegate = BackbufferReadyDelegate;

		ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
			[LocalSurfaceQueue, InSampleState, BackbufferRenderTarget, OnBackbufferReadDelegate](FRHICommandListImmediate& RHICmdList)
		{
				// If they are using tiles, we'll output some more fine-grained progress via logging since it freezes the UI for such a long time.
				if (InSampleState.GetTileCount() > 1)
				{
					UE_LOG(LogMovieRenderPipeline, Log, TEXT("Frame: %d TemporalSample: %d/%d Rendered Tile: %d/%d Sample: %d/%d"),
						InSampleState.OutputState.OutputFrameNumber, InSampleState.TemporalSampleIndex + 1, InSampleState.TemporalSampleCount,
						InSampleState.GetTileIndex() + 1, InSampleState.GetTileCount(), InSampleState.SpatialSampleIndex + 1, InSampleState.SpatialSampleCount);
				}

				LocalSurfaceQueue->OnRenderTargetReady_RenderThread(BackbufferRenderTarget->GetRenderTargetTexture(), InSampleState, OnBackbufferReadDelegate);
		});
	}

	FSceneView* FDeferredRenderEnginePass::CalcSceneView(FSceneViewFamily* ViewFamily, const FMoviePipelineRenderPassMetrics& InSampleState)
	{
		APlayerController* LocalPlayerController = GetPipeline()->GetWorld()->GetFirstPlayerController();

		int32 TileSizeX = InitSettings.BackbufferResolution.X;
		int32 TileSizeY = InitSettings.BackbufferResolution.Y;

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.ViewFamily = ViewFamily;
		ViewInitOptions.ViewOrigin = InSampleState.FrameInfo.CurrViewLocation;
		ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), FIntPoint(TileSizeX, TileSizeY)));
		ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(InSampleState.FrameInfo.CurrViewRotation);

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

			if (ViewInitOptions.GetViewRect().Width() > ViewInitOptions.GetViewRect().Height())
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

				if (InSampleState.OverlappedPad.X > 0 && InSampleState.OverlappedPad.Y > 0)
				{
					PadRatioX = float(InSampleState.OverlappedPad.X * 2 + InSampleState.TileSize.X) / float(InSampleState.TileSize.X);
					PadRatioY = float(InSampleState.OverlappedPad.Y * 2 + InSampleState.TileSize.Y) / float(InSampleState.TileSize.Y);
				}

				float ScaleX = PadRatioX / float(InitSettings.TileCount.X);
				float ScaleY = PadRatioY / float(InitSettings.TileCount.Y);

				BaseProjMatrix.M[0][0] /= ScaleX;
				BaseProjMatrix.M[1][1] /= ScaleY;
				DofSensorScale = ScaleX;

				// this offset would be correct with no pad
				float OffsetX = -((float(InSampleState.TileIndexes.X) + 0.5f - float(InitSettings.TileCount.X) / 2.0f)* 2.0f);
				float OffsetY = ((float(InSampleState.TileIndexes.Y) + 0.5f - float(InitSettings.TileCount.Y) / 2.0f)* 2.0f);

				BaseProjMatrix.M[2][0] += OffsetX / PadRatioX;
				BaseProjMatrix.M[2][1] += OffsetY / PadRatioX;
			}

			ViewInitOptions.ProjectionMatrix = BaseProjMatrix;
		}

		ViewInitOptions.SceneViewStateInterface = ViewState.GetReference();
		ViewInitOptions.FOV = ViewFOV;

		FSceneView* View = new FSceneView(ViewInitOptions);
		ViewFamily->Views.Add(View);
		View->ViewLocation = InSampleState.FrameInfo.CurrViewLocation;
		View->ViewRotation = InSampleState.FrameInfo.CurrViewRotation;
		// Override previous/current view transforms so that tiled renders don't use the wrong occlusion/motion blur information.
		View->PreviousViewTransform = FTransform(InSampleState.FrameInfo.PrevViewRotation, InSampleState.FrameInfo.PrevViewLocation);

		View->StartFinalPostprocessSettings(View->ViewLocation);

		// CameraAnim override
		if (LocalPlayerController->PlayerCameraManager)
		{
			TArray<FPostProcessSettings> const* CameraAnimPPSettings;
			TArray<float> const* CameraAnimPPBlendWeights;
			LocalPlayerController->PlayerCameraManager->GetCachedPostProcessBlends(CameraAnimPPSettings, CameraAnimPPBlendWeights);

			if (LocalPlayerController->PlayerCameraManager->bEnableFading)
			{
				View->OverlayColor = LocalPlayerController->PlayerCameraManager->FadeColor;
				View->OverlayColor.A = FMath::Clamp(LocalPlayerController->PlayerCameraManager->FadeAmount, 0.f, 1.f);
			}

			if (LocalPlayerController->PlayerCameraManager->bEnableColorScaling)
			{
				FVector ColorScale = LocalPlayerController->PlayerCameraManager->ColorScale;
				View->ColorScale = FLinearColor(ColorScale.X, ColorScale.Y, ColorScale.Z);
			}

			FMinimalViewInfo ViewInfo = LocalPlayerController->PlayerCameraManager->GetCameraCachePOV();
			for (int32 PPIdx = 0; PPIdx < CameraAnimPPBlendWeights->Num(); ++PPIdx)
			{
				View->OverridePostProcessSettings((*CameraAnimPPSettings)[PPIdx], (*CameraAnimPPBlendWeights)[PPIdx]);
			}

			View->OverridePostProcessSettings(ViewInfo.PostProcessSettings, ViewInfo.PostProcessBlendWeight);

		}

		// Scaling sensor size inversely with the the projection matrix [0][0] should physically
		// cause the circle of confusion to be unchanged.
		View->FinalPostProcessSettings.DepthOfFieldSensorWidth *= DofSensorScale;

		{
			// We need our final view parameters to be in the space of [-1,1], including all the tiles.
			// Starting with a single tile, the middle of the tile in offset screen space is:
			FVector2D TilePrincipalPointOffset;

			TilePrincipalPointOffset.X = (float(InSampleState.TileIndexes.X) + 0.5f - (0.5f * float(InSampleState.TileCounts.X))) * 2.0f;
			TilePrincipalPointOffset.Y = (float(InSampleState.TileIndexes.Y) + 0.5f - (0.5f * float(InSampleState.TileCounts.Y))) * 2.0f;

			// For the tile size ratio, we have to multiply by (1.0 + overlap) and then divide by tile num
			FVector2D OverlapScale;
			OverlapScale.X = (1.0f + float(2 * InSampleState.OverlappedPad.X) / float(InSampleState.TileSize.X) );
			OverlapScale.Y = (1.0f + float(2 * InSampleState.OverlappedPad.Y) / float(InSampleState.TileSize.Y) );

			TilePrincipalPointOffset.X /= OverlapScale.X;
			TilePrincipalPointOffset.Y /= OverlapScale.Y;

			FVector2D TilePrincipalPointScale;
			TilePrincipalPointScale.X = OverlapScale.X / float(InSampleState.TileCounts.X);
			TilePrincipalPointScale.Y = OverlapScale.Y / float(InSampleState.TileCounts.Y);

			TilePrincipalPointOffset.X *= TilePrincipalPointScale.X;
			TilePrincipalPointOffset.Y *= TilePrincipalPointScale.Y;

			View->LensPrincipalPointOffsetScale = FVector4(TilePrincipalPointOffset.X, -TilePrincipalPointOffset.Y, TilePrincipalPointScale.X, TilePrincipalPointScale.Y);
		}

		View->EndFinalPostprocessSettings(ViewInitOptions);

		return View;
	}
}


void UMoviePipelineDeferredPassBase::GetRequiredEnginePassesImpl(TSet<FMoviePipelinePassIdentifier>& RequiredEnginePasses)
{
	Super::GetRequiredEnginePassesImpl(RequiredEnginePasses);

	RequiredEnginePasses.Add(FMoviePipelinePassIdentifier(TEXT("MainDeferredPass")));
}

void UMoviePipelineDeferredPassBase::SetupImpl(TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>>& InEnginePasses, const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InEnginePasses, InPassInitSettings);

	DesiredOutputPasses.Empty();

	// Look for the Deferred Engine Pass
	const FMoviePipelinePassIdentifier DesiredPass = FMoviePipelinePassIdentifier(TEXT("MainDeferredPass"));
	for (const TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>& Pass : InEnginePasses)
	{
		if (Pass->PassIdentifier == DesiredPass)
		{
			// Register our callbacks here. We need access to the Backbuffer and the Composition Graph Pipes.
			TSharedPtr<MoviePipeline::FDeferredRenderEnginePass> DeferredPass = StaticCastSharedPtr<MoviePipeline::FDeferredRenderEnginePass>(Pass);
			DeferredPass->BackbufferReadyDelegate.AddUObject(this, &UMoviePipelineDeferredPassBase::OnBackbufferSampleReady);
			DeferredPass->SetupViewDelegate.AddUObject(this, &UMoviePipelineDeferredPassBase::OnSetupView);
			bAccumulateAlpha = DeferredPass->GetInitSettings().bAccumulateAlpha;
			break;
		}
	}

	DesiredOutputPasses.Add(TEXT("Backbuffer"));
	// DesiredOutputPasses.Add(TEXT("SceneDepth"));
	// DesiredOutputPasses.Add(TEXT("AmbientOcclusion"));

	for (const FString& Pass : DesiredOutputPasses)
	{
		// Create an allocator for each expected output pass.
		ImageTileAccumulator.Add(MakeShared<FImageOverlappedAccumulator, ESPMode::ThreadSafe>());
	}
}

void UMoviePipelineDeferredPassBase::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	Super::GatherOutputPassesImpl(ExpectedRenderPasses);

	// We will produce the following output files.
	for (const FString& Pass : DesiredOutputPasses)
	{
		ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(Pass));
	}
}

void UMoviePipelineDeferredPassBase::OnBackbufferSampleReady(TArray<FFloat16Color>& InPixelData, FMoviePipelineRenderPassMetrics InSampleState)
{
	SCOPE_CYCLE_COUNTER(STAT_OnBackbufferSampleReady_RenderThread);

	MoviePipeline::FSampleRenderThreadParams AccumulationParams;
	{
		AccumulationParams.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationParams.ImageAccumulator = ImageTileAccumulator[0];
		AccumulationParams.SampleState = InSampleState;
		AccumulationParams.bAccumulateAlpha = bAccumulateAlpha;
	}

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FrameData = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FrameData->OutputState = InSampleState.OutputState;
	FrameData->PassIdentifier = FMoviePipelinePassIdentifier(TEXT("Backbuffer"));
	FrameData->SampleState = InSampleState;

	TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(InSampleState.BackbufferSize, TArray64<FFloat16Color>(MoveTemp(InPixelData)), FrameData);

	AccumulateSample_RenderThread(MoveTemp(PixelData), FrameData, AccumulationParams);
}

void UMoviePipelineDeferredPassBase::OnSetupView(FSceneViewFamily& InViewFamily, FSceneView& InView, const FMoviePipelineRenderPassMetrics& InSampleState)
{
	struct FIterator
	{
		FFinalPostProcessSettings& FinalPostProcessSettings;
		const TArray<FString>& RenderPasses;

		FIterator(FFinalPostProcessSettings& InFinalPostProcessSettings, const TArray<FString>& InRenderPasses)
			: FinalPostProcessSettings(InFinalPostProcessSettings), RenderPasses(InRenderPasses)
		{}

		void ProcessValue(const FString& InName, UMaterialInterface* Material, const FText& InText)
		{
			if (!RenderPasses.Num() || RenderPasses.Contains(InName) || RenderPasses.Contains(InText.ToString()))
			{
				FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Add(Material);
			}
		}
	} Iterator(InView.FinalPostProcessSettings, DesiredOutputPasses);
	GetBufferVisualizationData().IterateOverAvailableMaterials(Iterator);

	for (int32 PassIndex = 0; PassIndex < InView.FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Num(); PassIndex++)
	{
		const UMaterialInterface* Pass = InView.FinalPostProcessSettings.BufferVisualizationOverviewMaterials[PassIndex];
		UE_LOG(LogTemp, Log, TEXT("Pass: %s"), *Pass->GetName());

		FName PassName = FName(*Pass->GetName());
		TSharedPtr<FImagePixelPipe, ESPMode::ThreadSafe>* Pipe = InView.FinalPostProcessSettings.BufferVisualizationPipes.Find(PassName);
		if (!Pipe)
		{
			Pipe = &InView.FinalPostProcessSettings.BufferVisualizationPipes.Add(PassName, MakeShared<FImagePixelPipe, ESPMode::ThreadSafe>());
		}

		MoviePipeline::FSampleRenderThreadParams AccumulationParams;
		{
			AccumulationParams.OutputMerger = GetPipeline()->OutputBuilder;
			AccumulationParams.ImageAccumulator = ImageTileAccumulator[PassIndex + 1]; // 0 is reserved for backbuffer.
			AccumulationParams.SampleState = InSampleState;
			AccumulationParams.bAccumulateAlpha = bAccumulateAlpha;
		}

		auto OnImageReceived = [PassName, AccumulationParams](TUniquePtr<FImagePixelData>&& InPixelData) mutable
		{
			UE_LOG(LogTemp, Log, TEXT("Recieved Data for Pass: %d Size: %dx%d BitDepth: %d"), *PassName.ToString(),
				InPixelData->GetSize().X, InPixelData->GetSize().Y, InPixelData->GetBitDepth());

			// Don't try to accumulate samples rendered for the sake of setting up history.
			if (AccumulationParams.SampleState.bDiscardResult)
			{
				return;
			}

			TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FrameData = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
			FrameData->OutputState = AccumulationParams.SampleState.OutputState;
			FrameData->PassIdentifier = FMoviePipelinePassIdentifier(PassName.ToString());
			FrameData->SampleState = AccumulationParams.SampleState;
			MoviePipeline::AccumulateSample_RenderThread(MoveTemp(InPixelData), FrameData, AccumulationParams);
		};


		(*Pipe)->AddEndpoint(OnImageReceived);
	}


	// if (PostProcessingMaterial)
	// {
	// 	FWeightedBlendable Blendable(1.f, PostProcessingMaterial);
	// 	PostProcessingMaterial->OverrideBlendableSettings(InView, 1.f);
	// }
}

namespace MoviePipeline
{
	void AccumulateSample_RenderThread(TUniquePtr<FImagePixelData>&& InPixelData, TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData, MoviePipeline::FSampleRenderThreadParams& InParams)
	{
		SCOPE_CYCLE_COUNTER(STAT_AccumulateSample_RenderThread);

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

		// Writing tiles can be useful for debug reasons. These get passed onto the output every frame.
		if (InParams.SampleState.bWriteSampleToDisk)
		{
			// Send the data to the Output Builder. This has to be a copy of the pixel data from the GPU, since
			// it enqueues it onto the game thread and won't be read/sent to write to disk for another frame. 
			// The extra copy is unfortunate, but is only the size of a single sample (ie: 1920x1080 -> 17mb)
			TUniquePtr<FImagePixelData> SampleData = InPixelData->CopyImageData();
			InParams.OutputMerger->OnSingleSampleDataAvailable_AnyThread(MoveTemp(SampleData), InFrameData);
		}

		// Optimization! If we don't need the accumulator (no tiling, no supersampling) then we'll skip it and just send it straight to the output stage.
		// This significantly improves performance in the baseline case.
		const bool bOneTile = InFrameData->IsFirstTile() && InFrameData->IsLastTile();
		const bool bOneTS = InFrameData->IsFirstTemporalSample() && InFrameData->IsLastTemporalSample();
		const bool bOneSS = InFrameData->SampleState.SpatialSampleCount == 1;

		if (bOneTile && bOneTS && bOneSS)
		{
			// Send the data directly to the Output Builder and skip the accumulator.
			InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(InPixelData), InFrameData);
			return;
		}

		// For the first sample in a new output, we allocate memory
		if (InFrameData->IsFirstTile() && InFrameData->IsFirstTemporalSample())
		{
			int32 ChannelCount = InParams.bAccumulateAlpha ? 4 : 3;
			InParams.ImageAccumulator->InitMemory(FIntPoint(InFrameData->SampleState.TileSize.X * InFrameData->SampleState.TileCounts.X, InFrameData->SampleState.TileSize.Y * InFrameData->SampleState.TileCounts.Y), ChannelCount);
			InParams.ImageAccumulator->ZeroPlanes();
			InParams.ImageAccumulator->AccumulationGamma = InFrameData->SampleState.AccumulationGamma;
		}

		// Accumulate the new sample to our target
		{
			const double AccumulateBeginTime = FPlatformTime::Seconds();

			FIntPoint RawSize = InPixelData->GetSize();

			check(InFrameData->SampleState.TileSize.X + 2 * InFrameData->SampleState.OverlappedPad.X == RawSize.X);
			check(InFrameData->SampleState.TileSize.Y + 2 * InFrameData->SampleState.OverlappedPad.Y == RawSize.Y);

			// bool bSkip = InFrameData->SampleState.TileIndexes.X != 0 || InFrameData->SampleState.TileIndexes.Y != 1;
			// if (!bSkip)
			{
				InParams.ImageAccumulator->AccumulatePixelData(*InPixelData.Get(), InFrameData->SampleState.OverlappedOffset, InFrameData->SampleState.OverlappedSubpixelShift,
					InFrameData->SampleState.WeightFunctionX, InFrameData->SampleState.WeightFunctionY);
			}

			const double AccumulateEndTime = FPlatformTime::Seconds();
			const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime)*1000.0f);

			UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("Accumulation time: %8.2fms"), ElapsedMs);

		}

		if (InFrameData->IsLastTile() && InFrameData->IsLastTemporalSample())
		{
			int32 FullSizeX = InParams.ImageAccumulator->PlaneSize.X;
			int32 FullSizeY = InParams.ImageAccumulator->PlaneSize.Y;

			// Now that a tile is fully built and accumulated we can notify the output builder that the
			// data is ready so it can pass that onto the output containers (if needed).
			if (InPixelData->GetType() == EImagePixelType::Float32)
			{
				// 32 bit FLinearColor
				TUniquePtr<TImagePixelData<FLinearColor> > FinalPixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(FullSizeX, FullSizeY), InFrameData);
				InParams.ImageAccumulator->FetchFinalPixelDataLinearColor(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData), InFrameData);
			}
			else if (InPixelData->GetType() == EImagePixelType::Float16)
			{
				// 32 bit FLinearColor
				TUniquePtr<TImagePixelData<FFloat16Color> > FinalPixelData = MakeUnique<TImagePixelData<FFloat16Color>>(FIntPoint(FullSizeX, FullSizeY), InFrameData);
				InParams.ImageAccumulator->FetchFinalPixelDataHalfFloat(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData), InFrameData);
			}
			else if (InPixelData->GetType() == EImagePixelType::Color)
			{
				// 8bit FColors
				TUniquePtr<TImagePixelData<FColor>> FinalPixelData = MakeUnique<TImagePixelData<FColor>>(FIntPoint(FullSizeX, FullSizeY), InFrameData);
				InParams.ImageAccumulator->FetchFinalPixelDataByte(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData), InFrameData);
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
