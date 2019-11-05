// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineImageSequenceContainer.h"
#include "ImageWriteTask.h"
#include "ImagePixelData.h"
#include "Modules/ModuleManager.h"
#include "ImageWriteQueue.h"
#include "MoviePipeline.h"
#include "ImageWriteStream.h"
#include "MovieRenderPipelineConfig.h"
#include "MovieRenderTileImage.h"
#include "MovieRenderOverlappedImage.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Misc/FrameRate.h"


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

static FImageTileAccumulator TestAccumulator;
static FImageOverlappedAccumulator TestOverlappedAccumulator;

void UMoviePipelineImageSequenceContainerBase::OnInitializedForPipelineImpl(UMoviePipeline* InPipeline)
{
	FString OutputDirectory = GetPipeline()->GetPipelineConfig()->OutputDirectory.Path;
	IImageWriteQueue* WriteQueue = ImageWriteQueue;

	FImageTileAccumulator* TileAccumulator = &TestAccumulator;
	FImageOverlappedAccumulator* OverlappedAccumulator = &TestOverlappedAccumulator;


	auto OnImageReceived = [OutputDirectory, WriteQueue, TileAccumulator, OverlappedAccumulator](TUniquePtr<FImagePixelData>&& InOwnedImage)
	{
		FImagePixelDataPayload* TilePayload = InOwnedImage->GetPayload<FImagePixelDataPayload>();

		// Init if first one, shut down if last one
		bool bIsFirstTile = (TilePayload->TileIndexX == 0 && TilePayload->TileIndexY == 0 && TilePayload->SpatialJitterIndex == 0);
		bool bIsLastTile =  (TilePayload->TileIndexX == TilePayload->NumTilesX-1  &&
							TilePayload->TileIndexY == TilePayload->NumTilesY-1 &&
							TilePayload->SpatialJitterIndex == TilePayload->NumSpatialJitters-1);
		bool bIsLastTemporalSample = TilePayload->TemporalJitterIndex == TilePayload->NumTemporalJitters - 1;
		bool bIsFirstTemporalSample = TilePayload->TemporalJitterIndex == 0;


		if (!TilePayload->bIsUsingOverlappedTiles)
		{
			if (bIsFirstTile && bIsFirstTemporalSample)
			{
				TileAccumulator->InitMemory(TilePayload->TileSizeX, TilePayload->TileSizeY, TilePayload->NumTilesX, TilePayload->NumTilesY, 3);
				TileAccumulator->ZeroPlanes();
				TileAccumulator->AccumulationGamma = TilePayload->AccumulationGamma;
			}

			const double AccumulateBeginTime = FPlatformTime::Seconds();
			TileAccumulator->AccumulatePixelData(*InOwnedImage.Get(), TilePayload->TileIndexX, TilePayload->TileIndexY, FVector2D(TilePayload->JitterOffsetX,TilePayload->JitterOffsetY));
			const double AccumulateEndTime = FPlatformTime::Seconds();
			const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime)*1000.0f);

		}
		else
		{
			if (bIsFirstTile && bIsFirstTemporalSample)
			{
				OverlappedAccumulator->InitMemory(FIntPoint(TilePayload->OverlappedSizeX * TilePayload->NumTilesX, TilePayload->OverlappedSizeY * TilePayload->NumTilesY), 3);
				OverlappedAccumulator->ZeroPlanes();
				OverlappedAccumulator->AccumulationGamma = TilePayload->AccumulationGamma;
			}

			FIntPoint RawSize = InOwnedImage.Get()->GetSize();

			check(TilePayload->OverlappedSizeX + 2 * TilePayload->OverlappedPadX == RawSize.X);
			check(TilePayload->OverlappedSizeY + 2 * TilePayload->OverlappedPadY == RawSize.Y);
			OverlappedAccumulator->AccumulatePixelData(*InOwnedImage.Get(), FIntPoint(TilePayload->OverlappedOffsetX, TilePayload->OverlappedOffsetY), TilePayload->OverlappedSubpixelShift);
		}

		//UE_LOG(LogTemp, Log, TEXT("[%8.2f] Accumulation time."), ElapsedMs);

		static bool bWriteTiles = true;
		if (bWriteTiles)
		{
			TUniquePtr<FImageWriteTask> TileImageTask = MakeUnique<FImageWriteTask>();
			TileImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
			// ImageTask->PixelPreProcessors.Add(TAsyncGammaCorrect<FColor>(2.2f));
			TileImageTask->Format = EImageFormat::JPEG;
			TileImageTask->CompressionQuality = 100;

			// ImageTask->CompressionQuality = GetCompressionQuality();
			FString OutputName = FString::Printf(TEXT("/%s_%d_SS_%d_TS_%d_TileX_%d_TileY_%d.jpeg"),
				*TilePayload->PassName, TilePayload->OutputState.OutputFrameNumber, TilePayload->SpatialJitterIndex, TilePayload->OutputState.TemporalSampleIndex,
				TilePayload->TileIndexX, TilePayload->TileIndexY);
			FString OutputPath = OutputDirectory + OutputName;
			TileImageTask->Filename = OutputPath;

			TileImageTask->PixelData = MoveTemp(InOwnedImage);

			WriteQueue->Enqueue(MoveTemp(TileImageTask));
		}

		if (bIsLastTile && bIsLastTemporalSample)
		{

			int32 FullSizeX = TilePayload->bIsUsingOverlappedTiles ? OverlappedAccumulator->PlaneSize.X : TileAccumulator->TileSizeX * TileAccumulator->NumTilesX;
			int32 FullSizeY = TilePayload->bIsUsingOverlappedTiles ? OverlappedAccumulator->PlaneSize.Y : TileAccumulator->TileSizeY * TileAccumulator->NumTilesY;

			{
				TUniquePtr<FImageWriteTask> ImageTask = MakeUnique<FImageWriteTask>();

				static bool bFullLinearColor = false; // Make this an option. For now, if it's linear, write EXR. Otherwise PNGs for 8bit.
				if (bFullLinearColor)
				{
					ImageTask->Format = EImageFormat::EXR;

					TUniquePtr<TImagePixelData<FLinearColor> > PixelData = MakeUnique<TImagePixelData<FLinearColor> >(FIntPoint(FullSizeX,FullSizeY));
					if (!TilePayload->bIsUsingOverlappedTiles)
					{
						TileAccumulator->FetchFinalPixelDataLinearColor(PixelData->Pixels);
					}
					else
					{
						OverlappedAccumulator->FetchFinalPixelDataLinearColor(PixelData->Pixels);
					}

					ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FLinearColor>(1.0f));
					ImageTask->PixelData = MoveTemp(PixelData);

					FString TempOutputName = FString::Printf(TEXT("/FINAL_%s_SS.%d.exr"),
						*TilePayload->PassName, TilePayload->OutputState.OutputFrameNumber);
					FString TempOutputPath = OutputDirectory + TempOutputName;
					ImageTask->Filename = TempOutputPath;
				}
				else
				{
					ImageTask->Format = EImageFormat::JPEG;

					// 8bit FColors
					TUniquePtr<TImagePixelData<FColor> > PixelData = MakeUnique<TImagePixelData<FColor> >(FIntPoint(FullSizeX,FullSizeY));
					if (!TilePayload->bIsUsingOverlappedTiles)
					{
						TileAccumulator->FetchFinalPixelDataByte(PixelData->Pixels);
					}
					else
					{
						OverlappedAccumulator->FetchFinalPixelDataByte(PixelData->Pixels);
					}

					ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
					ImageTask->PixelData = MoveTemp(PixelData);
					ImageTask->CompressionQuality = 100;

					FString TempOutputName = FString::Printf(TEXT("/FINAL_%s_SS.%d.jpeg"),
						*TilePayload->PassName, TilePayload->OutputState.OutputFrameNumber);
					FString TempOutputPath = OutputDirectory + TempOutputName;
					ImageTask->Filename = TempOutputPath;
				}

				WriteQueue->Enqueue(MoveTemp(ImageTask));
			}

			TileAccumulator->Reset();
		}
	};

	InPipeline->GetOutputPipe()->AddEndpoint(OnImageReceived);
}

