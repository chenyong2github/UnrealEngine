// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineImageSequenceOutput.h"
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
#include "Containers/UnrealString.h"
#include "Misc/StringFormatArg.h"
#include "MoviePipelineOutputBase.h"
#include "MoviePipelineImageQuantization.h"

// Forward Declare
static TUniquePtr<FImagePixelData> QuantizePixelDataTo8bpp(FImagePixelData* InPixelData);

DECLARE_CYCLE_STAT(TEXT("ImgSeqOutput_RecieveImageData"), STAT_ImgSeqRecieveImageData, STATGROUP_MoviePipeline);

UMoviePipelineImageSequenceOutputBase::UMoviePipelineImageSequenceOutputBase()
{
	if (!HasAnyFlags(RF_ArchetypeObject))
	{
		ImageWriteQueue = &FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();
	}
}

void UMoviePipelineImageSequenceOutputBase::BeginFinalizeImpl()
{
	FinalizeFence = ImageWriteQueue->CreateFence();
}

bool UMoviePipelineImageSequenceOutputBase::HasFinishedProcessingImpl()
{ 
	// Wait until the finalization fence is reached meaning we've written everything to disk.
	return Super::HasFinishedProcessingImpl() && (!FinalizeFence.IsValid() || FinalizeFence.WaitFor(0));
}


