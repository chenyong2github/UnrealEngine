// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MediaPlateComponent.generated.h"

class UMediaComponent;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;
struct FMediaTextureTrackerObject;

/**
 * This is a component for AMediaPlate that can play and show media in the world.
 */
UCLASS()
class MEDIAPLATE_API UMediaPlateComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	//~ UActorComponent interface.
	virtual void BeginPlay();

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

	/** Holds the media player. */
	UPROPERTY(Category = MediaPlate, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UMediaComponent> MediaComponent;

	/** URL (or file)  to play. This will take precedence over the MediaSource. */
	UPROPERTY(EditAnywhere, Category = MediaPlate)
	FFilePath MediaPath;

	/** What media to play. This will only be used if MediaPath is not valid. */
	UPROPERTY(EditAnywhere, Category = MediaPlate)
	TObjectPtr<UMediaSource> MediaSource;

	/**
	 * Adds our media texture to the media texture tracker.
	 */
	void RegisterWithMediaTextureTracker();
	/**
	 * Removes our texture from the media texture tracker.
	 */
	void UnregisterWithMediaTextureTracker();

private:
	/** Name for our media component. */
	static FLazyName MediaComponentName;

	/** Info representing this object. */
	TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> MediaTextureTrackerObject;

	/** If we are using MediaPath, then this is the media source for it. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaSource> MediaPathMediaSource;
};
