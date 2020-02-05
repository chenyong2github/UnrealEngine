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

// Forward Declare
static void ValidateOutputFormatString(FString& InOutFilenameFormatString, const bool bIncludeRenderPass);
static TUniquePtr<FImagePixelData> QuantizePixelDataTo8bpp(FImagePixelData* InPixelData);


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

	const TCHAR* Extension = TEXT("");
	switch (OutputFormat)
	{
	case EImageFormat::PNG: Extension = TEXT("png"); break;
	case EImageFormat::JPEG: Extension = TEXT("jpeg"); break;
	case EImageFormat::BMP: Extension = TEXT("bmp"); break;
	case EImageFormat::EXR: Extension = TEXT("exr"); break;
	}


	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		// Don't write out the burn in pass in this loop, it will get handled separately (or composited).
		if (RenderPassData.Key == FMoviePipelinePassIdentifier(TEXT("BurnInOverlay")))
		{
			continue;
		}

		TUniquePtr<FImagePixelData> QuantizedPixelData = nullptr;
		
		switch (OutputFormat)
		{
		case EImageFormat::PNG:
		case EImageFormat::JPEG:
		case EImageFormat::BMP:
		{
			// All three of these formats only support 8 bit data, so we need to take the incoming buffer type,
			// copy it into a new 8-bit array and optionally apply a little noise to the data to help hide gradient banding.
			QuantizedPixelData = QuantizePixelDataTo8bpp(RenderPassData.Value.Get());
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

			ValidateOutputFormatString(FileNameFormatString, bIncludeRenderPass);

			// Create specific data that needs to override 
			FStringFormatNamedArguments FormatOverrides;
			FormatOverrides.Add(TEXT("render_pass"), RenderPassData.Key.Name);
			FormatOverrides.Add(TEXT("ext"), Extension);

			FinalFilePath = GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, InMergedOutputFrame->FrameOutputState, FormatOverrides);
		}

		TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();
		TileImageTask->Format = OutputFormat;
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

static void ValidateOutputFormatString(FString& InOutFilenameFormatString, const bool bIncludeRenderPass)
{
	// If there is more than one file being written for this frame, make sure they uniquely identify.
	if (bIncludeRenderPass)
	{
		if (!InOutFilenameFormatString.Contains(TEXT("{render_pass}"), ESearchCase::IgnoreCase))
		{
			InOutFilenameFormatString += TEXT("{render_pass}");
		}
	}

	// Ensure there is a frame number in the output string somewhere to uniquely identify individual files in an image sequence.
	FString FrameNumberIdentifiers[] = { TEXT("{frame_number}"), TEXT("{frame_number_shot}"), TEXT("{frame_number_rel}"), TEXT("{frame_number_shot_rel}") };
	int32 FrameNumberIndex = INDEX_NONE;
	for (const FString& Identifier : FrameNumberIdentifiers)
	{
		FrameNumberIndex = InOutFilenameFormatString.Find(Identifier, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if(FrameNumberIndex != INDEX_NONE)
		{
			break;
		}
	}

	// We want to insert a {file_dup} before the frame number. This instructs the name resolver to put the (2) before
	// the frame number, so that they're still properly recognized as image sequences by other software. It will resolve
	// to "" if not needed.
	if (FrameNumberIndex == INDEX_NONE)
	{
		// Automatically append {frame_number} so the files are uniquely identified.
		InOutFilenameFormatString.Append(TEXT("{file_dup}.{frame_number}"));
	}
	else
	{
		// The user had already specified a frame number identifier, so we need to insert the
		// file_dup tag before it.
		InOutFilenameFormatString.InsertAt(FrameNumberIndex, TEXT("{file_dup}"));
	}
}

static TUniquePtr<FImagePixelData> QuantizePixelDataTo8bpp(FImagePixelData* InPixelData)
{
	TUniquePtr<FImagePixelData> QuantizedPixelData = nullptr;

	FIntPoint RawSize = InPixelData->GetSize();
	int32 RawNumChannels = InPixelData->GetNumChannels();

	// Look at our incoming bit depth
	switch (InPixelData->GetBitDepth())
	{
	case 8:
	{
		// No work actually needs to be done, hooray! We'll copy the data though so that when it gets moved
		// into the ImageWriteTask the original data can be passed to another output container.
		QuantizedPixelData = InPixelData->CopyImageData();
		break;
	}
	case 16:
	{
		TArray<FColor> ClampedPixels;
		ClampedPixels.SetNum(RawSize.X * RawSize.Y);

		int64 SizeInBytes = 0;
		const void* SrcRawDataPtr = nullptr;
		InPixelData->GetRawData(SrcRawDataPtr, SizeInBytes);

		const uint16* RawDataPtr = static_cast<const uint16*>(SrcRawDataPtr);

		// Copy pixels to new array
		for (int32 Y = 0; Y < RawSize.Y; Y++)
		{
			for (int32 X = 0; X < RawSize.X; X++)
			{
				FColor* DestColor = &ClampedPixels.GetData()[Y*RawSize.X + X];
				FLinearColor SrcColor;
				for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
				{
					FFloat16 Value;
					Value.Encoded = RawDataPtr[(Y*RawSize.X + X)*RawNumChannels + ChanIter];

					switch (ChanIter)
					{
					case 0: SrcColor.R = Value; break;
					case 1: SrcColor.G = Value; break;
					case 2: SrcColor.B = Value; break;
					case 3: SrcColor.A = Value; break;
					}
				}
				// convert to FColor using sRGB conversion
				*DestColor = SrcColor.ToFColor(true);
			}
		}

		QuantizedPixelData = MakeUnique<TImagePixelData<FColor>>(RawSize, TArray64<FColor>(MoveTemp(ClampedPixels)));
	}
	case 32:
	{
		TArray<FColor> ClampedPixels;
		ClampedPixels.SetNum(RawSize.X * RawSize.Y);

		int64 SizeInBytes = 0;
		const void* SrcRawDataPtr = nullptr;
		InPixelData->GetRawData(SrcRawDataPtr, SizeInBytes);

		const float* RawDataPtr = static_cast<const float*>(SrcRawDataPtr);

		// Copy pixels to new array
		for (int32 Y = 0; Y < RawSize.Y; Y++)
		{
			for (int32 X = 0; X < RawSize.X; X++)
			{
				FColor* DestColor = &ClampedPixels.GetData()[Y*RawSize.X + X];
				FLinearColor SrcColor;
				for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
				{
					float Value = RawDataPtr[(Y*RawSize.X + X)*RawNumChannels + ChanIter];

					switch (ChanIter)
					{
					case 0: SrcColor.R = Value; break;
					case 1: SrcColor.G = Value; break;
					case 2: SrcColor.B = Value; break;
					case 3: SrcColor.A = Value; break;
					}
				}
				// convert to FColor using sRGB conversion
				*DestColor = SrcColor.ToFColor(true);
			}
		}

		QuantizedPixelData = MakeUnique<TImagePixelData<FColor>>(RawSize, TArray64<FColor>(MoveTemp(ClampedPixels)));
		break;
	}

	default:
		check(false);
	}

	return QuantizedPixelData;
}