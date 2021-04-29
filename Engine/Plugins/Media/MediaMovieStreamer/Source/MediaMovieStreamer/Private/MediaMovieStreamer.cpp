// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieStreamer.h"

#include "IMediaModule.h"
#include "MediaMovieAssets.h"
#include "MediaMovieStreamerModule.h"
#include "MediaPlayer.h"
#include "MediaSource.h"

DEFINE_LOG_CATEGORY(LogMediaMovieStreamer);

FMediaMovieStreamer::FMediaMovieStreamer()
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
		MovieAssets->SetMediaPlayer(InMediaPlayer);
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
	MediaPlayer->OpenSource(MediaSource.Get());

	return true;
}

void FMediaMovieStreamer::ForceCompletion()
{
}

bool FMediaMovieStreamer::Tick(float DeltaTime)
{
	return false;
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
		MovieAssets->SetMediaPlayer(nullptr);
		MovieAssets->SetMediaSource(nullptr);
	}

	MediaPlayer.Reset();
	MediaSource.Reset();
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

