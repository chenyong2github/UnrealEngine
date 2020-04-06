// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipeline.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineOutputBase.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineRenderPass.h"
#include "MoviePipelineOutputBuilder.h"
#include "RenderingThread.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineConfigBase.h"
#include "MoviePipelineMasterConfig.h"
#include "Math/Halton.h"
#include "ImageWriteTask.h"
#include "ImageWriteQueue.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MoviePipelineHighResSetting.h"
#include "Modules/ModuleManager.h"
#include "MoviePipelineCameraSetting.h"
#include "Engine/GameViewportClient.h"
#include "LegacyScreenPercentageDriver.h"

// For flushing async systems
#include "RendererInterface.h"
#include "LandscapeProxy.h"
#include "EngineModule.h"
#include "DistanceFieldAtlas.h"
#include "ShaderCompiler.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "MoviePipeline"

static TArray<UMoviePipelineRenderPass*> GetAllRenderPasses(const UMoviePipelineMasterConfig* InMasterConfig, const FMoviePipelineShotInfo& InShot)
{
	TArray<UMoviePipelineRenderPass*> RenderPasses;

	// Master Configuration first.
	RenderPasses.Append(InMasterConfig->FindSettings<UMoviePipelineRenderPass>());

	// And then any additional passes requested by the shot.
	if (InShot.ShotOverrideConfig != nullptr)
	{
		RenderPasses.Append(InShot.ShotOverrideConfig->FindSettings<UMoviePipelineRenderPass>());
	}

	return RenderPasses;
}


bool GetAnyOutputWantsAlpha(UMoviePipelineConfigBase* InConfig)
{
	TArray<UMoviePipelineOutputBase*> OutputSettings = InConfig->FindSettings<UMoviePipelineOutputBase>();

	for (const UMoviePipelineOutputBase* Output : OutputSettings)
	{
		if (Output->IsAlphaSupported())
		{
			return true;
		}
	}

	return false;
}

