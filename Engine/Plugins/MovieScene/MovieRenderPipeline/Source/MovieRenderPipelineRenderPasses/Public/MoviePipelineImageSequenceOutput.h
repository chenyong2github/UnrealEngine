// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineOutputBase.h"
#include "Async/Future.h"
#include "MoviePipelineImageSequenceOutput.generated.h"

// Forward Declare
class IImageWriteQueue;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutputBase : public UMoviePipelineOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceOutputBase();

	virtual void OnRecieveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame) override;

protected:
	// UMovieRenderPipelineOutputContainer interface
	virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline);
	virtual void BeginFinalizeImpl() override;
	virtual bool HasFinishedProcessingImpl() override;
	// ~UMovieRenderPipelineOutputContainer interface

protected:
	/** The format of the image to write out */
	EImageFormat OutputFormat;

private:
	/** A pointer to the image write queue used for asynchronously writing images */
	IImageWriteQueue* ImageWriteQueue;

	/** A fence to keep track of when the Image Write queue has fully flushed. */
	TFuture<void> FinalizeFence;
	FString OutputDirectory;
};

UCLASS(meta = (DisplayName = "Image Sequence (.bmp [8bpp])"))
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_BMP : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceOutput_BMP()
	{
		OutputFormat = EImageFormat::BMP;
	}
};

UCLASS(meta = (DisplayName = "Image Sequence (.png [8bpp])"))
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_PNG : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceOutput_PNG()
	{
		OutputFormat = EImageFormat::PNG;
	}
};

UCLASS(meta = (DisplayName = "Image Sequence (.jpg [8bpp])"))
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_JPG : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceOutput_JPG()
	{
		OutputFormat = EImageFormat::JPEG;
	}
};

UCLASS(meta = (DisplayName = "Image Sequence (.exr [32bpp])"))
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_EXR : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceOutput_EXR()
	{
		OutputFormat = EImageFormat::EXR;
	}
};