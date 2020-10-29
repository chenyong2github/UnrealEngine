// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineOutputBase.h"
#include "Async/Future.h"
#include "MoviePipelineImageSequenceOutput.generated.h"

// Forward Declare
class IImageWriteQueue;

UCLASS(Blueprintable, Abstract)
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutputBase : public UMoviePipelineOutputBase
{
	GENERATED_BODY()
public:
	UMoviePipelineImageSequenceOutputBase();

	virtual void OnReceiveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame) override;

protected:
	// UMovieRenderPipelineOutputContainer interface
	virtual void BeginFinalizeImpl() override;
	virtual bool HasFinishedProcessingImpl() override;
	// ~UMovieRenderPipelineOutputContainer interface

	virtual void GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const override;
protected:
	/** The format of the image to write out */
	EImageFormat OutputFormat;

	/** A pointer to the image write queue used for asynchronously writing images */
	IImageWriteQueue* ImageWriteQueue;
private:

	/** A fence to keep track of when the Image Write queue has fully flushed. */
	TFuture<void> FinalizeFence;
};

UCLASS()
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_BMP : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "ImgSequenceBMPSettingDisplayName", ".bmp Sequence [8bit]"); }
#endif
public:
	UMoviePipelineImageSequenceOutput_BMP()
	{
		OutputFormat = EImageFormat::BMP;
	}
};

UCLASS()
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_PNG : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "ImgSequencePNGSettingDisplayName", ".png Sequence [8bit]"); }
#endif
public:
	UMoviePipelineImageSequenceOutput_PNG()
	{
		OutputFormat = EImageFormat::PNG;
	}
};

UCLASS()
class MOVIERENDERPIPELINERENDERPASSES_API UMoviePipelineImageSequenceOutput_JPG : public UMoviePipelineImageSequenceOutputBase
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "ImgSequenceJPGSettingDisplayName", ".jpg Sequence [8bit]"); }
#endif
public:
	UMoviePipelineImageSequenceOutput_JPG()
	{
		OutputFormat = EImageFormat::JPEG;
	}
};



/**
 * A pixel preprocessor for use with FImageWriteTask::PixelPreProcessor that does a simple alpha blend of the provided image onto the
 * target pixel data. This isn't very general purpose.
 */
template<typename PixelType> struct TAsyncCompositeImage;

template<>
struct TAsyncCompositeImage<FColor>
{
	TAsyncCompositeImage(TUniquePtr<FImagePixelData>&& InPixelData)
		: ImageToComposite(MoveTemp(InPixelData))
	{}

	void operator()(FImagePixelData* PixelData)
	{
		check(PixelData->GetType() == EImagePixelType::Color);
		check(ImageToComposite && ImageToComposite->GetType() == EImagePixelType::Color);
		if (!ensureMsgf(ImageToComposite->GetSize() == PixelData->GetSize(), TEXT("Cannot composite images of different sizes! Source: (%d,%d) Target: (%d,%d)"),
			ImageToComposite->GetSize().X, ImageToComposite->GetSize().Y, PixelData->GetSize().X, PixelData->GetSize().Y))
		{
			return;
		}


		TImagePixelData<FColor>* DestColorData = static_cast<TImagePixelData<FColor>*>(PixelData);
		TImagePixelData<FColor>* SrcColorData = static_cast<TImagePixelData<FColor>*>(ImageToComposite.Get());
		int64 NumPixels = DestColorData->GetSize().X * DestColorData->GetSize().Y;

		for (int64 Index = 0; Index < NumPixels; Index++)
		{
			FColor& Dst = DestColorData->Pixels[Index];
			FColor& Src = SrcColorData->Pixels[Index];
			
			float SourceAlpha = Src.A / 255.f;
			FColor Out;
			Out.A = FMath::Clamp(Src.A + FMath::RoundToInt(Dst.A * (1.f - SourceAlpha)), 0, 255);
			Out.R = FMath::Clamp(Src.R + FMath::RoundToInt(Dst.R * (1.f - SourceAlpha)), 0, 255);
			Out.G = FMath::Clamp(Src.G + FMath::RoundToInt(Dst.G * (1.f - SourceAlpha)), 0, 255);
			Out.B = FMath::Clamp(Src.B + FMath::RoundToInt(Dst.B * (1.f - SourceAlpha)), 0, 255);
			Dst = Out;
		}
	}