void UMoviePipeline::SetupRenderingPipelineForShot(FMoviePipelineShotInfo& Shot)
{
	/*
	* To support tiled rendering we take the final effective resolution and divide
	* it by the number of tiles to find the resolution of each render target. To 
	* handle non-evenly divisible numbers/resolutions we may oversize the targets
	* by a few pixels and then take the center of the resulting image when interlacing
	* to produce the final image at the right resolution. For example:
	*
	* 1920x1080 in 7x7 tiles gives you 274.29x154.29. We ceiling this to set the resolution
	* of the render pass to 275x155 which will give us a final interleaved image size of
	* 1925x1085. To ensure that the image matches a non-scaled one we take the center out.
	* LeftOffset = floor((1925-1920)/2) = 2
	* RightOffset = (1925-1920-LeftOffset)
	*/
	UMoviePipelineAntiAliasingSetting* AccumulationSettings = FindOrAddSetting<UMoviePipelineAntiAliasingSetting>(Shot);
	UMoviePipelineHighResSetting* HighResSettings = FindOrAddSetting<UMoviePipelineHighResSetting>(Shot);
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	bool bAnyOutputWantsAlpha = GetAnyOutputWantsAlpha(GetPipelineMasterConfig());

	FIntPoint BackbufferTileCount = FIntPoint(HighResSettings->TileCount, HighResSettings->TileCount);
	
	// Figure out how big each sub-region (tile) is.
	FIntPoint BackbufferResolution = FIntPoint(
		FMath::CeilToInt(OutputSettings->OutputResolution.X / HighResSettings->TileCount),
		FMath::CeilToInt(OutputSettings->OutputResolution.Y / HighResSettings->TileCount));

	// Then increase each sub-region by the overlap amount.
	BackbufferResolution = HighResSettings->CalculatePaddedBackbufferSize(BackbufferResolution);

	// Note how many tiles we wish to render with.
	BackbufferTileCount = FIntPoint(HighResSettings->TileCount, HighResSettings->TileCount);

	// Initialize our render pass. This is a copy of the settings to make this less coupled to the Settings UI.
	MoviePipeline::FMoviePipelineRenderPassInitSettings RenderPassInitSettings;
	RenderPassInitSettings.BackbufferResolution = BackbufferResolution;
	RenderPassInitSettings.TileCount = BackbufferTileCount;
	RenderPassInitSettings.bAccumulateAlpha = bAnyOutputWantsAlpha;

	// Code expects at least a 1x1 tile.
	ensure(RenderPassInitSettings.TileCount.X > 0 && RenderPassInitSettings.TileCount.Y > 0);

	// Now we need to look at all of the desired passes and find a unique set of actual engine passes that need
	// to be rendered. This allows us to have multiple passes that re-use one render from the engine for efficiency.
	TSet<FMoviePipelinePassIdentifier> RequiredEnginePasses;

	for (UMoviePipelineRenderPass* RenderPass : GetAllRenderPasses(GetPipelineMasterConfig(), Shot))
	{
		RenderPass->GetRequiredEnginePasses(RequiredEnginePasses);
	}

	// There shouldn't be any render passes active from previous shots by now. The system should have flushed/stalled between
	// them to complete using resources before switching passes.
	check(ActiveRenderPasses.Num() == 0);

	// Instantiate a new instance of every engine render pass we know how to use.
	TArray<TSharedPtr<MoviePipeline::FMoviePipelineEnginePass>> RenderPasses;

	FMovieRenderPipelineCoreModule& CoreModule = FModuleManager::Get().LoadModuleChecked<FMovieRenderPipelineCoreModule>("MovieRenderPipelineCore");
	for (const FOnCreateEngineRenderPass& PassCreationDelegate : CoreModule.GetEngineRenderPasses())
	{
		TSharedRef<MoviePipeline::FMoviePipelineEnginePass> PassInstance = PassCreationDelegate.Execute();
		if (RequiredEnginePasses.Contains(PassInstance->PassIdentifier))
		{
			ActiveRenderPasses.Add(PassInstance);
			RequiredEnginePasses.Remove(PassInstance->PassIdentifier);
		}
	}

	for(const FMoviePipelinePassIdentifier& RemainingPass : RequiredEnginePasses)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Pass \"%d\" was listed as a required engine render pass but was not found. Did you forget to register it with the module?"), *RemainingPass.Name);
		OnMoviePipelineErrored().Broadcast(this, true, LOCTEXT("MissingEnginePass", "A Render Pass specified an invalid Pass Identifier. Aborting Render. Check the log for more information."));
	}

	// Initialize each of the engine render passes.
	for (TSharedPtr<MoviePipeline::FMoviePipelineEnginePass> EnginePass : ActiveRenderPasses)
	{
		EnginePass->Setup(MakeWeakObjectPtr(this), RenderPassInitSettings);
	}

	// We can now initialize the output passes and provide them a reference to the engine passes to get data from.
	int32 NumOutputPasses = 0;
	for (UMoviePipelineRenderPass* RenderPass : GetAllRenderPasses(GetPipelineMasterConfig(), Shot))
	{
		RenderPass->Setup(ActiveRenderPasses, RenderPassInitSettings);
		NumOutputPasses++;
	}

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Finished setting up rendering for shot. Shot has %d Engine Passes and %d Output Passes."), ActiveRenderPasses.Num(), NumOutputPasses);
}

void UMoviePipeline::TeardownRenderingPipelineForShot(FMoviePipelineShotInfo& Shot)
{
	// Master Configuration first.
	for (UMoviePipelineRenderPass* RenderPass : GetAllRenderPasses(GetPipelineMasterConfig(), Shot))
	{
		RenderPass->Teardown();
	}

	for (TSharedPtr<MoviePipeline::FMoviePipelineEnginePass> EnginePass : ActiveRenderPasses)
	{
		EnginePass->Teardown();
	}

	ActiveRenderPasses.Empty();
}

