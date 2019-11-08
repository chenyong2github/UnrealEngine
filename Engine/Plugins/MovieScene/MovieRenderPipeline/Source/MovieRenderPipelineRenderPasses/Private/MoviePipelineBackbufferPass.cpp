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
#include "MoviePipelineOutputBuilder.h"
#include "MovieRenderTileImage.h"
#include "Async/Async.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MovieRenderOverlappedImage.h"

namespace MoviePipeline
{
	struct FSampleRenderThreadParams
	{
		FMoviePipelineRenderPassMetrics PassMetrics;
		FRenderTarget* CanvasRenderTarget;
		TSharedPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> ImageAccumulator;
		TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputMerger;
		bool bWriteTiles;
	};
}

// Forward Declares
static void ReadAndAccumulateSample_RenderThread(FRHICommandListImmediate &RHICmdList, const MoviePipeline::FSampleRenderThreadParams& InParams);

void UMoviePipelineBackbufferPass::SetupImpl(const FMoviePipelineRenderPassInitSettings& InInitSettings)
{
	TileRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	TileRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Initialize to the tile size (not final size) and use a 16 bit back buffer to avoid precision issues when accumulating later

	int32 TargetSizeX = InInitSettings.TileResolution.X;
	int32 TargetSizeY = InInitSettings.TileResolution.Y;
/*
	*/
	TileRenderTarget->InitCustomFormat(TargetSizeX, TargetSizeY, EPixelFormat::PF_A16B16G16R16, false);
	// Create a Render Target to hold our back buffer.
	ViewState.Allocate();

	ImageTileAccumulator = MakeShared<FImageOverlappedAccumulator, ESPMode::ThreadSafe>();
}

void UMoviePipelineBackbufferPass::TeardownImpl()
{
	ViewState.Destroy();
}

void UMoviePipelineBackbufferPass::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(TEXT("Backbuffer")));
}

void UMoviePipelineBackbufferPass::CaptureFrameImpl(const FMoviePipelineRenderPassMetrics& InPassMetrics)
{
	float TimeSeconds = InPassMetrics.OutputState.WorldSeconds;
	float RealTimeSeconds = InPassMetrics.OutputState.WorldSeconds;
	float DeltaTimeSeconds = InPassMetrics.OutputState.FrameDeltaTime;

	FEngineShowFlags ShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);

	if (InPassMetrics.bIsUsingOverlappedTiles)
	{
		// Disable these for now
		//ShowFlags.SetDepthOfField(false);
		ShowFlags.SetMotionBlur(false);
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		TileRenderTarget->GameThread_GetRenderTargetResource(),
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

	FCanvas Canvas = FCanvas(TileRenderTarget->GameThread_GetRenderTargetResource(), nullptr, GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);


	// Draw the world into this View Family
	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

	FRenderTarget* CanvasRenderTarget = Canvas.GetRenderTarget();
	TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputMerger = GetPipeline()->OutputBuilder;
	TSharedPtr<FImageOverlappedAccumulator, ESPMode::ThreadSafe> LocalImageTileAccumulator = ImageTileAccumulator;

	MoviePipeline::FSampleRenderThreadParams AccumulationParams;
	{
		AccumulationParams.bWriteTiles = true;
		AccumulationParams.CanvasRenderTarget = CanvasRenderTarget;
		AccumulationParams.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationParams.ImageAccumulator = ImageTileAccumulator;
		AccumulationParams.PassMetrics = InPassMetrics;
	}

	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[AccumulationParams](FRHICommandListImmediate& RHICmdList)
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Rendered Tile: %d Sample: %d"), AccumulationParams.PassMetrics.TileIndex, AccumulationParams.PassMetrics.SpatialJitterIndex);
		ReadAndAccumulateSample_RenderThread(RHICmdList, AccumulationParams);
	});
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

