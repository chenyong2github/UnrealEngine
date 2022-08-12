// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MediaSourceManagerChannel.generated.h"

class UMaterialInstanceConstant;
class UMediaPlayer;
class UMediaSource;
class UMediaSourceManagerInput;
class UProxyMediaSource;
class UTexture;

#if WITH_EDITOR

DECLARE_EVENT(UMediaSourceManagerChannel, FOnInputPropertyChanged);

#endif

/**
* Handles a single channel for the MediaSourceManager.
*/
UCLASS(BlueprintType)
class MEDIASOURCEMANAGER_API UMediaSourceManagerChannel : public UObject
{
	GENERATED_BODY()

public:
	/** The name of this channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	FString Name;

	/** Our input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	TObjectPtr<UMediaSourceManagerInput> Input = nullptr;

	/** Our media sources for our inputs. */
	TObjectPtr<UMediaSource> InMediaSource = nullptr;

	/** Our persistent media source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	TObjectPtr<UProxyMediaSource> OutMediaSource = nullptr;

	/** The channel will output the media here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Channel")
	TObjectPtr<UTexture> OutTexture = nullptr;

	/** Material that uses our texture. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceConstant> Material = nullptr;

#if WITH_EDITOR
	/** This is broadcast when something on the input is changed. */
	FOnInputPropertyChanged OnInputPropertyChanged;
#endif // WITH_EDITOR

	/**
	 * Call this to get the media player.
	 */
	UMediaPlayer* GetMediaPlayer();

	/** Start playback. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSourceManager")
	void Play();

	/**
	 * Call this to make sure everything is set up.
	 */
	void Validate();

	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject interface

private:
	/** Stores the player. */
	UPROPERTY()
	TObjectPtr<UMediaPlayer> CurrentMediaPlayer = nullptr;

#if WITH_EDITOR
	
	/**
	 * Called when an object is edited.
	 */
	void OnObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

#endif // WITH_EDITOR
};