void UMoviePipeline::RenderFrame()
{
	// Flush built in systems before we render anything. This maximizes the likelihood that the data is prepared for when
	// the render thread uses it.
	FlushAsyncEngineSystems();

	// Send any output frames that have been completed since the last render.
	ProcessOutstandingFinishedFrames();

	FMoviePipelineShotInfo& CurrentShot = ShotList[CurrentShotIndex];
	FMoviePipelineCameraCutInfo& CurrentCameraCut = CurrentShot.GetCurrentCameraCut();
	APlayerController* LocalPlayerController = GetWorld()->GetFirstPlayerController();


	// If we don't want to render this frame, then we will skip processing - engine warmup frames,
	// render every nTh frame, etc. In other cases, we may wish to render the frame but discard the
	// result and not send it to the output merger (motion blur frames, gpu feedback loops, etc.)
	if (CachedOutputState.bSkipRendering)
	{
		return;
	}
	
	// Hide the progress widget before we render anything. This allows widget captures to not include the progress bar.
	SetProgressWidgetVisible(false);

	// To produce a frame from the movie pipeline we may render many frames over a period of time, additively collecting the results
	// together before submitting it for writing on the last result - this is referred to as an "output frame". The 1 (or more) samples
	// that make up each output frame are referred to as "sample frames". Within each sample frame, we may need to render the scene many
	// times. In order to support ultra-high-resolution rendering (>16k) movie pipelines support building an output frame out of 'tiles'. 
	// Each tile renders the entire viewport with a small offset which causes different samples to be picked for each final pixel. These
	// 'tiles' are then interleaved together (on the CPU) to produce a higher resolution result. For each tile, we can render a number
	// of jitters that get added together to produce a higher quality single frame. This is useful for cases where you may not want any 
	// motion (such as trees fluttering in the wind) but you do want high quality anti-aliasing on the edges of the pixels. Finally,
	// the outermost loop (which is not represented here) is accumulation over time which happens over multiple engine ticks.
	// 
	// In short, for each output frame, for each accumulation frame, for each tile X/Y, for each jitter, we render a pass. This setup is
	// designed to maximize the likely hood of deterministic rendering and that different passes line up with each other.
	UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = FindOrAddSetting<UMoviePipelineAntiAliasingSetting>(CurrentShot);
	UMoviePipelineCameraSetting* CameraSettings = FindOrAddSetting<UMoviePipelineCameraSetting>(CurrentShot);
	UMoviePipelineHighResSetting* HighResSettings = FindOrAddSetting<UMoviePipelineHighResSetting>(CurrentShot);
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(AntiAliasingSettings);
	check(CameraSettings);
	check(HighResSettings);
	check(OutputSettings);

	FIntPoint TileCount = FIntPoint(HighResSettings->TileCount, HighResSettings->TileCount);
	FIntPoint OutputResolution = OutputSettings->OutputResolution;

	int32 NumSpatialSamples = AntiAliasingSettings->SpatialSampleCount;
	int32 NumTemporalSamples = AntiAliasingSettings->TemporalSampleCount;
	if (!ensureAlways(TileCount.X > 0 && TileCount.Y > 0 && NumSpatialSamples > 0 && NumTemporalSamples > 0))
	{
		return;
	}

	FrameInfo.PrevViewLocation = FrameInfo.CurrViewLocation;
	FrameInfo.PrevViewRotation = FrameInfo.CurrViewRotation;

	// Update our current view location
	LocalPlayerController->GetPlayerViewPoint(FrameInfo.CurrViewLocation, FrameInfo.CurrViewRotation);

	if (!CurrentCameraCut.bHasEvaluatedMotionBlurFrame)
	{
		// There won't be a valid Previous if we haven't done motion blur.
		FrameInfo.PrevViewLocation = FrameInfo.CurrViewLocation;
		FrameInfo.PrevViewRotation = FrameInfo.CurrViewRotation;
	}

	if (CurrentCameraCut.State != EMovieRenderShotState::Rendering)
	{
		// We can optimize some of the settings for 'special' frames we may be rendering, ie: we render once for motion vectors, but
		// we don't need that per-tile so we can set the tile count to 1, and spatial sample count to 1 for that particular frame.
		{
			// Tiling is only needed when actually producing frames.
			TileCount.X = 1;
			TileCount.Y = 1;

			// Spatial Samples aren't needed when not producing frames (caveat: Render Warmup Frame, handled below)
			NumSpatialSamples = 1;
		}
	}

	int32 NumWarmupSamples = 0;
	if (CurrentCameraCut.State == EMovieRenderShotState::WarmingUp)
	{
		// We should only get this far if we want to render samples, so we'll always overwrite it with NumRenderWarmUpSamples. We should
		// not change the NumSpatialSamples because that causes side effects to other parts of the rendering.
		NumWarmupSamples = AntiAliasingSettings->RenderWarmUpCount;
	}

	TArray<UMoviePipelineRenderPass*> InputBuffers = GetAllRenderPasses(GetPipelineMasterConfig(), CurrentShot);

	// If this is the first sample for a new frame, we want to notify the output builder that it should expect data to accumulate for this frame.
	if (CachedOutputState.IsFirstTemporalSample())
	{
		// This happens before any data is queued for this frame.
		FMoviePipelineMergerOutputFrame& OutputFrame = OutputBuilder->QueueOutputFrame_GameThread(CachedOutputState);

		// Now we need to go through all passes and get any identifiers from them of what this output frame should expect.
		for (UMoviePipelineRenderPass* RenderPass : InputBuffers)
		{
			RenderPass->GatherOutputPasses(OutputFrame.ExpectedRenderPasses);
		}
	}

	for (int32 TileY = 0; TileY < TileCount.Y; TileY++)
	{
		for (int32 TileX = 0; TileX < TileCount.X; TileX++)
		{
			int NumSamplesToRender = (CurrentCameraCut.State == EMovieRenderShotState::WarmingUp) ? NumWarmupSamples : NumSpatialSamples;

			// Now we want to render a user-configured number of spatial jitters to come up with the final output for this tile. 
			for (int32 RenderSampleIndex = 0; RenderSampleIndex < NumSamplesToRender; RenderSampleIndex++)
			{
				int32 SpatialSampleIndex = (CurrentCameraCut.State == EMovieRenderShotState::WarmingUp) ? 0 : RenderSampleIndex;

				if (CurrentCameraCut.State == EMovieRenderShotState::Rendering)
				{
					// Count this as a sample rendered for the current work.
					CurrentCameraCut.WorkMetrics.OutputSubSampleIndex++;
				}

				// We freeze views for all spatial samples except the last so that nothing in the FSceneView tries to update.
				// Our spatial samples need to be different positional takes on the same world, thus pausing it.
				const bool bAllowPause = CurrentCameraCut.State == EMovieRenderShotState::Rendering;
				const bool bIsLastTile = FIntPoint(TileX, TileY) == FIntPoint(TileCount.X - 1, TileCount.Y - 1);
				const bool bWorldIsPaused = bAllowPause && !(bIsLastTile && (RenderSampleIndex == (NumSamplesToRender - 1)));

				// We need to pass camera cut flag on the first sample that gets rendered for a given camera cut. If you don't have any render
				// warm up frames, we do this on the first render sample because we no longer render the motion blur frame (just evaluate it).
				const bool bCameraCut = CachedOutputState.ShotSamplesRendered == 0;
				CachedOutputState.ShotSamplesRendered++;

				EAntiAliasingMethod AntiAliasingMethod = UE::MovieRenderPipeline::GetEffectiveAntiAliasingMethod(AntiAliasingSettings);

				// Now to check if we have to force it off (at which point we warn the user).
				bool bMultipleTiles = (TileCount.X > 1) || (TileCount.Y > 1);
				if (bMultipleTiles && AntiAliasingMethod == EAntiAliasingMethod::AAM_TemporalAA)
				{
					// Temporal Anti-Aliasing isn't supported when using tiled rendering because it relies on having history, and
					// the tiles use the previous tile as the history which is incorrect. 
					UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Temporal AntiAliasing is not supported when using tiling!"));
					AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
				}


				// We Abs this so that negative numbers on the first frame of a cut (warm ups) don't go into Halton which will assign 0.
				int32 ClampedFrameNumber = FMath::Max(0, CachedOutputState.OutputFrameNumber);
				int32 ClampedTemporalSampleIndex = FMath::Max(0, CachedOutputState.TemporalSampleIndex);
				int32 FrameIndex = FMath::Abs((ClampedFrameNumber * (NumTemporalSamples * NumSpatialSamples)) + (ClampedTemporalSampleIndex * NumSpatialSamples) + SpatialSampleIndex);

				// if we are warming up, we will just use the RenderSampleIndex as the FrameIndex so the samples jump around a bit.
				if (CurrentCameraCut.State == EMovieRenderShotState::WarmingUp)
				{
					FrameIndex = RenderSampleIndex;
				}
				

				// Repeat the Halton Offset equally on each output frame so non-moving objects don't have any chance to crawl between frames.
				int32 HaltonIndex = (FrameIndex % (NumSpatialSamples*NumTemporalSamples)) + 1;
				float HaltonOffsetX = Halton(HaltonIndex, 2);
				float HaltonOffsetY = Halton(HaltonIndex, 3);

				// only allow a spatial jitter if we have more than one sample
				bool bAllowSpatialJitter = !(NumSpatialSamples == 1 && NumTemporalSamples == 1);

				UE_LOG(LogTemp, VeryVerbose, TEXT("FrameIndex: %d HaltonIndex: %d Offset: (%f,%f)"), FrameIndex, HaltonIndex, HaltonOffsetX, HaltonOffsetY);
				float SpatialShiftX = 0.0f;
				float SpatialShiftY = 0.0f;

				if (bAllowSpatialJitter)
				{
					static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TemporalAAFilterSize"));
					float FilterSize = CVar->GetFloat();

					// Scale distribution to set non-unit variance
					// Variance = Sigma^2
					float Sigma = 0.47f * FilterSize;

					// Window to [-0.5, 0.5] output
					// Without windowing we could generate samples far away on the infinite tails.
					float OutWindow = 0.5f;
					float InWindow = FMath::Exp(-0.5 * FMath::Square(OutWindow / Sigma));

					// Box-Muller transform
					float Theta = 2.0f * PI * HaltonOffsetY;
					float r = Sigma * FMath::Sqrt(-2.0f * FMath::Loge((1.0f - HaltonOffsetX) * InWindow + HaltonOffsetX));

					SpatialShiftX = r * FMath::Cos(Theta);
					SpatialShiftY = r * FMath::Sin(Theta);
				}

				FIntPoint BackbufferResolution = FIntPoint(FMath::CeilToInt(OutputSettings->OutputResolution.X / TileCount.X), FMath::CeilToInt(OutputSettings->OutputResolution.Y / TileCount.Y));
				FIntPoint TileResolution = BackbufferResolution;

				// Apply size padding.
				BackbufferResolution = HighResSettings->CalculatePaddedBackbufferSize(BackbufferResolution);

				// We take all of the information needed to render a single sample and package it into a struct.
				FMoviePipelineRenderPassMetrics SampleState;
				SampleState.FrameIndex = FrameIndex;
				SampleState.bWorldIsPaused = bWorldIsPaused;
				SampleState.bCameraCut = bCameraCut;
				SampleState.AntiAliasingMethod = AntiAliasingMethod;
				SampleState.SceneCaptureSource = OutputSettings->bDisableToneCurve ? ESceneCaptureSource::SCS_FinalColorHDR : ESceneCaptureSource::SCS_FinalToneCurveHDR;
				SampleState.OutputState = CachedOutputState;
				SampleState.ProjectionMatrixJitterAmount = FVector2D((float)(SpatialShiftX) * 2.0f / BackbufferResolution.X, (float)SpatialShiftY * -2.0f / BackbufferResolution.Y);
				SampleState.TileIndexes = FIntPoint(TileX, TileY);
				SampleState.TileCounts = TileCount;
				SampleState.bDiscardResult = CachedOutputState.bDiscardRenderResult;
				SampleState.SpatialSampleIndex = SpatialSampleIndex;
				SampleState.SpatialSampleCount = NumSpatialSamples;
				SampleState.TemporalSampleIndex = CachedOutputState.TemporalSampleIndex;
				SampleState.TemporalSampleCount = AntiAliasingSettings->TemporalSampleCount;
				SampleState.AccumulationGamma = AntiAliasingSettings->AccumulationGamma;
				SampleState.BackbufferSize = BackbufferResolution;
				SampleState.TileSize = TileResolution;
				SampleState.FrameInfo = FrameInfo;
				SampleState.bWriteSampleToDisk = HighResSettings->bWriteAllSamples;
				SampleState.ExposureCompensation = CameraSettings->bManualExposure ? CameraSettings->ExposureCompensation : TOptional<float>();
				SampleState.TextureSharpnessBias = HighResSettings->TextureSharpnessBias;
				SampleState.GlobalScreenPercentageFraction = FLegacyScreenPercentageDriver::GetCVarResolutionFraction();
				{
					SampleState.OverlappedPad = FIntPoint(FMath::CeilToInt(TileResolution.X * HighResSettings->OverlapRatio), 
														   FMath::CeilToInt(TileResolution.Y * HighResSettings->OverlapRatio));
					SampleState.OverlappedOffset = FIntPoint(TileX * TileResolution.X - SampleState.OverlappedPad.X,
															  TileY * TileResolution.Y - SampleState.OverlappedPad.Y);

					// Move the final render by this much in the accumulator to counteract the offset put into the view matrix.
					// Note that when bAllowSpatialJitter is false, SpatialShiftX/Y will always be zero.
					SampleState.OverlappedSubpixelShift = FVector2D(0.5f - SpatialShiftX, 0.5f - SpatialShiftY);
				}

				SampleState.WeightFunctionX.InitHelper(SampleState.OverlappedPad.X, SampleState.TileSize.X, SampleState.OverlappedPad.X);
				SampleState.WeightFunctionY.InitHelper(SampleState.OverlappedPad.Y, SampleState.TileSize.Y, SampleState.OverlappedPad.Y);

				// Now we can request that all of the engine passes render. The individual render passes should have already registered delegates
				// to receive data when the engine render pass is run, so no need to run them.
				for (TSharedPtr<MoviePipeline::FMoviePipelineEnginePass> EnginePass : ActiveRenderPasses)
				{
					EnginePass->RenderSample_GameThread(SampleState);
				}

				// We give a chance for each individual render pass to render as well. This should be used if they're not trying to share data from an Engine Pass.
				for (UMoviePipelineRenderPass* RenderPass : InputBuffers)
				{
					RenderPass->RenderSample_GameThread(SampleState);
				}
			}
		}
	}

	// Re-enable the progress widget so when the player viewport is drawn to the preview window, it shows.
	SetProgressWidgetVisible(true);
}

