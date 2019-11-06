// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineOutputBase.h"
#include "Async/Future.h"
#include "MoviePipelineImageSequenceContainer.generated.h"

// Forward Declare
class IImageWriteQueue;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceContainerBase : public UMoviePipelineOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceContainerBase();

protected:
	// UMovieRenderPipelineOutputContainer interface
	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline);
	virtual void BeginFinalizeImpl() override;
	virtual bool HasFinishedProcessingImpl() override;
	// ~UMovieRenderPipelineOutputContainer interface
private:
	/** A pointer to the image write queue used for asynchronously writing images */
	IImageWriteQueue* ImageWriteQueue;

	/** A fence to keep track of when the Image Write queue has fully flushed. */
	TFuture<void> FinalizeFence;
};