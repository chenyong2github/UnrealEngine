// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineObjectIdPass.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MovieRenderOverlappedImage.h"
#include "MoviePipelineOutputBuilder.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HitProxies.h"
#include "EngineUtils.h"
#include "Containers/HashTable.h"
#include "Misc/CString.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "MoviePipelineHashUtils.h"
#include "EngineModule.h"

DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_AccumulateMaskSample_TT"), STAT_AccumulateMaskSample_TaskThread, STATGROUP_MoviePipeline);

struct FObjectIdMaskSampleAccumulationArgs
{
public:
	TSharedPtr<FMaskOverlappedAccumulator, ESPMode::ThreadSafe> Accumulator;
	TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputMerger;
	int32 NumOutputLayers;
};

// Forward Declare
namespace MoviePipeline
{
	static void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const FObjectIdMaskSampleAccumulationArgs& InParams);
}

UMoviePipelineObjectIdRenderPass::UMoviePipelineObjectIdRenderPass()
	: UMoviePipelineImagePassBase()
{
	PassIdentifier = FMoviePipelinePassIdentifier("ActorHitProxyMask");

	// We output three layers which is 6 total influences per pixel.
	for (int32 Index = 0; Index < 3; Index++)
	{
		ExpectedPassIdentifiers.Add(FMoviePipelinePassIdentifier(PassIdentifier.Name + FString::Printf(TEXT("%02d"), Index)));
	}
}

void UMoviePipelineObjectIdRenderPass::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	// Don't call the super which adds the generic PassIdentifier, which in this case is numberless and incorrect for the final output spec.
	// Super::GatherOutputPassesImpl(ExpectedRenderPasses);
	
	ExpectedRenderPasses.Append(ExpectedPassIdentifiers);
}

void UMoviePipelineObjectIdRenderPass::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);

	// Re-initialize the render target with the correct bit depth.
	TileRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	TileRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	TileRenderTarget->InitCustomFormat(InPassInitSettings.BackbufferResolution.X, InPassInitSettings.BackbufferResolution.Y, EPixelFormat::PF_B8G8R8A8, false);

	AccumulatorPool = MakeShared<TAccumulatorPool<FMaskOverlappedAccumulator>, ESPMode::ThreadSafe>(6);
	SurfaceQueue = MakeShared<FMoviePipelineSurfaceQueue>(InPassInitSettings.BackbufferResolution, EPixelFormat::PF_B8G8R8A8, 3, false);
}

void UMoviePipelineObjectIdRenderPass::TeardownImpl()
{
	// This may call FlushRenderingCommands if there are outstanding readbacks that need to happen.
	SurfaceQueue->Shutdown();

	// Stall until the task graph has completed any pending accumulations.
	FTaskGraphInterface::Get().WaitUntilTasksComplete(OutstandingTasks, ENamedThreads::GameThread);
	OutstandingTasks.Reset();

	// Preserve our view state until the rendering thread has been flushed.
	Super::TeardownImpl();
}


void UMoviePipelineObjectIdRenderPass::GetViewShowFlags(FEngineShowFlags& OutShowFlag, EViewModeIndex& OutViewModeIndex) const 
{
	OutShowFlag = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);
	OutShowFlag.DisableAdvancedFeatures();
	OutShowFlag.SetPostProcessing(false);
	OutShowFlag.SetPostProcessMaterial(false);

	// Screen-percentage scaling mixes IDs when doing downsampling, so it is disabled.
	OutShowFlag.SetScreenPercentage(false);
	OutShowFlag.SetHitProxies(true);
	OutViewModeIndex = EViewModeIndex::VMI_Unlit;
}

void UMoviePipelineObjectIdRenderPass::RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	Super::RenderSample_GameThreadImpl(InSampleState);

	// Wait for a surface to be available to write to. This will stall the game thread while the RHI/Render Thread catch up.
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableSurface);
		SurfaceQueue->BlockUntilAnyAvailable();
	}


	// Main Render Pass
	{
		FMoviePipelineRenderPassMetrics InOutSampleState = InSampleState;

		TSharedPtr<FSceneViewFamilyContext> ViewFamily = CalculateViewFamily(InOutSampleState);

		// Submit to be rendered. Main render pass always uses target 0.
		FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();
		FCanvas Canvas = FCanvas(RenderTarget, nullptr, GetPipeline()->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);
		GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.Get());

		// Readback + Accumulate.
		PostRendererSubmission(InOutSampleState);
	}
}

