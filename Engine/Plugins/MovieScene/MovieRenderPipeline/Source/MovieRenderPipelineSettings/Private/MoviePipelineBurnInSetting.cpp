// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineBurnInSetting.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineBurnInWidget.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipeline.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MoviePipelineOutputBuilder.h"
#include "ImagePixelData.h"

void UMoviePipelineBurnInSetting::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	if (BurnInClass.IsValid())
	{
		ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(TEXT("BurnInOverlay")));
	}
}

void UMoviePipelineBurnInSetting::RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	if (InSampleState.bDiscardResult)
	{
		return;
	}

	if (!WidgetRenderer)
	{
		return;
	}

	const bool bFirstTile = InSampleState.GetTileIndex() == 0;
	const bool bFirstSpatial = InSampleState.SpatialSampleIndex == (InSampleState.SpatialSampleCount - 1);
	const bool bFirstTemporal = InSampleState.TemporalSampleIndex == (InSampleState.TemporalSampleCount - 1);

	if (bFirstTile && bFirstSpatial && bFirstTemporal)
	{
		// Update the Widget with the latest frame information
		BurnInWidgetInstance->OnOutputFrameStarted(GetPipeline());

		// Draw the widget to the render target
		WidgetRenderer->DrawWindow(RenderTarget, VirtualWindow->GetHittestGrid(), VirtualWindow.ToSharedRef(), 1.f, OutputResolution, InSampleState.OutputState.TimeData.FrameDeltaTime);

		FRenderTarget* BackbufferRenderTarget = RenderTarget->GameThread_GetRenderTargetResource();
		TSharedPtr<FMoviePipelineOutputMerger, ESPMode::ThreadSafe> OutputBuilder = GetPipeline()->OutputBuilder;

		ENQUEUE_RENDER_COMMAND(BurnInRenderTargetResolveCommand)(
			[InSampleState, BackbufferRenderTarget, OutputBuilder](FRHICommandListImmediate& RHICmdList)
		{
			FIntRect SourceRect = FIntRect(0, 0, BackbufferRenderTarget->GetSizeXY().X, BackbufferRenderTarget->GetSizeXY().Y);

			// Read the data back to the CPU
			TArray<FColor> RawPixels;
			RawPixels.SetNum(SourceRect.Width() * SourceRect.Height());

			FReadSurfaceDataFlags ReadDataFlags(ERangeCompressionMode::RCM_MinMax);
			ReadDataFlags.SetLinearToGamma(false);

			RHICmdList.ReadSurfaceData(BackbufferRenderTarget->GetRenderTargetTexture(), SourceRect, RawPixels, ReadDataFlags);

			TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FrameData = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
			FrameData->PassIdentifier = FMoviePipelinePassIdentifier(TEXT("BurnInOverlay"));
			FrameData->SampleState = InSampleState;
			FrameData->bRequireTransparentOutput = true;
			FrameData->SortingOrder = 3;

			TUniquePtr<FImagePixelData> PixelData = MakeUnique<TImagePixelData<FColor>>(InSampleState.BackbufferSize, TArray64<FColor>(MoveTemp(RawPixels)), FrameData);

			OutputBuilder->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(PixelData));
		});
	}
}

void UMoviePipelineBurnInSetting::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	if (!BurnInClass.IsValid())
	{
		return;
	}

	UClass* BurnIn = BurnInClass.TryLoadClass<UMoviePipelineBurnInWidget>();
	ensureAlwaysMsgf(BurnIn, TEXT("Failed to load burnin class: '%s'."), *BurnInClass.GetAssetPathString());

	BurnInWidgetInstance = CreateWidget<UMoviePipelineBurnInWidget>(GetWorld(), BurnIn);

	OutputResolution = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>()->OutputResolution;
	VirtualWindow = SNew(SVirtualWindow).Size(FVector2D(OutputResolution.X, OutputResolution.Y));
	VirtualWindow->SetContent(BurnInWidgetInstance->TakeWidget());

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterVirtualWindow(VirtualWindow.ToSharedRef());
	}

	RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->ClearColor = FLinearColor::Transparent;

	bool bInForceLinearGamma = false;
	RenderTarget->InitCustomFormat(OutputResolution.X, OutputResolution.Y, EPixelFormat::PF_B8G8R8A8, bInForceLinearGamma);

	bool bApplyGammaCorrection = false;
	WidgetRenderer = MakeShared<FWidgetRenderer>(bApplyGammaCorrection);
}

void UMoviePipelineBurnInSetting::TeardownImpl() 
{
	FlushRenderingCommands();

	if (FSlateApplication::IsInitialized() && VirtualWindow.IsValid())
	{
		FSlateApplication::Get().UnregisterVirtualWindow(VirtualWindow.ToSharedRef());
	}
	
	VirtualWindow = nullptr;
	
	WidgetRenderer = nullptr;
	RenderTarget = nullptr;
	BurnInWidgetInstance = nullptr;
}