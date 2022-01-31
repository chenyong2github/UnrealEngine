// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "MediaPlate.generated.h"

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

	/** Our player to play the media with. */
	UPROPERTY(transient, EditAnywhere, Category = Media)
	UMediaPlayer* MediaPlayer;

	/** Our texture that contains the media. */
	UPROPERTY(transient, EditAnywhere, Category = Media)
	UMediaTexture* MediaTexture;

	/** What media to play. */
	UPROPERTY(transient, EditAnywhere, Category = Media)
	UMediaSource* MediaSource;
};
