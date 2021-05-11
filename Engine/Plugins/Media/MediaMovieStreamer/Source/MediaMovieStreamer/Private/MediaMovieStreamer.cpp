// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieStreamer.h"

#include "IMediaModule.h"
#include "MediaMovieAssets.h"
#include "MediaMovieStreamerModule.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"

#include "Rendering/RenderingCommon.h"
#include "Slate/SlateTextures.h"

DEFINE_LOG_CATEGORY(LogMediaMovieStreamer);

FMediaMovieStreamer::FMediaMovieStreamer()
	: bIsPlaying(false)
{
	MovieViewport = MakeShareable(new FMovieViewport());
}

FMediaMovieStreamer::~FMediaMovieStreamer()
{
	Cleanup();
}

void FMediaMovieStreamer::SetMediaPlayer(UMediaPlayer* InMediaPlayer)
{
	// Tell MovieAssets about this so it does not get garbage collected.
	UMediaMovieAssets* MovieAssets = FMediaMovieStreamerModule::GetMovieAssets();
	if (MovieAssets != nullptr)
	{
		MovieAssets->SetMediaPlayer(InMediaPlayer, this);
	}

	MediaPlayer = InMediaPlayer;
}

void FMediaMovieStreamer::SetMediaSource(UMediaSource* InMediaSource)
{
	// Tell MovieAssets about this so it does not get garbage collected.
	UMediaMovieAssets* MovieAssets = FMediaMovieStreamerModule::GetMovieAssets();
	if (MovieAssets != nullptr)
	{
		MovieAssets->SetMediaSource(InMediaSource);
	}

	MediaSource = InMediaSource;
}

void FMediaMovieStreamer::SetMediaTexture(UMediaTexture* InMediaTexture)
{
	// Tell MovieAssets about this so it does not get garbage collected.
	UMediaMovieAssets* MovieAssets = FMediaMovieStreamerModule::GetMovieAssets();
	if (MovieAssets != nullptr)
	{
		MovieAssets->SetMediaTexture(InMediaTexture);
	}

	MediaTexture = InMediaTexture;
}

void FMediaMovieStreamer::OnMediaEnd()
{
	bIsPlaying = false;
}

bool FMediaMovieStreamer::Init(const TArray<FString>& InMoviePaths, TEnumAsByte<EMoviePlaybackType> InPlaybackType)
{
	MovieViewport->SetTexture(nullptr);

	// Get player.
	if (MediaPlayer.IsValid() == false)
	{
		UE_LOG(LogMediaMovieStreamer, Error, TEXT("OpenNextMovie called but no player set."));
		return false;
	}

	// Get source.
	if (MediaSource.IsValid() == false)
	{
		UE_LOG(LogMediaMovieStreamer, Error, TEXT("OpenNextMovie called but no source set."));
		return false;
	}

	// Play source.
	bIsPlaying = true;
	MediaPlayer->OpenSource(MediaSource.Get());

	Texture = MakeShareable(new FSlateTexture2DRHIRef(nullptr, 0, 0));
	
	return true;
}

void FMediaMovieStreamer::ForceCompletion()
{
}

bool FMediaMovieStreamer::Tick(float DeltaTime)
{
	check(IsInRenderingThread());

	// Get media texture.
	if (MediaTexture != nullptr)
	{
		FTextureResource* TextureResource = MediaTexture->GetResource();
		if (TextureResource != nullptr)
		{
			FRHITexture2D* RHITexture2D = TextureResource->GetTexture2DRHI();
			if (RHITexture2D != nullptr)
			{
				// Get slate texture.
				FSlateTexture2DRHIRef* CurrentTexture = Texture.Get();
				if (CurrentTexture)
				{
					if (!CurrentTexture->IsInitialized())
					{
						CurrentTexture->InitResource();
					}

					// Update the slate texture.
					FTexture2DRHIRef ref = RHITexture2D;
					CurrentTexture->SetRHIRef(ref, MediaTexture->GetWidth(), MediaTexture->GetHeight());

					// Update viewport.
					if (MovieViewport->GetViewportRenderTargetTexture() == nullptr)
					{
						MovieViewport->SetTexture(Texture);
					}
				}
			}
		}
	}

	return !bIsPlaying;
}

TSharedPtr<class ISlateViewport> FMediaMovieStreamer::GetViewportInterface()
{
	return MovieViewport;
}

float FMediaMovieStreamer::GetAspectRatio() const
{
	return 1.0f;
}

FString FMediaMovieStreamer::GetMovieName()
{
	return FString();
}

bool FMediaMovieStreamer::IsLastMovieInPlaylist()
{
	return true;
}

void FMediaMovieStreamer::Cleanup()
{
	// Remove our hold on the assets.
	UMediaMovieAssets* MovieAssets = FMediaMovieStreamerModule::GetMovieAssets();
	if (MovieAssets != nullptr)
	{
		MovieAssets->SetMediaPlayer(nullptr, nullptr);
		MovieAssets->SetMediaSource(nullptr);
	}

	MediaPlayer.Reset();
	MediaSource.Reset();

	MovieViewport->SetTexture(NULL);
	
	FSlateTexture2DRHIRef* CurrentTexture = Texture.Get();
	if (CurrentTexture != nullptr)
	{
		// Remove any reference to UMediaTexture so we can let MediaFramework release it.
		ENQUEUE_RENDER_COMMAND(ResetMovieTexture)(
		[CurrentTexture](FRHICommandListImmediate& RHICmdList)
		{
			CurrentTexture->SetRHIRef(nullptr, 0, 0);
		});
		BeginReleaseResource(CurrentTexture);
		FlushRenderingCommands();
		Texture.Reset();
	}
}

FTexture2DRHIRef FMediaMovieStreamer::GetTexture()
{
	return nullptr;
}

FMediaMovieStreamer::FOnCurrentMovieClipFinished& FMediaMovieStreamer::OnCurrentMovieClipFinished()
{
	return OnCurrentMovieClipFinishedDelegate;
}

void FMediaMovieStreamer::TickPreEngine()
{
	IMediaModule* MediaModule = GetMediaModule();
	if (MediaModule != nullptr)
	{
		MediaModule->TickPreEngine();
	}
}

void FMediaMovieStreamer::TickPostEngine()
{
	IMediaModule* MediaModule = GetMediaModule();
	if (MediaModule != nullptr)
	{
		MediaModule->TickPostEngine();
	}
}

void FMediaMovieStreamer::TickPostRender()
{
	IMediaModule* MediaModule = GetMediaModule();
	if (MediaModule != nullptr)
	{
		MediaModule->TickPostRender();
	}
}

IMediaModule* FMediaMovieStreamer::GetMediaModule()
{
	static const FName MediaModuleName(TEXT("Media"));
	IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>(MediaModuleName);
	return MediaModule;
}

