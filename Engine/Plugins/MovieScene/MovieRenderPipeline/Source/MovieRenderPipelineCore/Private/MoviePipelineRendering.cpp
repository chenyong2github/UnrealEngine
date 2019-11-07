// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	UMoviePipelineAccumulationSetting* AccumulationSettings = Shot.ShotConfig->FindOrAddSetting<UMoviePipelineAccumulationSetting>();
	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	FMoviePipelineRenderPassInitSettings RenderPassInitSettings;
	RenderPassInitSettings.TileResolution = FIntPoint(
		FMath::CeilToInt(OutputSettings->OutputResolution.X / AccumulationSettings->TileCount),
		FMath::CeilToInt(OutputSettings->OutputResolution.Y / AccumulationSettings->TileCount));
	RenderPassInitSettings.TileCount = AccumulationSettings->TileCount;
	RenderPassInitSettings.ShotConfig = Shot.ShotConfig;

	if (AccumulationSettings->bIsUsingOverlappedTiles)
	{
		// this code is duplicated in a few places, we should clean this up

		int32 OverlappedPadX = int32(float(RenderPassInitSettings.TileResolution.X) * AccumulationSettings->PadRatioX);
		int32 OverlappedPadY = int32(float(RenderPassInitSettings.TileResolution.Y) * AccumulationSettings->PadRatioY);
		RenderPassInitSettings.TileResolution.X += 2 * OverlappedPadX;
		RenderPassInitSettings.TileResolution.Y += 2 * OverlappedPadY;
	}



	// Code expects at least a 1x1 tile.
	ensure(RenderPassInitSettings.TileCount > 0);

	for (UMoviePipelineRenderPass* Input : Shot.ShotConfig->GetRenderPasses())
	{
		Input->Setup(RenderPassInitSettings);
	}
}

void UMoviePipeline::TeardownRenderingPipelineForShot(FMoviePipelineShotInfo& Shot)
{
	for (UMoviePipelineRenderPass* Input : Shot.ShotConfig->GetRenderPasses())
	{
		Input->Teardown();
	}
}

