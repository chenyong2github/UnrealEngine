// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "MediaPlate.generated.h"

class UMediaComponent;
class UMediaPlayer;
class UMediaSource;
class UMediaTexture;

/**
 * MediaPlate is an actor that can play and show media in the world.
 */
UCLASS()
class MEDIAPLATE_API AMediaPlate : public AStaticMeshActor
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin AActor Interface
	virtual void PostRegisterAllComponents() override;
	//~ End AActor Interface

	/**
	 * Call this get our media player.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaPlate")
	UMediaPlayer* GetMediaPlayer();

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

	/** Holds the media player. */
	UPROPERTY(Category = MediaPlate, VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UMediaComponent> MediaComponent;

	/** What media to play. */
	UPROPERTY(EditAnywhere, Category = MediaPlate)
	TObjectPtr<UMediaSource> MediaSource;

private:
	/** Name for our media component. */
	static FLazyName MediaComponentName;
	/** Name for the media texture parameter in the material. */
	static FLazyName MediaTextureName;
};
