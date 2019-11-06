// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineBackbufferPass.h"
#include "Slate/SceneViewport.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "MovieRenderPipelineDataTypes.h"
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SceneView.h"
#include "LegacyScreenPercentageDriver.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "ImagePixelData.h"
#include "RHICommandList.h"
#include "ImageWriteStream.h"
#include "GameFramework/PlayerController.h"
#include "Math/Vector.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineAccumulationSetting.h"
#include "MoviePipelineRenderPass.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineSetting.h"
#include "MoviePipeline.h"


void UMoviePipelineBackbufferPass::SetupImpl(const FMoviePipelineRenderPassInitSettings& InInitSettings, TSharedRef<FImagePixelPipe, ESPMode::ThreadSafe> InOutputPipe)
{
	OutputPipe = InOutputPipe;

	TileRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	TileRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Initialize to the tile size (not final size) and use a 16 bit back buffer to avoid precision issues
			// when accumulating later.

	int32 TargetSizeX = InInitSettings.TileResolution.X;
	int32 TargetSizeY = InInitSettings.TileResolution.Y;
/*
	*/
	TileRenderTarget->InitCustomFormat(TargetSizeX, TargetSizeY, EPixelFormat::PF_A16B16G16R16, false);

	// Create a Render Target to hold our back buffer.
	ViewState.Allocate();
}


void UMoviePipelineBackbufferPass::TeardownImpl()
{
	ViewState.Destroy();
}

void UMoviePipelineBackbufferPass::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	ExpectedRenderPasses.Add(FName("Backbuffer"));
}


