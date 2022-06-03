// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MediaPlayerOptions.h"
#include "MediaTextureTracker.h"

#include "MediaPlateComponent.generated.h"

class UMediaComponent;
class UMediaPlayer;
class UMediaPlaylist;
class UMediaSoundComponent;
class UMediaSource;
class UMediaTexture;


/**
 * This is a component for AMediaPlate that can play and show media in the world.
 */
UCLASS()
class MEDIAPLATE_API UMediaPlateComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	//~ UActorComponent interface.
	virtual void OnRegister();
	virtual void BeginPlay();
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	/**
	 * Call this get our media player.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	UMediaPlayer* GetMediaPlayer();

	/**
	 * Call this get our media texture.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	UMediaTexture* GetMediaTexture();

	/**
	 * Call this to start playing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	void Play();

	/**
	 * Call this to stop playing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	void Stop();

	/** If set then start playing right away. */
	UPROPERTY(EditAnywhere, Category = "MediaPlate")
	bool bAutoPlay;

	/** If set then loop when we reach the end. */
	UPROPERTY(EditAnywhere, Category = "MediaPlate")
	bool bLoop;

	/** If set then enable audio. */
	UPROPERTY(EditAnywhere, Category = "MediaPlate")
	bool bEnableAudio = false;

	/** What time to start playing from (in seconds). */
	UPROPERTY(EditAnywhere, Category = "MediaPlate", meta = (ClampMin = "0.0"))
	float StartTime = 0.0f;

	/** Holds the media player. */
	UPROPERTY(Category = MediaPlate, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UMediaComponent> MediaComponent;

	/** Holds the component to play sound. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = MediaPlate)
	TObjectPtr<UMediaSoundComponent> SoundComponent;

	/** What media playlist to play. */
	UPROPERTY(EditAnywhere, Category = "MediaPlate")
	TObjectPtr<UMediaPlaylist> MediaPlaylist;

	/** Enable smart caching for image sequences. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "MediaPlate")
	bool bSmartCacheEnabled = true;

	/**
	 * The cache will fill up with frames that are up to this time from the current time.
	 * E.g. if this is 0.2, and we are at time index 5 seconds,
	 * then we will fill the cache with frames between 5 seconds and 5.2 seconds.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "MediaPlate")
	float SmartCacheTimeToLookAhead = 0.2f;

	/**
	 * Specify type of mesh used for visible mips and tiles calculations.
	 * (Using the provided plane and sphere meshes only.)
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "MediaPlate")
	EMediaTextureVisibleMipsTiles VisibleMipsTilesCalculations;

	/**
	 * Horizontal size of the mesh used for visible mips and tiles calculations.
	 * For a sphere, this is the horizontal arc size in degrees.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "MediaPlate")
	float MeshHorizontalRange = 360.0f;

	/**
	 * Adds our media texture to the media texture tracker.
	 */
	void RegisterWithMediaTextureTracker();
	/**
	 * Removes our texture from the media texture tracker.
	 */
	void UnregisterWithMediaTextureTracker();

#if WITH_EDITOR
	/** Propagate visible calculation changes to the tracker/player objects, optionally restarting playback if needed. */
	void OnVisibleMipsTilesCalculationsChange();
#endif

private:
	/** Name for our media component. */
	static FLazyName MediaComponentName;
	/** Name for our playlist. */
	static FLazyName MediaPlaylistName;

	/** Info representing this object. */
	TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> MediaTextureTrackerObject;

	/**
	 * Plays a media source.
	 * 
	 * @param	InMediaSource		Media source to play.
	 * @return	True if we played anything.
	 */
	bool PlayMediaSource(UMediaSource* InMediaSource);
};