void UMoviePipeline::RenderFrame()
{
	// Flush built in systems before we render anything. This maximizes the likelyhood that the data is prepared for when
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

	// Allow our containers to think
	// ToDo: Is this necessaary?
	// for (UMoviePipelineOutputBase* Container : Config->OutputContainers)
	// {
	// 	Container->OnPostTick();
	// }
	
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
	UMoviePipelineAccumulationSetting* AccumulationSettings = CurrentShot.ShotConfig->FindOrAddSetting<UMoviePipelineAccumulationSetting>();
	UMoviePipelineOutputSetting* OutputSetting = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSetting);


	const int32 NumTilesX = AccumulationSettings->TileCount;
	const int32 NumTilesY = NumTilesX;
	const bool bIsUsingOverlappedTiles = AccumulationSettings->bIsUsingOverlappedTiles;

	int32 NumSpatialSamples = AccumulationSettings->SpatialSampleCount;
	ensure(NumTilesX > 0 && NumTilesY > 0 && NumSpatialSamples > 0);

	TArray<UMoviePipelineRenderPass*> InputBuffers = CurrentShot.ShotConfig->GetRenderPasses();

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

	// Each render has been initialized with ResX/NumTiles, ResY/NumTiles already to know how big each resulting render target should be.
	for (int32 TileY = 0; TileY < NumTilesY; TileY++)
	{
		for (int32 TileX = 0; TileX < NumTilesX; TileX++)
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
			* sequence to pick the offset, weighted by a guassian curve to mostly focus on samples around the
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
			double TilePixelSizeFractionX = 1.0 / NumTilesX;
			double TilePixelSizeFractionY = 1.0 / NumTilesY;
			double TileShiftX = (TilePixelSizeFractionX * TileX) + (TilePixelSizeFractionX / 2.0);
			double TileShiftY = (TilePixelSizeFractionY * TileY) + (TilePixelSizeFractionY / 2.0);

			if (bIsUsingOverlappedTiles)
			{
				TileShiftX = 0.5f;
				TileShiftY = 0.5f;

				TilePixelSizeFractionX = 1.0f;
				TilePixelSizeFractionY = 1.0f;
			}

			// If they're trying to seed the histories, then we'll just override the NumSpatialSamples since they're contributing towards
			// our final target buffer which needs the history.
			if (bIsHistoryOnlyFrame)
			{
				NumSpatialSamples = 1;
			}

			// Now we want to render a user-configured number of jitters to come up with the final output for this tile. These are considered
			// spatial jitters which are accumulated together before contributing to the temporal accumulation.
			for (int32 SpatialSample = 0; SpatialSample < NumSpatialSamples; SpatialSample++)
			{
				// Count this as a sample rendered for the current work.
				CurrentCameraCut.CurrentWorkInfo.NumSamples++;

				float OffsetX = 0.5f;
				float OffsetY = 0.5f; 

				// CachedOutputState.TemporalSampleIndex is -1 on the first frame for motion blur, so ignore jitter on this frame.
				// Also, we want jitter if either time samples or spatial samples are > 1, otherwise not.
				if (CachedOutputState.TemporalSampleIndex >= 0 &&
					(AccumulationSettings->SpatialSampleCount > 1 || AccumulationSettings->TemporalSampleCount > 1))
				{
					OffsetX = Halton((SpatialSample + 1) + (CachedOutputState.TemporalSampleIndex * NumSpatialSamples), 2);
					OffsetY = Halton((SpatialSample + 1) + (CachedOutputState.TemporalSampleIndex * NumSpatialSamples), 3);
				}

				// Our spatial sample can only move within a +/- range within the tile we're in.
				double SpatialShiftX = FMath::GetMappedRangeValueClamped(FVector2D(0, 1), FVector2D(-TilePixelSizeFractionX / 2.f, TilePixelSizeFractionX / 2.f), OffsetX);
				double SpatialShiftY = FMath::GetMappedRangeValueClamped(FVector2D(0, 1), FVector2D(-TilePixelSizeFractionY / 2.f, TilePixelSizeFractionY / 2.f), OffsetY);

				double FinalSubPixelShiftX = - (TileShiftX + SpatialShiftX - 0.5);
				double FinalSubPixelShiftY = (TileShiftY + SpatialShiftY - 0.5);

				int32 TileSizeX = FMath::CeilToInt(OutputSetting->OutputResolution.X / NumTilesX);
				int32 TileSizeY = FMath::CeilToInt(OutputSetting->OutputResolution.Y / NumTilesY);

				// in the case of overlapped rendering, the render target size will be larger than tile size because of overlap
				int32 PadX = bIsUsingOverlappedTiles ? int32(float(TileSizeX) * AccumulationSettings->PadRatioX) : 0;
				int32 PadY = bIsUsingOverlappedTiles ? int32(float(TileSizeY) * AccumulationSettings->PadRatioY) : 0;

				int TargetSizeX = TileSizeX + 2 * PadX;
				int TargetSizeY = TileSizeY + 2 * PadY;

				FMoviePipelineRenderPassMetrics FrameMetrics;
				FrameMetrics.OutputState = CachedOutputState;
				FrameMetrics.TileIndex = (TileY * NumTilesY) + TileX;
				FrameMetrics.SpatialShift = FVector2D((float)( FinalSubPixelShiftX) * 2.0f / TargetSizeX, (float)FinalSubPixelShiftY * 2.0f / TargetSizeX);
				FrameMetrics.TileIndexX = TileX;
				FrameMetrics.TileIndexY = TileY;
				FrameMetrics.NumTilesX = NumTilesX;
				FrameMetrics.NumTilesY = NumTilesY;
				FrameMetrics.bIsHistoryOnlyFrame = bIsHistoryOnlyFrame;
				FrameMetrics.SpatialJitterIndex = SpatialSample;
				FrameMetrics.NumSpatialJitters = NumSpatialSamples;
				FrameMetrics.TemporalJitterIndex = CachedOutputState.TemporalSampleIndex;
				FrameMetrics.NumTemporalJitters = AccumulationSettings->TemporalSampleCount;
				FrameMetrics.AccumulationGamma = AccumulationSettings->AccumulationGamma;
				FrameMetrics.JitterOffsetX = 1.0f - OffsetX; // reversing to match FinalSubPixelShiftX
				FrameMetrics.JitterOffsetY = OffsetY;
				FrameMetrics.bIsUsingOverlappedTiles = bIsUsingOverlappedTiles;

				if (bIsUsingOverlappedTiles)
				{
					FrameMetrics.OverlappedPadX = int32(float(TileSizeX) * AccumulationSettings->PadRatioX);
					FrameMetrics.OverlappedPadY = int32(float(TileSizeY) * AccumulationSettings->PadRatioY);
					FrameMetrics.OverlappedOffsetX = TileX * TileSizeX - FrameMetrics.OverlappedPadX;
					FrameMetrics.OverlappedOffsetY = TileY * TileSizeY - FrameMetrics.OverlappedPadY;
					FrameMetrics.OverlappedSizeX = TileSizeX;
					FrameMetrics.OverlappedSizeY = TileSizeY;
					FrameMetrics.OverlappedSubpixelShift = FVector2D(1.0f - OffsetX,OffsetY);
				}
				else
				{
					FrameMetrics.OverlappedOffsetX = 0;
					FrameMetrics.OverlappedOffsetY = 0;
					FrameMetrics.OverlappedSizeX = 0;
					FrameMetrics.OverlappedSizeY = 0;
					FrameMetrics.OverlappedPadX = 0;
					FrameMetrics.OverlappedPadY = 0;
					FrameMetrics.OverlappedSubpixelShift = FVector2D(0.0f,0.0f);
				}
				// Finally, we can ask each pass to render. Passes have already been initialized knowing how many tiles they're broken into
				for (UMoviePipelineRenderPass* Input : InputBuffers)
				{
					// Our Cached Output state has the expected output for this frame.
					Input->CaptureFrame(FrameMetrics);

					// If we needed to see each individual jittered image, we would need to
					// enqueue a copy of the pass's render target here. ToDo:

					// We can calculate the influence of the results of this pass before accumulating
					// If we are accumulating to a dedicated target for sub-frame jitter results, then
					// the influence of this pass is simply AccumTarget += ThisResult * (1/NumSpatialSamples);
					// However, to minimize memory usage we want to use a single accumulation target for
					// each pass on each tile. To simulate a camera shutter, different temporal sample frames
					// may have different weights (ie: the first and last sample may be slightly darker), and
					// in this case the influence of this pass is:
					// AccumTarget += (ThisResult * (1/NumSpatialSamples)) * GetWeightForTemporalSample(CachedOutputState.SubFrameIndex);
				}

			}
		}
	}
	
	// UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] Pre-FlushRenderingCommands"), GFrameCounter);
	FlushRenderingCommands();
	// UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] Post-FlushRenderingCommands"), GFrameCounter);

}

