// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieStreamer.h"
#include "MediaPlayer.h"
#include "MediaSource.h"

DEFINE_LOG_CATEGORY(LogMediaMovieStreamer);

FMediaMovieStreamer::FMediaMovieStreamer()
{
	MovieViewport = MakeShareable(new FMovieViewport());
}

FMediaMovieStreamer::~FMediaMovieStreamer()
{
}

void FMediaMovieStreamer::SetMediaPlayer(UMediaPlayer* InMediaPlayer)
{
	MediaPlayers.Add(InMediaPlayer);
}

void FMediaMovieStreamer::SetMediaSource(UMediaSource* InMediaSource)
{
	MediaSources.Add(InMediaSource);
}

bool FMediaMovieStreamer::Init(const TArray<FString>& InMoviePaths, TEnumAsByte<EMoviePlaybackType> InPlaybackType)
{
	MovieViewport->SetTexture(nullptr);

	// Get player.
	UMediaPlayer* MediaPlayer = nullptr;
	if (MediaPlayers.Num() > 0)
	{
		MediaPlayer = MediaPlayers[0];
	}
	if (MediaPlayer == nullptr)
	{
		UE_LOG(LogMediaMovieStreamer, Error, TEXT("OpenNextMovie called but no player set."));
		return false;
	}

	// Get source.
	UMediaSource* MediaSource = nullptr;
	if (MediaSources.Num() > 0)
	{
		MediaSource = MediaSources[0];
	}
	if (MediaSource == nullptr)
	{
		UE_LOG(LogMediaMovieStreamer, Error, TEXT("OpenNextMovie called but no source set."));
		return false;
	}

	// Play source.
	MediaPlayer->OpenSource(MediaSource);

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
}

FTexture2DRHIRef FMediaMovieStreamer::GetTexture()
{
	return nullptr;
}

FMediaMovieStreamer::FOnCurrentMovieClipFinished& FMediaMovieStreamer::OnCurrentMovieClipFinished()
{
	return OnCurrentMovieClipFinishedDelegate;
}