void UMoviePipelineImageSequenceContainerBase::ProcessFrameImpl(TArray<MoviePipeline::FOutputFrameData> FrameData, FMoviePipelineFrameOutputState CachedOutputState, FDirectoryPath OutputDirectory)
{
	if (FrameData.Num() == 0)
	{
		return;
	}

	for (MoviePipeline::FOutputFrameData& Frame : FrameData)
	{
		if (Frame.Resolution.X <= 0 || Frame.Resolution.Y <= 0)
		{
			continue;
		}

		TUniquePtr<FImageWriteTask> ImageTask = MakeUnique<FImageWriteTask>();

		// Move the color buffer into a raw image data container that we can pass to the write queue
		ImageTask->PixelData = MakeUnique<TImagePixelData<FColor>>(Frame.Resolution, MoveTemp(Frame.ColorBuffer));
	
		// Always write full alpha for PNGs
		ImageTask->PixelPreProcessors.Add(TAsyncAlphaWrite<FColor>(255));
		ImageTask->Format = EImageFormat::PNG;
	
		// ImageTask->CompressionQuality = GetCompressionQuality();
	
		FString OutputName = FString::Printf(TEXT("/Frame_%s_%d.png"), *Frame.PassName, CachedOutputState.OutputFrameNumber);
		FString OutputPath = OutputDirectory.Path + OutputName;
		ImageTask->Filename = OutputPath;

		ImageWriteQueue->Enqueue(MoveTemp(ImageTask));
	}
}
