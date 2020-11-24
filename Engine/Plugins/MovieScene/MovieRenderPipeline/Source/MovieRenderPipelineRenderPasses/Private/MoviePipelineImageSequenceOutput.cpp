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
#include "MoviePipelineBurnInSetting.h"
#include "Containers/UnrealString.h"
#include "Misc/StringFormatArg.h"
#include "MoviePipelineOutputBase.h"
#include "MoviePipelineImageQuantization.h"


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


void UMoviePipelineImageSequenceOutputBase::OnReceiveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame)
{
	SCOPE_CYCLE_COUNTER(STAT_ImgSeqRecieveImageData);

	check(InMergedOutputFrame);

	UMoviePipelineBurnInSetting* BurnInSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineBurnInSetting>();
	bool bCompositeBurnInOntoFinalImage = BurnInSettings ? BurnInSettings->bCompositeOntoFinalImage : false;

	// We do a little special handling for Burn In overlays if we are compositing them on top of the main image, otherwise we treat them as normal passes
	TUniquePtr<FImagePixelData> BurnInImageData = nullptr;
	if (bCompositeBurnInOntoFinalImage)
	{
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
	}

	UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	UMoviePipelineColorSetting* ColorSetting = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineColorSetting>();

	FString OutputDirectory = OutputSettings->OutputDirectory.Path;

	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		// Don't write out the burn in pass in this loop if it is being composited on the final image
		if (bCompositeBurnInOntoFinalImage && RenderPassData.Key == FMoviePipelinePassIdentifier(TEXT("BurnInOverlay")))
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
			QuantizedPixelData = UE::MoviePipeline::QuantizeImagePixelDataToBitDepth(RenderPassData.Value.Get(), 8, nullptr, !(ColorSetting && ColorSetting->OCIOConfiguration.bIsEnabled));
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
		FString FinalImageSequenceFileName;
		FString ClipName;
		{
			FString FileNameFormatString = OutputSettings->FileNameFormat;

			// If we're writing more than one render pass out, we need to ensure the file name has the format string in it so we don't
			// overwrite the same file multiple times. Burn In overlays don't count if they are getting composited on top of an existing file.
			const bool bIncludeRenderPass = InMergedOutputFrame->ImageOutputData.Num() - (BurnInImageData ? 1 : 0) > 1;
			const bool bTestFrameNumber = true;

			UE::MoviePipeline::ValidateOutputFormatString(FileNameFormatString, bIncludeRenderPass, bTestFrameNumber);

			// Create specific data that needs to override 
			FStringFormatNamedArguments FormatOverrides;
			FormatOverrides.Add(TEXT("render_pass"), RenderPassData.Key.Name);
			FormatOverrides.Add(TEXT("ext"), Extension);

			// We don't support metadata on the generic file writing.
			FMoviePipelineFormatArgs FinalFormatArgs;
			GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, FinalImageSequenceFileName, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState, -InMergedOutputFrame->FrameOutputState.ShotOutputFrameNumber);
			
			FString FilePathFormatString = OutputDirectory / FileNameFormatString;
			GetPipeline()->ResolveFilenameFormatArguments(FilePathFormatString, FormatOverrides, FinalFilePath, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState);

			// Create a deterministic clipname by removing frame numbers, file extension, and any trailing .'s
			UE::MoviePipeline::RemoveFrameNumberFormatStrings(FileNameFormatString, true);
			GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, ClipName, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState);
			ClipName.RemoveFromEnd(Extension);
			ClipName.RemoveFromEnd(".");
		}

		TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();
		TileImageTask->Format = PreferredOutputFormat;
		TileImageTask->CompressionQuality = 100;
		TileImageTask->Filename = FinalFilePath;

		// We composite before flipping the alpha so that it is consistent for all formats.
		if (BurnInImageData && RenderPassData.Key == FMoviePipelinePassIdentifier(TEXT("FinalImage")))
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


		TileImageTask->PixelData = MoveTemp(QuantizedPixelData);
		
#if WITH_EDITOR
		GetPipeline()->AddFrameToOutputMetadata(ClipName, FinalImageSequenceFileName, InMergedOutputFrame->FrameOutputState, Extension, Payload->bRequireTransparentOutput);
#endif
		GetPipeline()->AddOutputFuture(ImageWriteQueue->Enqueue(MoveTemp(TileImageTask)));
	}
}


void UMoviePipelineImageSequenceOutputBase::GetFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const
{
	// Stub in a dummy extension (so people know it exists)
	// InOutFormatArgs.Arguments.Add(TEXT("ext"), TEXT("jpg/png/exr")); Hidden since we just always post-pend with an extension.
	InOutFormatArgs.FilenameArguments.Add(TEXT("render_pass"), TEXT("RenderPassName"));
}

