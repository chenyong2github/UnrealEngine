// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineImageSequenceContainer.h"
#include "ImageWriteTask.h"
#include "ImagePixelData.h"
#include "Modules/ModuleManager.h"
#include "ImageWriteQueue.h"
#include "MoviePipeline.h"
#include "ImageWriteStream.h"
#include "MoviePipelineMasterConfig.h"
#include "MovieRenderTileImage.h"
#include "MovieRenderOverlappedImage.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineOutputSetting.h"


UMoviePipelineImageSequenceContainerBase::UMoviePipelineImageSequenceContainerBase()
{
	// ToDo: Move this to a specific function to get out of CDO land.
	ImageWriteQueue = &FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();
}

void UMoviePipelineImageSequenceContainerBase::BeginFinalizeImpl()
{
	FinalizeFence = ImageWriteQueue->CreateFence();
}

bool UMoviePipelineImageSequenceContainerBase::HasFinishedProcessingImpl()
{ 
	// Wait until the finalization fence is reached meaning we've written everything to disk.
	return Super::HasFinishedProcessingImpl() && (!FinalizeFence.IsValid() || FinalizeFence.WaitFor(0));
}

void UMoviePipelineImageSequenceContainerBase::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	OutputDirectory = OutputSettings->OutputDirectory.Path;
}

void UMoviePipelineImageSequenceContainerBase::OnRecieveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame)
{
	check(InMergedOutputFrame);

	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();

		// Fill alpha for now 
		TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));

		// JPEG output
		TileImageTask->Format = EImageFormat::JPEG;
		TileImageTask->CompressionQuality = 100;

		FString OutputName = FString::Printf(TEXT("/%s.%d.jpeg"),
			*RenderPassData.Key.Name, InMergedOutputFrame->FrameOutputState.OutputFrameNumber);

		FString OutputPath = OutputDirectory + OutputName;
		TileImageTask->Filename = OutputPath;

		// Duplicate the data so that the Image Task can own it.
		TileImageTask->PixelData = MoveTemp(RenderPassData.Value);
		ImageWriteQueue->Enqueue(MoveTemp(TileImageTask));
	}
}