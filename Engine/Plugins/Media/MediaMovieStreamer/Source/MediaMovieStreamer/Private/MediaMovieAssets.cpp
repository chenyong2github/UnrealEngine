// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieAssets.h"
#include "MediaPlayer.h"
#include "MediaSource.h"

UMediaMovieAssets::UMediaMovieAssets()
	: MediaPlayer(nullptr)
	, MediaSource(nullptr)
{
}

UMediaMovieAssets::~UMediaMovieAssets()
{
}

void UMediaMovieAssets::SetMediaPlayer(UMediaPlayer* InMediaPlayer)
{
	MediaPlayer = InMediaPlayer;
}

void UMediaMovieAssets::SetMediaSource(UMediaSource* InMediaSource)
{
	MediaSource = InMediaSource;
}
