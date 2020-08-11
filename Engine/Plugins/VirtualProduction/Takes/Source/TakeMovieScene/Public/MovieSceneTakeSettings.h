// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTakeSettings.generated.h"

/**
 * Universal take recorder settings that apply to a whole take
 */
UCLASS(config=EditorSettings, MinimalAPI)
class UMovieSceneTakeSettings : public UObject
{
public:
	GENERATED_BODY()

	UMovieSceneTakeSettings();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Hours text */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Take Recorder")
	FText HoursText;

	/** Minutes text */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Take Recorder")
	FText MinutesText;

	/** Seconds text */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Take Recorder")
	FText SecondsText;

	/** Frames text */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Take Recorder")
	FText FramesText;

	/** SubFrames text */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Take Recorder")
	FText SubFramesText;

	/** Slate text */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Take Recorder")
	FText SlateText;
};
