// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#if WITH_EDITOR
#include "TickableEditorObject.h"
#endif // WITH_EDITOR

#include "MediaSourceRenderer.generated.h"

class UMediaPlayer;
class UMediaSource;
class UMediaTexture;

/** Renders a media source to a texture in editor builds. */
UCLASS()
class UMediaSourceRenderer : public UObject
#if WITH_EDITOR
	, public FTickableEditorObject
#endif // WITH_EDITOR
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	/**
	 * Open the media source to render a texture for.
	 * 
	 * @param	InMediaSource		Media source to play.
	 * @return	Media texture that will hold the image.
	 */
	UMediaTexture* Open(UMediaSource* InMediaSource);

	/** FTickableEditorObject interface */
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMediaSourceRenderer, STATGROUP_Tickables); }
#endif // WITH_EDITOR

private:
	/**
	 * Callback when the media player is done seeking.
	 */
	UFUNCTION()
	void OnSeekCompleted();

	/**
	 * Cleans everything up.
	 * The media textue will remain so it can be used/reused.
	 */
	void Close();

	/** Holds the player we are using. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;

	/** Holds the media source we are using. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaSource> MediaSource = nullptr;

	/** Holds the media texture we are using. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture = nullptr;

	/** True if we are currently seeking. */
	bool bIsSeekActive = false;
	
};