	TUniquePtr<FImagePixelData> ImageToComposite;
};

template<>
struct TAsyncCompositeImage<FFloat16Color>
{
	TAsyncCompositeImage(TUniquePtr<FImagePixelData>&& InPixelData)
		: ImageToComposite(MoveTemp(InPixelData))
	{}

	void operator()(FImagePixelData* PixelData)
	{
		check(PixelData->GetType() == EImagePixelType::Float16);
		check(ImageToComposite && ImageToComposite->GetType() == EImagePixelType::Color);
		if (!ensureMsgf(ImageToComposite->GetSize() == PixelData->GetSize(), TEXT("Cannot composite images of different sizes! Source: (%d,%d) Target: (%d,%d)"),
			ImageToComposite->GetSize().X, ImageToComposite->GetSize().Y, PixelData->GetSize().X, PixelData->GetSize().Y))
		{
			return;
		}

		TImagePixelData<FFloat16Color>* DestColorData = static_cast<TImagePixelData<FFloat16Color>*>(PixelData);
		TImagePixelData<FColor>* SrcColorData = static_cast<TImagePixelData<FColor>*>(ImageToComposite.Get());
		int64 NumPixels = DestColorData->GetSize().X * DestColorData->GetSize().Y;

		for (int64 Index = 0; Index < NumPixels; Index++)
		{
			FFloat16Color& Dst = DestColorData->Pixels[Index];
			FColor& Src = SrcColorData->Pixels[Index];

			float SourceAlpha = Src.A / 255.f;
			FFloat16Color Out;
			Out.A = (Src.A/255.f) + (Dst.A * (1.f - SourceAlpha));
			Out.R = (Src.R/255.f) + (Dst.R * (1.f - SourceAlpha));
			Out.G = (Src.G/255.f) + (Dst.G * (1.f - SourceAlpha));
			Out.B = (Src.B/255.f) + (Dst.B * (1.f - SourceAlpha));
			Dst = Out;
		}
	}

	TUniquePtr<FImagePixelData> ImageToComposite;
};

template<>
struct TAsyncCompositeImage<FLinearColor>
{
	TAsyncCompositeImage(TUniquePtr<FImagePixelData>&& InPixelData)
		: ImageToComposite(MoveTemp(InPixelData))
	{}

	void operator()(FImagePixelData* PixelData)
	{
		check(PixelData->GetType() == EImagePixelType::Float32);
		check(ImageToComposite && ImageToComposite->GetType() == EImagePixelType::Color);
		if (!ensureMsgf(ImageToComposite->GetSize() == PixelData->GetSize(), TEXT("Cannot composite images of different sizes! Source: (%d,%d) Target: (%d,%d)"),
			ImageToComposite->GetSize().X, ImageToComposite->GetSize().Y, PixelData->GetSize().X, PixelData->GetSize().Y))
		{
			return;
		}

		TImagePixelData<FLinearColor>* DestColorData = static_cast<TImagePixelData<FLinearColor>*>(PixelData);
		TImagePixelData<FColor>* SrcColorData = static_cast<TImagePixelData<FColor>*>(ImageToComposite.Get());
		int64 NumPixels = DestColorData->GetSize().X * DestColorData->GetSize().Y;

		for (int64 Index = 0; Index < NumPixels; Index++)
		{
			FLinearColor& Dst = DestColorData->Pixels[Index];
			FColor& Src = SrcColorData->Pixels[Index];

			float SourceAlpha = Src.A / 255.f;
			FLinearColor Out;
			Out.A = (Src.A/255.f) + (Dst.A * (1.f - SourceAlpha));
			Out.R = (Src.R/255.f) + (Dst.R * (1.f - SourceAlpha));
			Out.G = (Src.G/255.f) + (Dst.G * (1.f - SourceAlpha));
			Out.B = (Src.B/255.f) + (Dst.B * (1.f - SourceAlpha));
			Dst = Out;
		}
	}

	TUniquePtr<FImagePixelData> ImageToComposite;
};
