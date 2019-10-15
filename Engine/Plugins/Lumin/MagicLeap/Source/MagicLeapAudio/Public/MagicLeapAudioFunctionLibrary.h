// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
};
