// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/DisplayClusterMediaCaptureBase.h"
#include "DisplayClusterMediaHelpers.h"
#include "DisplayClusterMediaLog.h"

#include "MediaCapture.h"
#include "MediaOutput.h"
#include "Engine/TextureRenderTarget2D.h"

#include "RHIResources.h"


FDisplayClusterMediaCaptureBase::FDisplayClusterMediaCaptureBase(const FString& InMediaId, const FString& InClusterNodeId, UMediaOutput* InMediaOutput, UTextureRenderTarget2D* InRenderTarget)
	: FDisplayClusterMediaBase(InMediaId, InClusterNodeId)
	, MediaOutput(InMediaOutput)
	, RenderTarget(InRenderTarget)
{
	// We expect these to always be valid
	check(InMediaOutput && InRenderTarget);
}


void FDisplayClusterMediaCaptureBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MediaOutput);
	Collector.AddReferencedObject(RenderTarget);

	if (MediaCapture)
	{
		Collector.AddReferencedObject(MediaCapture);
	}
}

bool FDisplayClusterMediaCaptureBase::StartCapture()
{
	if (MediaOutput && RenderTarget && !MediaCapture)
	{
		MediaCapture = MediaOutput->CreateMediaCapture();
		if (MediaCapture)
		{
			MediaCapture->SetMediaOutput(MediaOutput);

			FMediaCaptureOptions MediaCaptureOptions;
			MediaCaptureOptions.NumberOfFramesToCapture = -1;
			MediaCaptureOptions.bResizeSourceBuffer = true;
			MediaCaptureOptions.bSkipFrameWhenRunningExpensiveTasks = false;

			const bool bCaptureStarted = MediaCapture->CaptureTextureRenderTarget2D(RenderTarget, MediaCaptureOptions);
			if (bCaptureStarted)
			{
				UE_LOG(LogDisplayClusterMedia, Log, TEXT("Started media capture: '%s'"), *GetMediaId());
			}
			else
			{
				UE_LOG(LogDisplayClusterMedia, Warning, TEXT("Couldn't start media capture '%s'"), *GetMediaId());
			}

			return bCaptureStarted;
		}
	}

	return false;
}

void FDisplayClusterMediaCaptureBase::StopCapture()
{
	if (MediaCapture)
	{
		MediaCapture->StopCapture(false);
		MediaCapture = nullptr;
	}
}

void FDisplayClusterMediaCaptureBase::ExportMediaData(FRHICommandListImmediate& RHICmdList, const FMediaTextureInfo& TextureInfo)
{
	FRHITexture* const SrcTexture = TextureInfo.Texture;
	FRHITexture* const DstTexture = GetRenderTarget()->GetRenderTargetResource()->GetTextureRHI();

	if (SrcTexture && DstTexture)
	{
		const FIntPoint SrcRegionSize = TextureInfo.Region.Size();

		if (SrcTexture->GetDesc().Format == DstTexture->GetDesc().Format &&
			SrcRegionSize == DstTexture->GetDesc().Extent)
		{
			FRHICopyTextureInfo CopyInfo;

			CopyInfo.SourcePosition = FIntVector(TextureInfo.Region.Min.X, TextureInfo.Region.Min.Y, 0);
			CopyInfo.Size = FIntVector(SrcRegionSize.X, SrcRegionSize.Y, 0);

			TransitionAndCopyTexture(RHICmdList, SrcTexture, DstTexture, CopyInfo);
		}
		else
		{
			DisplayClusterMediaHelpers::ResampleTexture_RenderThread(RHICmdList, SrcTexture, DstTexture, TextureInfo.Region, FIntRect(FIntPoint::ZeroValue, DstTexture->GetDesc().Extent));
		}
	}
}