void UMoviePipelineObjectIdRenderPass::PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	// If this was just to contribute to the history buffer, no need to go any further.
	if (InSampleState.bDiscardResult)
	{
		return;
	}

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = PassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = GetOutputFileSortingOrder();

	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, FramePayload->PassIdentifier);
	}

	FObjectIdMaskSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.Accumulator = StaticCastSharedPtr<FMaskOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.NumOutputLayers = ExpectedPassIdentifiers.Num();
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();
	
		FMoviePipelineBackgroundAccumulateTask Task;
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(InPixelData), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});

		this->OutstandingTasks.Add(Event);
	};
	
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;
	FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[LocalSurfaceQueue, FramePayload, Callback, RenderTarget](FRHICommandListImmediate& RHICmdList) mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			LocalSurfaceQueue->OnRenderTargetReady_RenderThread(RenderTarget->GetRenderTargetTexture(), FramePayload, MoveTemp(Callback));
		});
}

namespace MoviePipeline
{
	static void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const FObjectIdMaskSampleAccumulationArgs& InParams)
	{
		SCOPE_CYCLE_COUNTER(STAT_AccumulateMaskSample_TaskThread);

		bool bIsWellFormed = InPixelData->IsDataWellFormed();

		if (!bIsWellFormed)
		{
			// figure out why it is not well formed, and print a warning.
			int64 RawSize = InPixelData->GetRawDataSizeInBytes();

			int64 SizeX = InPixelData->GetSize().X;
			int64 SizeY = InPixelData->GetSize().Y;
			int64 ByteDepth = int64(InPixelData->GetBitDepth() / 8);
			int64 NumChannels = int64(InPixelData->GetNumChannels());
			int64 ExpectedTotalSize = SizeX * SizeY * ByteDepth * NumChannels;
			int64 ActualTotalSize = InPixelData->GetRawDataSizeInBytes();

			UE_LOG(LogMovieRenderPipeline, Log, TEXT("MaskPassAccumulateSample_TaskThread: Data is not well formed."));
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Image dimension: %lldx%lld, %lld, %lld"), SizeX, SizeY, ByteDepth, NumChannels);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Expected size: %lld"), ExpectedTotalSize);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Actual size:   %lld"), ActualTotalSize);
		}

		check(bIsWellFormed);

		FImagePixelDataPayload* FramePayload = InPixelData->GetPayload<FImagePixelDataPayload>();
		check(FramePayload);

		// Writing tiles can be useful for debug reasons. These get passed onto the output every frame.
		if (FramePayload->SampleState.bWriteSampleToDisk)
		{
			// Send the data to the Output Builder. This has to be a copy of the pixel data from the GPU, since
			// it enqueues it onto the game thread and won't be read/sent to write to disk for another frame. 
			// The extra copy is unfortunate, but is only the size of a single sample (ie: 1920x1080 -> 17mb)
			TUniquePtr<FImagePixelData> SampleData = InPixelData->CopyImageData();
			InParams.OutputMerger->OnSingleSampleDataAvailable_AnyThread(MoveTemp(SampleData));
		}


		// For the first sample in a new output, we allocate memory
		if (FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample())
		{
			InParams.Accumulator->InitMemory(FIntPoint(FramePayload->SampleState.TileSize.X * FramePayload->SampleState.TileCounts.X, FramePayload->SampleState.TileSize.Y * FramePayload->SampleState.TileCounts.Y));
			InParams.Accumulator->ZeroPlanes();
		}

		// Accumulate the new sample to our target
		{
			const double AccumulateBeginTime = FPlatformTime::Seconds();

			FIntPoint RawSize = InPixelData->GetSize();

			check(FramePayload->SampleState.TileSize.X + 2 * FramePayload->SampleState.OverlappedPad.X == RawSize.X);
			check(FramePayload->SampleState.TileSize.Y + 2 * FramePayload->SampleState.OverlappedPad.Y == RawSize.Y);

			const void* RawData;
			int64 TotalSize;
			InPixelData->GetRawData(RawData, TotalSize);

			TSharedRef<FJsonObject> JsonManifest = MakeShared<FJsonObject>();

			const FColor* RawDataPtr = static_cast<const FColor*>(RawData);
			TArray64<float> IdData;
			IdData.Reserve(RawSize.X * RawSize.Y);
			static const uint32 DefaultHash = HashNameToId(TCHAR_TO_UTF8(TEXT("default")));

			for (int64 Index = 0; Index < (RawSize.X * RawSize.Y); Index++)
			{
				FHitProxyId HitProxyId(RawDataPtr[Index]);
				HHitProxy* HitProxy = GetHitProxyById(HitProxyId);
				HActor* ActorHitProxy = HitProxyCast<HActor>(HitProxy);
				uint32 Hash = DefaultHash;
				
				if (ActorHitProxy && ActorHitProxy->Actor && ActorHitProxy->PrimComponent)
				{
					// Hitproxies only have one material to represent an entire component, but component names are too generic
					// so instead we build a hash out of the actor name and component. This can still lead to duplicates but
					// they would be visible in the World Outliner.
					FString ProxyIdName;
#if WITH_EDITOR
					FName FolderPath = ActorHitProxy->Actor->GetFolderPath();
					if (FolderPath.IsNone())
					{
						ProxyIdName = FString::Printf(TEXT("%s.%s"), *ActorHitProxy->Actor->GetActorLabel(), *GetNameSafe(ActorHitProxy->PrimComponent));
					}
					else
					{
						ProxyIdName = FString::Printf(TEXT("%s/%s.%s"), *FolderPath.ToString(), *ActorHitProxy->Actor->GetActorLabel(), *GetNameSafe(ActorHitProxy->PrimComponent));
					}
#else
					ProxyIdName = FString::Printf(TEXT("%s.%s"), *GetNameSafe(ActorHitProxy->Actor), *GetNameSafe(ActorHitProxy->PrimComponent));
#endif
					Hash = HashNameToId(TCHAR_TO_UTF8(*ProxyIdName));

					FString HashAsString = FString::Printf(TEXT("%08x"), Hash);

					// Build an object which is json key/value pairs for each hitproxy name and object id hash.
					JsonManifest->SetStringField(ProxyIdName, HashAsString);
				}

				IdData.Add(*(float*)(&Hash));
			}

			// Build Metadata
			uint32 NameHash = HashNameToId(TCHAR_TO_UTF8(*FramePayload->PassIdentifier.Name));
			FString HashAsShortString = FString::Printf(TEXT("%08x"), NameHash);
			HashAsShortString.LeftInline(7); 

			FramePayload->SampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("cryptomatte/%s/name"), *HashAsShortString), FramePayload->PassIdentifier.Name);
			FramePayload->SampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("cryptomatte/%s/hash"), *HashAsShortString), TEXT("MurmurHash3_32"));
			FramePayload->SampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("cryptomatte/%s/conversion"), *HashAsShortString), TEXT("uint32_to_float32"));

			// Add our default to the manifest.
			JsonManifest->SetStringField(TEXT("default"), FString::Printf(TEXT("%08x"), DefaultHash));
			FString ManifestOutput;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ManifestOutput);
			FJsonSerializer::Serialize(JsonManifest, Writer);
			FramePayload->SampleState.OutputState.FileMetadata.Add(FString::Printf(TEXT("cryptomatte/%s/manifest"), *HashAsShortString), ManifestOutput);


			// bool bSkip = FramePayload->SampleState.TileIndexes.X != 0 || FramePayload->SampleState.TileIndexes.Y != 1;
			// if (!bSkip)
			{
				InParams.Accumulator->AccumulatePixelData((float*)(IdData.GetData()), RawSize, FramePayload->SampleState.OverlappedOffset, FramePayload->SampleState.OverlappedSubpixelShift,
					FramePayload->SampleState.WeightFunctionX, FramePayload->SampleState.WeightFunctionY);
			}

			const double AccumulateEndTime = FPlatformTime::Seconds();
			const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime) * 1000.0f);

			UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("Accumulation time: %8.2fms"), ElapsedMs);
		}

		if (FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample())
		{
			int32 FullSizeX = InParams.Accumulator->PlaneSize.X;
			int32 FullSizeY = InParams.Accumulator->PlaneSize.Y;

			// Now that a tile is fully built and accumulated we can notify the output builder that the
			// data is ready so it can pass that onto the output containers (if needed).
			// 32 bit FLinearColor
			TArray<TArray64<FLinearColor>> OutputLayers;
			for (int32 Index = 0; Index < InParams.NumOutputLayers; Index++)
			{
				OutputLayers.Add(TArray64<FLinearColor>());
			}
			InParams.Accumulator->FetchFinalPixelDataLinearColor(OutputLayers);

			for (int32 Index = 0; Index < InParams.NumOutputLayers; Index++)
			{			
				// We unfortunately can't share ownership of the payload from the last sample due to the changed pass identifiers.
				TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> NewPayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
				NewPayload->PassIdentifier = FMoviePipelinePassIdentifier(FramePayload->PassIdentifier.Name + FString::Printf(TEXT("%02d"), Index));
				NewPayload->SampleState = FramePayload->SampleState;
				NewPayload->SortingOrder = FramePayload->SortingOrder;

				TUniquePtr<TImagePixelData<FLinearColor> > FinalPixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(FullSizeX, FullSizeY), MoveTemp(OutputLayers[Index]), NewPayload);

				// Send each layer to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			
			// Free the memory in the accumulator now that we've extracted all
			InParams.Accumulator->Reset();
		}
	}
}