void UMoviePipelineBackbufferPass::CaptureFrameImpl(const FMoviePipelineRenderPassMetrics& InPassMetrics)
{
	float TimeSeconds = InPassMetrics.OutputState.WorldSeconds;
	float RealTimeSeconds = InPassMetrics.OutputState.WorldSeconds;
	float DeltaTimeSeconds = InPassMetrics.OutputState.FrameDeltaTime;

	// Reuse the common RenderTarget
	UTextureRenderTarget2D* RenderTarget = TileRenderTarget;

	FEngineShowFlags ShowFlags(EShowFlagInitMode::ESFIM_Game);
	if (InPassMetrics.bIsUsingOverlappedTiles)
	{
		// Disable these for now
		//ShowFlags.SetDepthOfField(false);
		ShowFlags.SetMotionBlur(false);
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget->GameThread_GetRenderTargetResource(),
		GetWorld()->Scene,
		ShowFlags)
		.SetWorldTimes(TimeSeconds, DeltaTimeSeconds, RealTimeSeconds)
		.SetRealtimeUpdate(true));

	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.0f, false));
	// View is added as a child of the ViewFamily
	FSceneView* View = CalcSceneView(&ViewFamily, InPassMetrics);
	ViewFamily.FrameNumber += InPassMetrics.SpatialJitterIndex;
	
	// Override the Motion Blur settings since these are controlled by the movie pipeline.
	{
		View->FinalPostProcessSettings.MotionBlurTargetFPS = FMath::RoundToInt(GetPipeline()->GetEffectiveFrameRate().AsDecimal());
		View->FinalPostProcessSettings.MotionBlurAmount = InPassMetrics.OutputState.MotionBlurFraction;
		View->FinalPostProcessSettings.MotionBlurMax = 100.f;
		View->FinalPostProcessSettings.bOverride_MotionBlurAmount = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurTargetFPS = true;
		View->FinalPostProcessSettings.bOverride_MotionBlurMax = true;
	}

	// Disable TAA while we're working on pixel accuracy testing
	{
		View->AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
	}

	// Override the view matrix with our offsets provided by tiling/jittering. This requires
	// TAA to be disabled.
	{
		View->ViewMatrices.HackAddTemporalAAProjectionJitter(InPassMetrics.SpatialShift);
	}

	// Bias all mip-mapping to pretend to be working at our target resolution and not our tile resolution
	// so that the images don't end up soft.
	{
		float EffectivePrimaryResolutionFraction = 1.f / InitSettings.TileCount;
		View->MaterialTextureMipBias = FMath::Log2(EffectivePrimaryResolutionFraction);

		// Add an additional bias per user settings. This allows them to choose to make the textures sharper if it
		// looks better with their particular settings.
		UMoviePipelineAccumulationSetting* AccumulationSettings = InitSettings.ShotConfig->FindOrAddSetting<UMoviePipelineAccumulationSetting>();
		View->MaterialTextureMipBias += AccumulationSettings->TextureSharpnessBias;
	}

	// If this is a history only frame it means we just came from a camera cut, flag it as such
	// which will have knock-on effects in histories (such as clearing old TAA buffers).
	if (InPassMetrics.bIsHistoryOnlyFrame)
	{
		View->bCameraCut = true;
	}
	
	// Pause the view family on renders after the first one. This prevents motion blur/view matrices from being
	// updated
	ViewFamily.bWorldIsPaused = InPassMetrics.SpatialJitterIndex != InPassMetrics.NumSpatialJitters - 1;

	FCanvas Canvas = FCanvas(RenderTarget->GameThread_GetRenderTargetResource(), nullptr, GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);


	// Draw the world into this View Family
	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

	FRenderTarget* CanvasRenderTarget = Canvas.GetRenderTarget();
	TSharedPtr<FImagePixelPipe, ESPMode::ThreadSafe> LocalOutputPipe = OutputPipe;
	
	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[CanvasRenderTarget, InPassMetrics, LocalOutputPipe](FRHICommandListImmediate& RHICmdList)
	{
		// If they only needed this frame for the history, we don't actually care about trying to output it,
		// so we can skip the copy and pushing to output.
		if (InPassMetrics.bIsHistoryOnlyFrame)
		{
			return;
		}

		FReadSurfaceDataFlags ReadDataFlags;
		ReadDataFlags.SetLinearToGamma(false);

		FIntRect SourceRect = FIntRect(0, 0, CanvasRenderTarget->GetSizeXY().X, CanvasRenderTarget->GetSizeXY().Y);

		// Build our metadata to associate with this frame.
		TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FrameData = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
		FrameData->OutputState = InPassMetrics.OutputState;
		FrameData->PassName = TEXT("Backbuffer");
		FrameData->TileIndexX = InPassMetrics.TileIndexX;
		FrameData->TileIndexY = InPassMetrics.TileIndexY;
		FrameData->TileSizeX = SourceRect.Width();
		FrameData->TileSizeY = SourceRect.Height();
		FrameData->NumTilesX = InPassMetrics.NumTilesX;
		FrameData->NumTilesY = InPassMetrics.NumTilesY;
		FrameData->SpatialShift = InPassMetrics.SpatialShift;
		FrameData->SpatialJitterIndex = InPassMetrics.SpatialJitterIndex;
		FrameData->NumSpatialJitters = InPassMetrics.NumSpatialJitters;
		FrameData->TemporalJitterIndex = InPassMetrics.TemporalJitterIndex;
		FrameData->NumTemporalJitters = InPassMetrics.NumTemporalJitters;
		FrameData->JitterOffsetX = InPassMetrics.JitterOffsetX;
		FrameData->JitterOffsetY = InPassMetrics.JitterOffsetY;
		FrameData->bIsUsingOverlappedTiles = InPassMetrics.bIsUsingOverlappedTiles;

		FrameData->AccumulationGamma = InPassMetrics.AccumulationGamma;

		FrameData->OverlappedOffsetX = InPassMetrics.OverlappedOffsetX;
		FrameData->OverlappedOffsetY = InPassMetrics.OverlappedOffsetY;
		FrameData->OverlappedSizeX = InPassMetrics.OverlappedSizeX;
		FrameData->OverlappedSizeY = InPassMetrics.OverlappedSizeY;
		FrameData->OverlappedPadX = InPassMetrics.OverlappedPadX;
		FrameData->OverlappedPadY = InPassMetrics.OverlappedPadY;
		FrameData->OverlappedSubpixelShift = InPassMetrics.OverlappedSubpixelShift;

		TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(SourceRect.Size(), FrameData);
		RHICmdList.ReadSurfaceData(CanvasRenderTarget->GetRenderTargetTexture(), SourceRect, PixelData->Pixels, ReadDataFlags);

		check(PixelData->IsDataWellFormed());

		const bool bIsOutputFrame = true;
		if (bIsOutputFrame)
		{
			LocalOutputPipe->Push(MoveTemp(PixelData));
		}
	}
	);
}

