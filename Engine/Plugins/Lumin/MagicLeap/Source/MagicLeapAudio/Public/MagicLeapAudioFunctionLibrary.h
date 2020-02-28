// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapAudioTypes.h"
#include "MagicLeapAudioFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPAUDIO_API UMagicLeapAudioFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 
		Sets the delegate used to notify that an audio device has been plugged into the audio jack.
		@param ResultDelegate The delegate that will be notified when an audio device has been plugged into the audio jack.
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio Function Library | MagicLeap")
	static bool SetOnAudioJackPluggedDelegate(const FMagicLeapAudioJackPluggedDelegate& ResultDelegate);

	/**
		Sets the delegate used to notify that an audio device has been unplugged from the audio jack.
		@param ResultDelegate The delegate that will be notified when an audio device has been unplugged from the audio jack.
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio Function Library | MagicLeap")
	static bool SetOnAudioJackUnpluggedDelegate(const FMagicLeapAudioJackUnpluggedDelegate& ResultDelegate);

	/**
		Mute or unmute all microphone capture.
		Note: When mic capture is muted or unmuted by one app, it is muted or unmuted for all apps.
		Note: this setting is separate from any muting done by the audio policy manager (such as when the "reality button"
		is pressed).
		@param IsMuted Boolean value indicating whether or not to mute
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio Function Library | MagicLeap")
	static bool SetMicMute(bool IsMuted);

	/**
		Returns whether all microphone capture is muted or not.
	 */
	UFUNCTION(BlueprintPure, Category = "Audio Function Library | MagicLeap")
	static bool IsMicMuted();
};
