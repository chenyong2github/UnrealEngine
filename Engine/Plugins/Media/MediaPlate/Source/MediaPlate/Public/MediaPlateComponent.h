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
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MediaPlate", meta = (ClampMin = "0.0"))
	float StartTime = 0.0f;

	/** If true then set the aspect ratio automatically based on the media. */
	UPROPERTY(BlueprintReadWrite, Category = "MediaPlate")
	bool bIsAspectRatioAuto = true;

	/** Holds the component to play sound. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = MediaPlate)
	TObjectPtr<UMediaSoundComponent> SoundComponent;

	/** Holds the component for the mesh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = MediaPlate)
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;
	/** Holds the component for the mesh. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = MediaPlate)
	TArray<TObjectPtr<UStaticMeshComponent>> Letterboxes;

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

	/** Call this to set bPlayOnlyWhenVisible. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	void SetPlayOnlyWhenVisible(bool bInPlayOnlyWhenVisible);

	/**
	 * Adds our media texture to the media texture tracker.
	 */
	void RegisterWithMediaTextureTracker();
	/**
	 * Removes our texture from the media texture tracker.
	 */
	void UnregisterWithMediaTextureTracker();

	/**
	 * Call this to geet the aspect ratio of the mesh.
	 */
	float GetAspectRatio();

	/**
	 * Call this to set the aspect ratio of the mesh.
	 */
	void SetAspectRatio(float AspectRatio);

	/**
	 * Call this to get the aspect ratio of the screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	float GetLetterboxAspectRatio() { return LetterboxAspectRatio; }

	/**
	 * Call this to set the aspect ratio of the screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	void SetLetterboxAspectRatio(float AspectRatio);

	/**
	 * Called from the media clock.
	 */
	void TickOutput();

#if WITH_EDITOR
	/** Propagate visible calculation changes to the tracker/player objects, optionally restarting playback if needed. */
	void OnVisibleMipsTilesCalculationsChange();
#endif

private:
	/** If true then only allow playback when the media plate is visible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPlate", meta = (AllowPrivateAccess = true))
	bool bPlayOnlyWhenVisible = false;

	/** If > 0, then this is the aspect ratio of our screen and 
	 * letterboxes will be added if the media is smaller than the screen. */
	UPROPERTY()
	float LetterboxAspectRatio = 0.0f;

	UPROPERTY()
	FVector2D MeshRange = FVector2D(360.0f, 180.0f);

	/** Name for our media component. */
	static FLazyName MediaComponentName;
	/** Name for our playlist. */
	static FLazyName MediaPlaylistName;

	/** Holds the media player. */
	UPROPERTY()
	TObjectPtr<UMediaTexture> MediaTexture;

	/** This component's media player */
	UPROPERTY()
	TObjectPtr<UMediaPlayer> MediaPlayer;

	/** Info representing this object. */
	TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> MediaTextureTrackerObject;
	/** Our media clock sink. */
	TSharedPtr<FMediaComponentClockSink, ESPMode::ThreadSafe> ClockSink;
	/** Game time when we paused playback. */
	float TimeWhenPlaybackPaused = 0.0f;
	/** True if our media should be playing when visible. */
	bool bWantsToPlayWhenVisible = false;

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

	/**
	 * Call this to see if this media plate is visible.
	 */
	bool IsVisible();

	/**
	 * Call this to resume playback when the media plate is visible.
	 */
	void ResumeWhenVisible();

	/**
	 * Returns the time to seek to when resuming playback.
	 */
	FTimespan GetResumeTime();

	/**
	 * Updates if we should tick or not based on current state.
	 */
	void UpdateTicking();

	/**
	 * Updates letterboxes based on the current state.
	 */
	void UpdateLetterboxes();

	/**
	 * Adds ability to have letterboxes.
	 */
	void AddLetterboxes();

	/**
	 * Removes ability to have letterboxes.
	 */
	void RemoveLetterboxes();

};
