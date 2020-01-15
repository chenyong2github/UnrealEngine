// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipeline.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineOutputBase.h"
#include "MoviePipelineAccumulationSetting.h"
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
	UMoviePipelineAccumulationSetting* AccumulationSettings = FindOrAddSetting<UMoviePipelineAccumulationSetting>(Shot);
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
		RenderPass->Setup(ActiveRenderPasses);
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

	FMoviePipelineShotInfo& CurrentShot = ShotList[CurrentShotIndex];
	FMoviePipelineCameraCutInfo& CurrentCameraCut = CurrentShot.GetCurrentCameraCut();

	// We render a frame during motion blur fixes (but don't try to submit it for output) to allow seeding
	// the view histories with data. We want to do this all in one tick so that the motion is correct.
	const bool bIsHistoryOnlyFrame = CurrentCameraCut.State == EMovieRenderShotState::MotionBlur;
	const bool bIsRenderFrame = CurrentCameraCut.State == EMovieRenderShotState::Rendering;

	if(!(bIsHistoryOnlyFrame || bIsRenderFrame))
	{
		UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[%d] Skipping RenderFrame() call due to state being ineligable for rendering. State: %d"), GFrameCounter, CurrentCameraCut.State);
		return;
	}
	
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
	UMoviePipelineAccumulationSetting* AccumulationSettings = FindOrAddSetting<UMoviePipelineAccumulationSetting>(CurrentShot);
	UMoviePipelineCameraSetting* CameraSettings = FindOrAddSetting<UMoviePipelineCameraSetting>(CurrentShot);
	UMoviePipelineHighResSetting* HighResSettings = FindOrAddSetting<UMoviePipelineHighResSetting>(CurrentShot);
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(AccumulationSettings);
	check(CameraSettings);
	check(HighResSettings);
	check(OutputSettings);

	FIntPoint TileCount = FIntPoint(HighResSettings->TileCount, HighResSettings->TileCount);
	FIntPoint OutputResolution = OutputSettings->OutputResolution;

	int32 NumSpatialSamples = AccumulationSettings->SpatialSampleCount;
	int32 NumTemporalSamples = CameraSettings->TemporalSampleCount;
	ensure(TileCount.X > 0 && TileCount .Y> 0 && NumSpatialSamples > 0);

	FrameInfo.PrevViewLocation = FrameInfo.CurrViewLocation;
	FrameInfo.PrevViewRotation = FrameInfo.CurrViewRotation;

	APlayerController* LocalPlayerController = GetWorld()->GetFirstPlayerController();
	LocalPlayerController->GetPlayerViewPoint(FrameInfo.CurrViewLocation, FrameInfo.CurrViewRotation);

	// if it is the first frame, then motion blur is disabled, and we just use the current frame camera position/rotation
	if (bIsHistoryOnlyFrame)
	{
		FrameInfo.PrevViewLocation = FrameInfo.CurrViewLocation;
		FrameInfo.PrevViewRotation = FrameInfo.CurrViewRotation;
	}

	TArray<UMoviePipelineRenderPass*> InputBuffers = GetAllRenderPasses(GetPipelineMasterConfig(), CurrentShot);

	// If this is the first sample for a new frame, we want to notify the output builder that it should expect data to accumulate for this frame.
	if (CachedOutputState.TemporalSampleIndex == 0)
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
			/*
			* Two different features need to move the viewport and need to work in combination with each other.
			* Ultra-high resolution rendering and single-frame anti-aliasing both work by moving the viewport
			* by a small amount to create new samples for each render pass which generates more usable data.
			*
			* High resolution rendering is referred to as "tiles" but it is not actually a tile based render.
			* Splitting the view into many smaller rectangles and using an off-center camera projection on them
			* wouldn't work due to post-processing effects (such as vignette) as you would end up with many copies
			* of the effect on your final image. Instead, it renders the entire screen for every 'tile', but shifts
			* the camera position a portion of a pixel to cause new, unique data to be captured in the next tile.
			*
			* Single-frame anti-aliasing works in a similar way, by shifting the view by less than a pixel for each
			* sample in the anti-aliasing. While the high-resolution rendering is a specific grid pattern, the
			* anti-aliasing uses a more nuanced technique for picking offsets. It uses a deterministic Halton
			* sequence to pick the offset, weighted by a Gaussian curve to mostly focus on samples around the
			* center, but occasionally picking an offset which would be a sample outside of it's current sub-pixel.
			*
			* In the example below we're looking at one render target pixel, split into a 2x2 tiling. To gain
			* 2x resolution, we shift the viewport so that the center of the pixel is no longer 'o', but now
			* 'v'. Since this shift is less than a whole pixel we can interlace the resulting images and they
			* won't be duplicates-but-shifted-by-one of each other. This is how we end up with new unique data.
			*
			* |-------------|-------------|
			* |        a    |             |
			* |    a        |             |
			* |      v      |      v      |
			* |   a         |             |
			* |a       a    |             |
			* |-------------o-------------|
			* |             |             |
			* |             |             |
			* |      v      |      v      |
			* |             |             |
			* |             |             |
			* |-------------|-------------|
			*
			* Now that a given render target pixel |v| has been shifted to it's correct location, we need to randomly
			* sample within that box (perhaps with a slight overshoot) for the anti-aliasing samples. These samples
			* ('a') are added together to create one |v| pixel and then only the |v| pixels are interleaved.
			* Interleaving is performed on the CPU to support > 16k output sizes which is the maximum texture resolution
			* for most GPUs.
			*/

			// Calculate the sub-pixel shift to get this pass into the right portion of the main pixel. Assuming the
			// pixel coordinates are [0-1], the size of a sub-pixel (for tiles) is 1/NumTiles. To get to the right
			// 'quadrant' we need to take our current tile index and multiply it by the size of each tile, so
			// for the top right quadrant (in above example) size=(1/2), offset=(size*1). Finally we add half
			// of the size to put us in the center of that pixel.
			double TilePixelSizeFractionX;
			double TilePixelSizeFractionY; 
			double TileShiftX; 
			double TileShiftY;

			{
				TileShiftX = 0.5f;
				TileShiftY = 0.5f;

				TilePixelSizeFractionX = 1.0f;
				TilePixelSizeFractionY = 1.0f;
			}

			// If they're trying to seed the histories, then we'll just override the NumSpatialSamples since they don't need all samples as
			// this isn't output.
			if (bIsHistoryOnlyFrame)
			{
				NumSpatialSamples = 1;
			}

			// Now we want to render a user-configured number of spatial jitters to come up with the final output for this tile. 
			for (int32 SpatialSample = 0; SpatialSample < NumSpatialSamples; SpatialSample++)
			{
				// Count this as a sample rendered for the current work.
				CurrentCameraCut.CurrentWorkInfo.NumSamples++;

				float OffsetX = 0.5f;
				float OffsetY = 0.5f; 

				// Only jitter when required, as jitter requires TAA to be off. 
				// Also, we want jitter if either time samples or spatial samples are > 1, otherwise not.
				if (!bIsHistoryOnlyFrame &&
					(NumSpatialSamples > 1 || NumTemporalSamples > 1))
				{
					OffsetX = Halton((SpatialSample + 1) + (CachedOutputState.TemporalSampleIndex * NumSpatialSamples), 2);
					OffsetY = Halton((SpatialSample + 1) + (CachedOutputState.TemporalSampleIndex * NumSpatialSamples), 3);
				}

				// Our spatial sample can only move within a +/- range within the tile we're in.
				double SpatialShiftX = FMath::GetMappedRangeValueClamped(FVector2D(0, 1), FVector2D(-TilePixelSizeFractionX / 2.f, TilePixelSizeFractionX / 2.f), OffsetX);
				double SpatialShiftY = FMath::GetMappedRangeValueClamped(FVector2D(0, 1), FVector2D(-TilePixelSizeFractionY / 2.f, TilePixelSizeFractionY / 2.f), OffsetY);

				double FinalSubPixelShiftX = - (TileShiftX + SpatialShiftX - 0.5);
				double FinalSubPixelShiftY = (TileShiftY + SpatialShiftY - 0.5);

				FIntPoint BackbufferResolution = OutputSettings->OutputResolution;
				FIntPoint TileResolution = BackbufferResolution;

				{
					BackbufferResolution = FIntPoint(
						FMath::CeilToInt(BackbufferResolution.X / TileCount.X),
						FMath::CeilToInt(BackbufferResolution.Y / TileCount.Y));

					// Cache tile resolution before we add padding.
					TileResolution = BackbufferResolution;

					// Apply size padding.
					BackbufferResolution = HighResSettings->CalculatePaddedBackbufferSize(BackbufferResolution);
				}

				// We take all of the information needed to render a single sample and package it into a struct.
				FMoviePipelineRenderPassMetrics SampleState;
				SampleState.OutputState = CachedOutputState;
				SampleState.SpatialShift = FVector2D((float)( FinalSubPixelShiftX) * 2.0f / BackbufferResolution.X, (float)FinalSubPixelShiftY * 2.0f / BackbufferResolution.X);
				SampleState.TileIndexes = FIntPoint(TileX, TileY);
				SampleState.TileCounts = TileCount;
				SampleState.bIsHistoryOnlyFrame = bIsHistoryOnlyFrame;
				SampleState.SpatialSampleIndex = SpatialSample;
				SampleState.SpatialSampleCount = NumSpatialSamples;
				SampleState.TemporalSampleIndex = CachedOutputState.TemporalSampleIndex;
				SampleState.TemporalSampleCount = CameraSettings->TemporalSampleCount;
				SampleState.AccumulationGamma = AccumulationSettings->AccumulationGamma;
				SampleState.BackbufferSize = BackbufferResolution;
				SampleState.TileSize = TileResolution;
				SampleState.FrameInfo = FrameInfo;
				SampleState.bWriteSampleToDisk = HighResSettings->bWriteAllSamples;
				SampleState.ExposureCompensation = CameraSettings->bManualExposure ? CameraSettings->ExposureCompensation : TOptional<float>();
				SampleState.TextureSharpnessBias = HighResSettings->TextureSharpnessBias;
				{
					SampleState.OverlappedPad = FIntPoint(FMath::CeilToInt(TileResolution.X * HighResSettings->OverlapPercentage), 
														   FMath::CeilToInt(TileResolution.Y * HighResSettings->OverlapPercentage));
					SampleState.OverlappedOffset = FIntPoint(TileX * TileResolution.X - SampleState.OverlappedPad.X,
															  TileY * TileResolution.Y - SampleState.OverlappedPad.Y);
					SampleState.OverlappedSubpixelShift = FVector2D(1.0f - OffsetX,OffsetY);
				}

				SampleState.WeightFunctionX.InitHelper(SampleState.OverlappedPad.X, SampleState.TileSize.X, SampleState.OverlappedPad.X);
				SampleState.WeightFunctionY.InitHelper(SampleState.OverlappedPad.Y, SampleState.TileSize.Y, SampleState.OverlappedPad.Y);

				// Now we can request that all of the engine passes render. The individual render passes should have already registered delegates
				// to receive data when the engine render pass is run, so no need to run them.
				for (TSharedPtr<MoviePipeline::FMoviePipelineEnginePass> EnginePass : ActiveRenderPasses)
				{
					EnginePass->RenderSample_GameThread(SampleState);
				}
			}
		}
	}
	
	// UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] Pre-FlushRenderingCommands"), GFrameCounter);
	FlushRenderingCommands();
	// UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] Post-FlushRenderingCommands"), GFrameCounter);
}

void UMoviePipeline::OnFrameCompletelyRendered(FMoviePipelineMergerOutputFrame&& OutputFrame, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData)
{
	UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] Data required for output available! Frame: %d"), GFrameCounter, OutputFrame.FrameOutputState.OutputFrameNumber);
	
	for (UMoviePipelineOutputBase* OutputContainer : GetPipelineMasterConfig()->GetOutputContainers())
	{
		OutputContainer->OnRecieveImageDataImpl(&OutputFrame);
	}
}

void UMoviePipeline::OnSampleRendered(TUniquePtr<FImagePixelData>&& OutputSample, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData)
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();

	// Fill alpha for now 
	/*switch (OutputSample->GetType())
	{
		case EImagePixelType::Color:
		{
			TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
			break;
		}
		case EImagePixelType::Float16:
		{

			break;
		}
		case EImagePixelType::Float32:
		{
			TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FLinearColor>(255));
			break;
		}
		default:
			check(false);
	}*/

	// JPEG output
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
