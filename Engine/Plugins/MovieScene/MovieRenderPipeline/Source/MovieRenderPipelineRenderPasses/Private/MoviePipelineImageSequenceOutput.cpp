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


UMoviePipelineImageSequenceOutputBase::UMoviePipelineImageSequenceOutputBase()
{
	// ToDo: Move this to a specific function to get out of CDO land.
	ImageWriteQueue = &FModuleManager::Get().LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();
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
		TUniquePtr<FImagePixelData> QuantizedPixelData = nullptr;
		FIntPoint RawSize = RenderPassData.Value->GetSize();
		int32 RawNumChannels = RenderPassData.Value->GetNumChannels();

		
		switch (OutputFormat)
		{
		case EImageFormat::PNG:
		case EImageFormat::JPEG:
		case EImageFormat::BMP:
		{
			// All three of these formats only support 8 bit data, so we need to take the incoming buffer type,
			// copy it into a new 8-bit array and optionally apply a little noise to the data to help hide gradient banding.
			switch (RenderPassData.Value->GetBitDepth())
			{
				case 8:
				{
					// No work actually needs to be done, hooray! We'll copy the data though so that when it gets moved
					// into the ImageWriteTask the original data can be passed to another output container.
					QuantizedPixelData = RenderPassData.Value->CopyImageData();
					break;
				}
				case 16:
				{
					TArray<FColor> ClampedPixels;
					ClampedPixels.SetNum(RenderPassData.Value->GetSize().X * RenderPassData.Value->GetSize().Y);

					int64 SizeInBytes = 0;
					const void* SrcRawDataPtr = nullptr;
					RenderPassData.Value->GetRawData(SrcRawDataPtr, SizeInBytes);

					const uint16* RawDataPtr = static_cast<const uint16*>(SrcRawDataPtr);

					// Copy pixels to new array
					for (int32 Y = 0; Y < RawSize.Y; Y++)
					{
						for (int32 X = 0; X < RawSize.X; X++)
						{
							FColor* DestColor = &ClampedPixels.GetData()[Y*RawSize.X + X];
							for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
							{
								FFloat16 RawValue;
								RawValue.Encoded = RawDataPtr[(Y*RawSize.X + X)*RawNumChannels + ChanIter];

								// c cast does the conversion from fp16 bits to float
								uint8 Value = FMath::Clamp(FMath::FloorToInt(float(RawValue)/255), 0, 1);
								switch (ChanIter)
								{
								case 0: DestColor->R = Value; break;
								case 1: DestColor->G = Value; break;
								case 2: DestColor->B = Value; break;
								case 3: DestColor->A = Value; break;
								}
							}
						}
					}

					QuantizedPixelData = MakeUnique<TImagePixelData<FColor>>(RenderPassData.Value->GetSize(), TArray64<FColor>(MoveTemp(ClampedPixels)));
				}
			case 32:
			{
				TArray<FColor> ClampedPixels;
				ClampedPixels.SetNum(RenderPassData.Value->GetSize().X * RenderPassData.Value->GetSize().Y);

				int64 SizeInBytes = 0;
				const void* SrcRawDataPtr = nullptr;
				RenderPassData.Value->GetRawData(SrcRawDataPtr, SizeInBytes);

				const float* RawDataPtr = static_cast<const float*>(SrcRawDataPtr);

				// Copy pixels to new array
				for (int32 Y = 0; Y < RawSize.Y; Y++)
				{
					for (int32 X = 0; X < RawSize.X; X++)
					{
						FColor* DestColor = &ClampedPixels.GetData()[Y*RawSize.X + X];
						for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
						{
							float RawValue = RawDataPtr[(Y*RawSize.X + X)*RawNumChannels + ChanIter];
							RawValue = FMath::Pow(RawValue, 1/2.2f);

							// c cast does the conversion from fp16 bits to float
							uint8 Value = FMath::Clamp(FMath::RoundToInt(float(RawValue) * 255), 0, 255);
							switch (ChanIter)
							{
							case 0: DestColor->R = Value; break;
							case 1: DestColor->G = Value; break;
							case 2: DestColor->B = Value; break;
							case 3: DestColor->A = Value; break;
							}
						}
					}
				}

				QuantizedPixelData = MakeUnique<TImagePixelData<FColor>>(RenderPassData.Value->GetSize(), TArray64<FColor>(MoveTemp(ClampedPixels)));
				break;
			}

			default:
				check(false);
			}

			break;
		}
		case EImageFormat::EXR:
			QuantizedPixelData = RenderPassData.Value->CopyImageData();
			break;
		default:
			check(false);
		}

		TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();

		// Fill alpha for now 
		// switch (RenderPassData.Value->GetType())
		// {
		// case EImagePixelType::Color:
		// {
		// 	TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
		// 	break;
		// }
		// case EImagePixelType::Float16:
		// {
		// 
		// 	break;
		// }
		// case EImagePixelType::Float32:
		// {
		// 	TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FLinearColor>(255));
		// 	break;
		// }
		// default:
		// 	check(false);
		// }

		TileImageTask->Format = OutputFormat;
		TileImageTask->CompressionQuality = 100;

		FString OutputName = FString::Printf(TEXT("/%s.%d.%s"),
			*RenderPassData.Key.Name, InMergedOutputFrame->FrameOutputState.OutputFrameNumber, Extension);

		FString OutputPath = OutputDirectory + OutputName;
		TileImageTask->Filename = OutputPath;

		TileImageTask->PixelData = MoveTemp(QuantizedPixelData);
		ImageWriteQueue->Enqueue(MoveTemp(TileImageTask));
	}
}
