// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/DisplayClusterMediaInputBase.h"

#include "DisplayClusterMediaHelpers.h"
#include "DisplayClusterMediaLog.h"

#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"


FDisplayClusterMediaInputBase::FDisplayClusterMediaInputBase(const FString& InMediaId, const FString& InClusterNodeId, UMediaSource* InMediaSource, UMediaPlayer* InMediaPlayer, UMediaTexture* InMediaTexture)
	: FDisplayClusterMediaBase(InMediaId, InClusterNodeId)
	, MediaSource(InMediaSource)
	, MediaPlayer(InMediaPlayer)
	, MediaTexture(InMediaTexture)
{
	// We expect these to always be valid
	check(MediaSource && MediaPlayer && MediaTexture);
}


void FDisplayClusterMediaInputBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MediaSource);
	Collector.AddReferencedObject(MediaPlayer);
	Collector.AddReferencedObject(MediaTexture);
}

bool FDisplayClusterMediaInputBase::Play()
{
	if (MediaSource && MediaPlayer && MediaTexture)
	{
		MediaPlayer->PlayOnOpen = true;
		MediaPlayer->OnMediaEvent().AddRaw(this, &FDisplayClusterMediaInputBase::OnMediaEvent);

		const bool bIsPlaying = MediaPlayer->OpenSource(MediaSource);
		if (bIsPlaying)
		{
			UE_LOG(LogDisplayClusterMedia, Log, TEXT("Started playing media: %s"), *GetMediaId());
		}
		else
		{
			UE_LOG(LogDisplayClusterMedia, Warning, TEXT("Couldn't start playing media: %s"), *GetMediaId());
		}

		return bIsPlaying;
	}

	return false;
}

void FDisplayClusterMediaInputBase::Stop()
{
	if (MediaPlayer)
	{
		MediaPlayer->Close();
		MediaPlayer->OnMediaEvent().RemoveAll(this);
	}
}

void FDisplayClusterMediaInputBase::ImportMediaData(FRHICommandListImmediate& RHICmdList, const FMediaTextureInfo& TextureInfo)
{
	FRHITexture* const SrcTexture = MediaTexture->GetResource()->GetTextureRHI();
	FRHITexture* const DstTexture = TextureInfo.Texture;

	if (SrcTexture && DstTexture)
	{
		const FIntPoint DstRegionSize = TextureInfo.Region.Size();

		if (SrcTexture->GetDesc().Format == DstTexture->GetDesc().Format &&
			SrcTexture->GetDesc().Extent == DstRegionSize)
		{
			FRHICopyTextureInfo CopyInfo;

			CopyInfo.DestPosition = FIntVector(TextureInfo.Region.Min.X, TextureInfo.Region.Min.Y, 0);
			CopyInfo.Size = FIntVector(DstRegionSize.X, DstRegionSize.Y, 0);

			TransitionAndCopyTexture(RHICmdList, SrcTexture, DstTexture, CopyInfo);
		}
		else
		{
			UE_LOG(LogDisplayClusterMedia, Log, TEXT(""));

			DisplayClusterMediaHelpers::ResampleTexture_RenderThread(
				RHICmdList, SrcTexture, DstTexture,
				FIntRect(FIntPoint(0, 0), SrcTexture->GetDesc().Extent),
				TextureInfo.Region);
		}
	}
}

void FDisplayClusterMediaInputBase::OnMediaEvent(EMediaEvent MediaEvent)
{
	switch (MediaEvent)
	{
	/** The player started connecting to the media source. */
	case EMediaEvent::MediaConnecting:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': Connectiong"), *GetMediaId());
		break;

	/** A new media source has been opened. */
	case EMediaEvent::MediaOpened:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': Opened"), *GetMediaId());
		break;

	/** The current media source has been closed. */
	case EMediaEvent::MediaClosed:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': Closed"), *GetMediaId());
		break;
		
	/** A media source failed to open. */
	case EMediaEvent::MediaOpenFailed:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': OpenFailed"), *GetMediaId());
		break;

	default:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': %d"), *GetMediaId(), static_cast<int32>(MediaEvent));
		break;
	}
}