void UMoviePipeline::OnFrameCompletelyRendered(FMoviePipelineMergerOutputFrame&& OutputFrame)
{
	UE_LOG(LogMovieRenderPipeline, Warning, TEXT("[%d] Data required for output available! Frame: %d"), GFrameCounter, OutputFrame.FrameOutputState.OutputFrameNumber);

	for (UMoviePipelineOutputBase* OutputContainer : GetPipelineMasterConfig()->GetOutputContainers())
	{
		OutputContainer->OnRecieveImageDataImpl(&OutputFrame);
	}
}

void UMoviePipeline::OnSampleRendered(TUniquePtr<FImagePixelData>&& OutputSample)
{
	FImagePixelDataPayload* Payload = OutputSample->GetPayload< FImagePixelDataPayload>();
	check(Payload);

	UMoviePipelineOutputSetting* OutputSettings = GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();

	// Fill alpha for now 
	TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));

	// JPEG output
	TileImageTask->Format = EImageFormat::JPEG;
	TileImageTask->CompressionQuality = 100;

	FString OutputName = FString::Printf(TEXT("/%s_SS_%d_TS_%d_TileX_%d_TileY_%d.%d.jpeg"),
		*Payload->PassIdentifier.Name, Payload->SpatialJitterIndex, Payload->TemporalJitterIndex,
		Payload->TileIndexX, Payload->TileIndexY, Payload->OutputState.OutputFrameNumber);

	FString OutputDirectory = OutputSettings->OutputDirectory.Path;
	FString OutputPath = OutputDirectory + OutputName;
	TileImageTask->Filename = OutputPath;

	// Duplicate the data so that the Image Task can own it.
	TileImageTask->PixelData = MoveTemp(OutputSample);
	ImageWriteQueue->Enqueue(MoveTemp(TileImageTask));
}