void UMoviePipeline::ProcessOutstandingFinishedFrames()
{
	while (!OutputBuilder->FinishedFrames.IsEmpty())
	{
		FMoviePipelineMergerOutputFrame OutputFrame;
		OutputBuilder->FinishedFrames.Dequeue(OutputFrame);

		for (UMoviePipelineOutputBase* OutputContainer : GetPipelineMasterConfig()->GetOutputContainers())
		{
			OutputContainer->OnRecieveImageData(&OutputFrame);
		}
	}
}

void UMoviePipeline::OnSampleRendered(TUniquePtr<FImagePixelData>&& OutputSample, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData)
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	// This is for debug output, writing every individual sample to disk that comes off of the GPU (that isn't discarded).
	TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();


	TileImageTask->Format = EImageFormat::EXR;
	TileImageTask->CompressionQuality = 100;

	FString OutputName = FString::Printf(TEXT("/%s_SS_%d_TS_%d_TileX_%d_TileY_%d.%d.jpeg"),
		*InFrameData->PassIdentifier.Name, InFrameData->SampleState.SpatialSampleIndex, InFrameData->SampleState.TemporalSampleIndex,
		InFrameData->SampleState.TileIndexes.X, InFrameData->SampleState.TileIndexes.Y, InFrameData->OutputState.OutputFrameNumber);

	FString OutputDirectory = OutputSettings->OutputDirectory.Path;
	FString OutputPath = OutputDirectory + OutputName;
	TileImageTask->Filename = OutputPath;

	// Duplicate the data so that the Image Task can own it.
	TileImageTask->PixelData = MoveTemp(OutputSample);
	ImageWriteQueue->Enqueue(MoveTemp(TileImageTask));
}



