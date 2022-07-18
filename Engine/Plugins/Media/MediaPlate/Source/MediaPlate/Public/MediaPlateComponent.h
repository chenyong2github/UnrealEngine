// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MediaPlayerOptions.h"
#include "MediaSource.h"
#include "MediaTextureTracker.h"

#include "MediaPlateComponent.generated.h"

class FMediaComponentClockSink;
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
	virtual void BeginDestroy();
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

	/** If true then set the aspect ratio automatically based on the media. */
	UPROPERTY(BlueprintReadWrite, Category = "MediaPlate")
	bool bIsAspectRatioAuto = true;

	/** Holds the media player. */
	UPROPERTY(Category = MediaPlate, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UMediaComponent> MediaComponent;

	/** Holds the component to play sound. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = MediaPlate)
	TObjectPtr<UMediaSoundComponent> SoundComponent;

	/** Holds the component for the mesh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = MediaPlate)
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	/** What media playlist to play. */
	UPROPERTY(EditAnywhere, Category = "MediaPlate")
	TObjectPtr<UMediaPlaylist> MediaPlaylist;

	/** Override the default cache settings. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "MediaPlate")
	FMediaSourceCacheSettings CacheSettings;

	/**
	 * Specify type of mesh used for visible mips and tiles calculations.
	 * (Using the provided plane and sphere meshes only.)
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "MediaPlate")
	EMediaTextureVisibleMipsTiles VisibleMipsTilesCalculations;

	/** Set the arc size in degrees used for visible mips and tiles calculations, specific to the sphere. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	void SetMeshRange(FVector2D InMeshRange);

	/** Return the arc size in degrees used for visible mips and tiles calculations, specific to the sphere. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	FVector2D GetMeshRange() const { return MeshRange; }

	/**
	 * Adds our media texture to the media texture tracker.
	 */
	void RegisterWithMediaTextureTracker();
	/**
	 * Removes our texture from the media texture tracker.
	 */
	void UnregisterWithMediaTextureTracker();

	/**
	 * Call this to set the aspect ratio of the mesh.
	 */
	void SetAspectRatio(float AspectRatio);

	/**
	 * Called from the media clock.
	 */
	void TickOutput();

#if WITH_EDITOR
	/** Propagate visible calculation changes to the tracker/player objects, optionally restarting playback if needed. */
	void OnVisibleMipsTilesCalculationsChange();
#endif

private:
	UPROPERTY()
	FVector2D MeshRange = FVector2D(360.0f, 180.0f);

	/** Name for our media component. */
	static FLazyName MediaComponentName;
	/** Name for our playlist. */
	static FLazyName MediaPlaylistName;

	/** Info representing this object. */
	TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> MediaTextureTrackerObject;
	/** Our media clock sink. */
	TSharedPtr<FMediaComponentClockSink, ESPMode::ThreadSafe> ClockSink;

	/**
	 * Plays a media source.
	 * 
	 * @param	InMediaSource		Media source to play.
	 * @return	True if we played anything.
	 */
	bool PlayMediaSource(UMediaSource* InMediaSource);

	/**
	 * Stops the clock sink so we no longer tick.
	 */
	void StopClockSink();

};
