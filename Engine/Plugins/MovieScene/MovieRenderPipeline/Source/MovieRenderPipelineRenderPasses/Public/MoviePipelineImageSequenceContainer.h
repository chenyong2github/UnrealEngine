// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineOutput.h"
#include "Async/Future.h"
#include "MoviePipelineImageSequenceContainer.generated.h"

// Forward Declare
class IImageWriteQueue;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceContainerBase : public UMoviePipelineOutput
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceContainerBase();

protected:
	// UMovieRenderPipelineOutputContainer interface
	virtual void OnInitializedForPipelineImpl(UMoviePipeline* InPipeline);
	virtual void ProcessFrameImpl(TArray<MoviePipeline::FOutputFrameData> FrameData, FMoviePipelineFrameOutputState CachedOutputState, FDirectoryPath OutputDirectory) override;
	virtual void BeginFinalizeImpl() override;
	virtual bool HasFinishedProcessingImpl() override;
	// ~UMovieRenderPipelineOutputContainer interface
private:
	/** A pointer to the image write queue used for asynchronously writing images */
	IImageWriteQueue* ImageWriteQueue;

	/** A fence to keep track of when the Image Write queue has fully flushed. */
	TFuture<void> FinalizeFence;
};