void UMoviePipelineImageSequenceOutputBase::OnRecieveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_ImgSeqRecieveImageData);

	check(InMergedOutputFrame);

	// We do a little special handling for Burn In overlays, because we need to composite them on top of the main image. We may also want to write them
	// to disk separately from the main file, which may then result in writing a separate image type (ie: jpegs must write burn in as a png for alpha support).
	TUniquePtr<FImagePixelData> BurnInImageData = nullptr;
	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		if (RenderPassData.Key == FMoviePipelinePassIdentifier(TEXT("BurnInOverlay")))
		{
			// Burn in data should always be 8 bit values, this is assumed later when we composite.
			check(RenderPassData.Value->GetType() == EImagePixelType::Color);
			BurnInImageData = RenderPassData.Value->CopyImageData();
			break;
		}
	}

	UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	FString OutputDirectory = OutputSettings->OutputDirectory.Path;

	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		// Don't write out the burn in pass in this loop, it will get handled separately (or composited).
		if (RenderPassData.Key == FMoviePipelinePassIdentifier(TEXT("BurnInOverlay")))
		{
			continue;
		}

		EImageFormat PreferredOutputFormat = OutputFormat;

		FImagePixelDataPayload* Payload = RenderPassData.Value->GetPayload<FImagePixelDataPayload>();

		// If the output requires a transparent output (to be useful) then we'll on a per-case basis override their intended
		// filetype to something that makes that file useful.
		if (Payload->bRequireTransparentOutput)
		{
			if (PreferredOutputFormat == EImageFormat::BMP ||
				PreferredOutputFormat == EImageFormat::JPEG)
			{
				PreferredOutputFormat = EImageFormat::PNG;
			}
		}

		const TCHAR* Extension = TEXT("");
		switch (PreferredOutputFormat)
		{
		case EImageFormat::PNG: Extension = TEXT("png"); break;
		case EImageFormat::JPEG: Extension = TEXT("jpeg"); break;
		case EImageFormat::BMP: Extension = TEXT("bmp"); break;
		case EImageFormat::EXR: Extension = TEXT("exr"); break;
		}

		TUniquePtr<FImagePixelData> QuantizedPixelData = nullptr;
		

		switch (PreferredOutputFormat)
		{
		case EImageFormat::PNG:
		case EImageFormat::JPEG:
		case EImageFormat::BMP:
		{
			// All three of these formats only support 8 bit data, so we need to take the incoming buffer type,
			// copy it into a new 8-bit array and optionally apply a little noise to the data to help hide gradient banding.
			QuantizedPixelData = UE::MoviePipeline::QuantizeImagePixelDataToBitDepth(RenderPassData.Value.Get(), 8);
			break;
		}
		case EImageFormat::EXR:
			// No quantization required, just copy the data as we will move it into the image write task.
			QuantizedPixelData = RenderPassData.Value->CopyImageData();
			break;
		default:
			check(false);
		}

		// We need to resolve the filename format string. We combine the folder and file name into one long string first
		FString FinalFilePath;
		{
			FString FileNameFormatString = OutputDirectory / OutputSettings->FileNameFormat;

			// If we're writing more than one render pass out, we need to ensure the file name has the format string in it so we don't
			// overwrite the same file multiple times. Burn In overlays don't count because they get composited on top of an existing file.
			const bool bIncludeRenderPass = InMergedOutputFrame->ImageOutputData.Num() - (BurnInImageData ? 1 : 0) > 1;
			const bool bTestFrameNumber = true;

			UE::MoviePipeline::ValidateOutputFormatString(FileNameFormatString, bIncludeRenderPass, bTestFrameNumber);

			// Create specific data that needs to override 
			FStringFormatNamedArguments FormatOverrides;
			FormatOverrides.Add(TEXT("render_pass"), RenderPassData.Key.Name);
			FormatOverrides.Add(TEXT("ext"), Extension);

			FinalFilePath = GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, InMergedOutputFrame->FrameOutputState, FormatOverrides);
		}

		TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();
		TileImageTask->Format = PreferredOutputFormat;
		TileImageTask->CompressionQuality = 100;
		TileImageTask->Filename = FinalFilePath;

		// We composite before flipping the alpha so that it is consistent for all formats.
		if (BurnInImageData && RenderPassData.Key == FMoviePipelinePassIdentifier(TEXT("Backbuffer")))
		{
			switch (QuantizedPixelData->GetType())
			{
			case EImagePixelType::Color:
				TileImageTask->PixelPreProcessors.Add(TAsyncCompositeImage<FColor>(BurnInImageData->CopyImageData()));
				break;
			case EImagePixelType::Float16:
				TileImageTask->PixelPreProcessors.Add(TAsyncCompositeImage<FFloat16Color>(BurnInImageData->CopyImageData()));
				break;
			case EImagePixelType::Float32:
				TileImageTask->PixelPreProcessors.Add(TAsyncCompositeImage<FLinearColor>(BurnInImageData->CopyImageData()));
				break;
			}
		}

		if (IsAlphaSupported())
		{
			// Flip the alpha channel output to match what PNG and other specifications expect.
			switch (QuantizedPixelData->GetType())
			{
			case EImagePixelType::Color:
				TileImageTask->PixelPreProcessors.Add(TAsyncAlphaInvert<FColor>());
				break;
			case EImagePixelType::Float16:
				TileImageTask->PixelPreProcessors.Add(TAsyncAlphaInvert<FFloat16Color>());
				break;
			case EImagePixelType::Float32:
				TileImageTask->PixelPreProcessors.Add(TAsyncAlphaInvert<FLinearColor>());
				break;
			}
		}
		// We don't flip these right now because we assume that it comes in with the correct Transparent vs. Opaque.
		else if(!Payload->bRequireTransparentOutput)
		{
			// Fill the alpha channel when alpha is not supported/enabled.
			switch (QuantizedPixelData->GetType())
			{
			case EImagePixelType::Color:
				TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
				break;
			case EImagePixelType::Float16:
				TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FFloat16Color>(1.f));
				break;
			case EImagePixelType::Float32:
				TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FLinearColor>(1.f));
				break;
			}
		}

		TileImageTask->PixelData = MoveTemp(QuantizedPixelData);
		ImageWriteQueue->Enqueue(MoveTemp(TileImageTask));
	}
}


void UMoviePipelineImageSequenceOutputBase::GetFilenameFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const
{
	// Stub in a dummy extension (so people know it exists)
	// InOutFormatArgs.Arguments.Add(TEXT("ext"), TEXT("jpg/png/exr")); Hidden since we just always post-pend with an extension.
	InOutFormatArgs.Arguments.Add(TEXT("render_pass"), TEXT("RenderPassName"));
}

