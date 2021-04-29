// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieAssets.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"

UMediaMovieAssets::UMediaMovieAssets()
	: MediaPlayer(nullptr)
	, MediaSource(nullptr)
	, MediaTexture(nullptr)
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

void UMediaMovieAssets::SetMediaTexture(UMediaTexture* InMediaTexture)
{
	MediaTexture = InMediaTexture;
}