void ReadAndAccumulateSample_RenderThread(FRHICommandListImmediate &RHICmdList, const MoviePipeline::FSampleRenderThreadParams& InParams)
{
	check(IsInRenderingThread());

	// If they only needed this frame for the history, we don't actually care about trying to output it,
	// so we can skip the copy and pushing to output.
	if (InParams.PassMetrics.bIsHistoryOnlyFrame)
	{
		return;
	}

	FReadSurfaceDataFlags ReadDataFlags;
	ReadDataFlags.SetLinearToGamma(false);

	FIntRect SourceRect = FIntRect(0, 0, InParams.CanvasRenderTarget->GetSizeXY().X, InParams.CanvasRenderTarget->GetSizeXY().Y);

	// Build our metadata to associate with this frame.
	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FrameData = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FrameData->OutputState = InParams.PassMetrics.OutputState;
	FrameData->PassIdentifier = FMoviePipelinePassIdentifier(TEXT("Backbuffer"));
	FrameData->TileIndexX = InParams.PassMetrics.TileIndexX;
	FrameData->TileIndexY = InParams.PassMetrics.TileIndexY;
	FrameData->TileSizeX = SourceRect.Width();
	FrameData->TileSizeY = SourceRect.Height();
	FrameData->NumTilesX = InParams.PassMetrics.NumTilesX;
	FrameData->NumTilesY = InParams.PassMetrics.NumTilesY;
	FrameData->SpatialShift = InParams.PassMetrics.SpatialShift;
	FrameData->SpatialJitterIndex = InParams.PassMetrics.SpatialJitterIndex;
	FrameData->NumSpatialJitters = InParams.PassMetrics.NumSpatialJitters;
	FrameData->TemporalJitterIndex = InParams.PassMetrics.TemporalJitterIndex;
	FrameData->NumTemporalJitters = InParams.PassMetrics.NumTemporalJitters;
	FrameData->JitterOffsetX = InParams.PassMetrics.JitterOffsetX;
	FrameData->JitterOffsetY = InParams.PassMetrics.JitterOffsetY;
	FrameData->bIsUsingOverlappedTiles = InParams.PassMetrics.bIsUsingOverlappedTiles;

	FrameData->AccumulationGamma = InParams.PassMetrics.AccumulationGamma;

	FrameData->OverlappedOffsetX = InParams.PassMetrics.OverlappedOffsetX;
	FrameData->OverlappedOffsetY = InParams.PassMetrics.OverlappedOffsetY;
	FrameData->OverlappedSizeX = InParams.PassMetrics.OverlappedSizeX;
	FrameData->OverlappedSizeY = InParams.PassMetrics.OverlappedSizeY;
	FrameData->OverlappedPadX = InParams.PassMetrics.OverlappedPadX;
	FrameData->OverlappedPadY = InParams.PassMetrics.OverlappedPadY;
	FrameData->OverlappedSubpixelShift = InParams.PassMetrics.OverlappedSubpixelShift;

	// Read the data back to the CPU
	TArray<FColor> RawPixels;
	RawPixels.SetNum(SourceRect.Width() * SourceRect.Height());

	RHICmdList.ReadSurfaceData(InParams.CanvasRenderTarget->GetRenderTargetTexture(), SourceRect, RawPixels, ReadDataFlags);
	TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(SourceRect.Size(), TArray64<FColor>(MoveTemp(RawPixels)), FrameData);

	check(PixelData->IsDataWellFormed());

	const bool bWriteTiles = true;
	
	// Writing tiles can be useful for debug reasons. These get passed onto the output every frame.
	if (bWriteTiles)
	{
		// Send the data to the Output Builder. This has to be a copy of the pixel data from the GPU, since
		// it enqueues it onto the game thread and won't be read/sent to write to disk for another frame. 
		// The extra copy is unfortunate, but is only the size of a single sample (ie: 1920x1080 -> 17mb)
		TUniquePtr<FImagePixelData> SampleData = PixelData->Copy();
		InParams.OutputMerger->OnSingleSampleDataAvailable_AnyThread(MoveTemp(SampleData));
	}

	// For the first sample in a new output, we allocate memory
	if (FrameData->IsFirstTile() && FrameData->IsFirstTemporalSample())
	{
		InParams.ImageAccumulator->InitMemory(FIntPoint(FrameData->OverlappedSizeX * FrameData->NumTilesX, FrameData->OverlappedSizeY * FrameData->NumTilesY), 3);
		InParams.ImageAccumulator->ZeroPlanes();
		InParams.ImageAccumulator->AccumulationGamma = FrameData->AccumulationGamma;
	}

	// Accumulate the new sample to our target
	{
		const double AccumulateBeginTime = FPlatformTime::Seconds();

		FIntPoint RawSize = PixelData->GetSize();

		check(FrameData->OverlappedSizeX + 2 * FrameData->OverlappedPadX == RawSize.X);
		check(FrameData->OverlappedSizeY + 2 * FrameData->OverlappedPadY == RawSize.Y);

		InParams.ImageAccumulator->AccumulatePixelData(*PixelData.Get(), FIntPoint(FrameData->OverlappedOffsetX, FrameData->OverlappedOffsetY), FrameData->OverlappedSubpixelShift);

		const double AccumulateEndTime = FPlatformTime::Seconds();
		const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime)*1000.0f);
	
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Accumulation time: %8.2fms"), ElapsedMs);

	}

	if (FrameData->IsLastTile() && FrameData->IsLastTemporalSample())
	{
		int32 FullSizeX = InParams.ImageAccumulator->PlaneSize.X;
		int32 FullSizeY = InParams.ImageAccumulator->PlaneSize.Y;

		// Now that a tile is fully built and accumulated we can notify the output builder that the
		// data is ready so it can pass that onto the output containers (if needed).
		static bool bFullLinearColor = false; // Make this an option. For now, if it's linear, write EXR. Otherwise PNGs for 8bit.
		if (bFullLinearColor)
		{
			// 32 bit FLinearColor
			TUniquePtr<TImagePixelData<FLinearColor> > FinalPixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(FullSizeX, FullSizeY), FrameData);
			InParams.ImageAccumulator->FetchFinalPixelDataLinearColor(FinalPixelData->Pixels);

			// Send the data to the Output Builder
			InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
		}
		else
		{
			// 8bit FColors
			TUniquePtr<TImagePixelData<FColor>> FinalPixelData = MakeUnique<TImagePixelData<FColor>>(FIntPoint(FullSizeX, FullSizeY), FrameData);
			InParams.ImageAccumulator->FetchFinalPixelDataByte(FinalPixelData->Pixels);

			// Send the data to the Output Builder
			InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
		}

		// Free the memory in the accumulator.
		InParams.ImageAccumulator->Reset();
	}
}