FSceneView* UMoviePipelineBackbufferPass::CalcSceneView(FSceneViewFamily* ViewFamily, const FMoviePipelineRenderPassMetrics& InPassMetrics)
{
	FVector ViewLocation;
	FRotator ViewRotation;

	APlayerController* LocalPlayerController = GetWorld()->GetFirstPlayerController();
	LocalPlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	int32 TileSizeX = InitSettings.TileResolution.X;
	int32 TileSizeY = InitSettings.TileResolution.Y;

	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.ViewOrigin = ViewLocation;
	ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), FIntPoint(TileSizeX, TileSizeY)));
	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(ViewRotation);

	// Rotate the view 90 degrees (reason: unknown)
	ViewInitOptions.ViewRotationMatrix = ViewInitOptions.ViewRotationMatrix * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	float ViewFOV = 90.f;
	if (GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
	{
		ViewFOV = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetFOVAngle();
	}

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

		if (InPassMetrics.bIsUsingOverlappedTiles)
		{
			float PadRatioX = float(InPassMetrics.OverlappedPadX * 2 + InPassMetrics.OverlappedSizeX) / float (InPassMetrics.OverlappedSizeX);
			float PadRatioY = float(InPassMetrics.OverlappedPadY * 2 + InPassMetrics.OverlappedSizeY) / float (InPassMetrics.OverlappedSizeY);

			float ScaleX = PadRatioX / float(InitSettings.TileCount);
			float ScaleY = PadRatioY / float(InitSettings.TileCount);

			BaseProjMatrix.M[0][0] /= ScaleX;
			BaseProjMatrix.M[1][1] /= ScaleY;

			// this offset would be correct with no pad
			float OffsetX = -((float(InPassMetrics.TileIndexX) + 0.5f - float(InitSettings.TileCount) / 2.0f)* 2.0f);
			float OffsetY =  ((float(InPassMetrics.TileIndexY) + 0.5f - float(InitSettings.TileCount) / 2.0f)* 2.0f);

			BaseProjMatrix.M[2][0] += OffsetX / PadRatioX;
			BaseProjMatrix.M[2][1] += OffsetY / PadRatioX;
		}

		ViewInitOptions.ProjectionMatrix = BaseProjMatrix;
	}

	ViewInitOptions.SceneViewStateInterface = ViewState.GetReference();
	ViewInitOptions.FOV = ViewFOV;

	FSceneView* View = new FSceneView(ViewInitOptions);
	ViewFamily->Views.Add(View);
	View->ViewLocation = ViewLocation;
	View->ViewRotation = ViewRotation;
	View->StartFinalPostprocessSettings(View->ViewLocation);

	// CameraAnim override
	if (LocalPlayerController->PlayerCameraManager)
	{
		TArray<FPostProcessSettings> const* CameraAnimPPSettings;
		TArray<float> const* CameraAnimPPBlendWeights;
		LocalPlayerController->PlayerCameraManager->GetCachedPostProcessBlends(CameraAnimPPSettings, CameraAnimPPBlendWeights);

		FMinimalViewInfo ViewInfo = LocalPlayerController->PlayerCameraManager->GetCameraCachePOV();
		for (int32 PPIdx = 0; PPIdx < CameraAnimPPBlendWeights->Num(); ++PPIdx)
		{
			View->OverridePostProcessSettings((*CameraAnimPPSettings)[PPIdx], (*CameraAnimPPBlendWeights)[PPIdx]);
		}

		View->OverridePostProcessSettings(ViewInfo.PostProcessSettings, ViewInfo.PostProcessBlendWeight);

	}

	View->EndFinalPostprocessSettings(ViewInitOptions);

	return View;
}