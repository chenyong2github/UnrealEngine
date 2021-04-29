// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "MediaMovieAssets.generated.h"

class UMediaPlayer;
class UMediaSource;

/**
 * Keeps assets alive during level loading so they don't get garbage collected while we are using them.
 */
UCLASS(Transient, NotPlaceable)
class UMediaMovieAssets : public UObject
{
	GENERATED_BODY()

public:
	UMediaMovieAssets();
	~UMediaMovieAssets();

	/**
	 * Sets which media player we are using.
	 *
	 * @param InMediaPlayer
	 */
	void SetMediaPlayer(UMediaPlayer* InMediaPlayer);


	/**
	 * Sets what media source we are using.
	 *
	 * @param InMediaSource Media source to play.
	 */
	void SetMediaSource(UMediaSource* InMediaSource);
	
private:
	/** Holds the player we are using. */
	UPROPERTY()
	UMediaPlayer* MediaPlayer;

	/** Holds the media source we are using. */
	UPROPERTY()
	UMediaSource* MediaSource;
};
