// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MoviePlayer.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMediaMovieStreamer, Log, All);

class IMediaModule;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;

/**
 * Movie streamer that allows you to use MediaFramework during level loading, etc.
 */
class FMediaMovieStreamer : public IMovieStreamer
{
public:
	FMediaMovieStreamer();
	~FMediaMovieStreamer();

	/**
	 * Sets which media player should be playing.
	 *
	 * @param InMediaPlayer
	 */
	MEDIAMOVIESTREAMER_API void SetMediaPlayer(UMediaPlayer* InMediaPlayer);

	/**
	 * Sets what to play.
	 *
	 * @param InMediaSource Media source to play.
	 */
	MEDIAMOVIESTREAMER_API void SetMediaSource(UMediaSource* InMediaSource);

	/**
	 * Sets what media texture we are using.
	 *
	 * @param InMediaSource Media source to play.
	 */
	MEDIAMOVIESTREAMER_API void SetMediaTexture(UMediaTexture* InMediaTexture);

	/**
	 * Called from UMediaMovieAssets when the media ends.
	 */
	void OnMediaEnd();

	/** IMovieStreamer interface */
	virtual bool Init(const TArray<FString>& InMoviePaths, TEnumAsByte<EMoviePlaybackType> InPlaybackType) override;
	virtual void ForceCompletion() override;
	virtual bool Tick(float DeltaTime) override;
	virtual TSharedPtr<class ISlateViewport> GetViewportInterface() override;
	virtual float GetAspectRatio() const override;
	virtual FString GetMovieName() override;
	virtual bool IsLastMovieInPlaylist() override;
	virtual void Cleanup() override;
	virtual FTexture2DRHIRef GetTexture() override;
	virtual FOnCurrentMovieClipFinished& OnCurrentMovieClipFinished() override;

	virtual void TickPreEngine() override;
	virtual void TickPostEngine() override;
	virtual void TickPostRender() override;
	
private:
	/** Delegate for when the movie is finished. */
	FOnCurrentMovieClipFinished OnCurrentMovieClipFinishedDelegate;

	/** Viewport data for displaying to Slate. */
	TSharedPtr<FMovieViewport> MovieViewport;
	/** Texture displaying to Slate. */
	TSharedPtr<FSlateTexture2DRHIRef, ESPMode::ThreadSafe> Texture;

	/** Holds the player we are using. */
	TWeakObjectPtr<UMediaPlayer> MediaPlayer;
	/** Holds the media source we are using. */
	TWeakObjectPtr<UMediaSource> MediaSource;
	/** Holds the media texture we are using. */
	TWeakObjectPtr<UMediaTexture> MediaTexture;

	/** Gets the media module to interface with MediaFramework. */
	IMediaModule* GetMediaModule();

	/** True if the media is still playing. */
	bool bIsPlaying;
};