void UMoviePipeline::FlushAsyncEngineSystems()
{
	// Flush Level Streaming. This solves the problem where levels that are not controlled
	// by the Sequencer Level Visibility track are marked for Async Load by a gameplay system.
	// This will register any new actors/components that were spawned during this frame. This needs 
	// to be done before the shader compiler is flushed so that we compile shaders for any newly
	// spawned component materials.
	if (GetWorld())
	{
		GetWorld()->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	}

	// Now we can flush the shader compiler. ToDo: This should probably happen right before SendAllEndOfFrameUpdates() is normally called
	if (GShaderCompilingManager)
	{
		bool bDidWork = false;
		int32 NumShadersToCompile = GShaderCompilingManager->GetNumRemainingJobs();
		if (NumShadersToCompile > 0)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Starting build for %d shaders."), GFrameCounter, NumShadersToCompile);
		}

		while (GShaderCompilingManager->GetNumRemainingJobs() > 0 || GShaderCompilingManager->HasShaderJobs())
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Waiting for %d shaders [Has Shader Jobs: %d] to finish compiling..."), GFrameCounter, GShaderCompilingManager->GetNumRemainingJobs(), GShaderCompilingManager->HasShaderJobs());
			GShaderCompilingManager->ProcessAsyncResults(false, true);

			// Sleep for 1 second and then check again. This way we get an indication of progress as this works.
			FPlatformProcess::Sleep(1.f);
			bDidWork = true;
		}

		if (bDidWork)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Done building %d shaders."), GFrameCounter, NumShadersToCompile);
		}
	}

	// Flush the Mesh Distance Field builder as well.
	if (GDistanceFieldAsyncQueue)
	{
		bool bDidWork = false;
		int32 NumDistanceFieldsToBuild = GDistanceFieldAsyncQueue->GetNumOutstandingTasks();
		if (NumDistanceFieldsToBuild > 0)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Starting build for %d mesh distance fields."), GFrameCounter, NumDistanceFieldsToBuild);
		}

		while (GDistanceFieldAsyncQueue->GetNumOutstandingTasks() > 0)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Waiting for %d Mesh Distance Fields to finish building..."), GFrameCounter, GDistanceFieldAsyncQueue->GetNumOutstandingTasks());
			GDistanceFieldAsyncQueue->ProcessAsyncTasks();

			// Sleep for 1 second and then check again. This way we get an indication of progress as this works.
			FPlatformProcess::Sleep(1.f);
			bDidWork = true;
		}

		if (bDidWork)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Done building %d Mesh Distance Fields."), GFrameCounter, NumDistanceFieldsToBuild);
		}
	}

	// Flush grass
	{
		for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
		{
			ALandscapeProxy* LandscapeProxy = (*It);
			if (LandscapeProxy)
			{
				TArray<FVector> CameraList;
				LandscapeProxy->UpdateGrass(CameraList, true);
			}
		}
	}

	// Flush virtual texture tile calculations
	ERHIFeatureLevel::Type FeatureLevel = GetWorld()->FeatureLevel;
	ENQUEUE_RENDER_COMMAND(VirtualTextureSystemFlushCommand)(
		[FeatureLevel](FRHICommandListImmediate& RHICmdList)
	{
		GetRendererModule().LoadPendingVirtualTextureTiles(RHICmdList, FeatureLevel);
	});
}

#undef LOCTEXT_NAMESPACE // "MoviePipeline"
