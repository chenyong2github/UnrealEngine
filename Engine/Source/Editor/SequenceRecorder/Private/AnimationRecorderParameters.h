// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Animation/AnimationRecordingSettings.h"
#include "AnimationRecorderParameters.generated.h"

UCLASS(config = EditorPerProjectUserSettings)
class UAnimationRecordingParameters : public UObject
{
	GENERATED_BODY()

public:
	float GetRecordingDurationSeconds();
	float GetRecordingSampleRate();

	/** Sample rate of the recorded animation (in Hz) */
	UPROPERTY(EditAnywhere, config, Category = Options, meta = (UIMin = "1.0", ClampMin = "1.0", UIMax = "60.0", ClampMax = "60.0"))
	float SampleRate = FAnimationRecordingSettings::DefaultSampleRate;

	/** If enabled, this animation recording will automatically end after a set amount of time */
	UPROPERTY(EditAnywhere, config, Category = Options)
	bool bEndAfterDuration = false;

	/** The maximum duration of this animation recording */
	UPROPERTY(EditAnywhere, config, Category = Options, meta = (EditCondition = "bEndAfterDuration", UIMin = "5.0", ClampMin = "5.0"))
	float MaximumDurationSeconds = FAnimationRecordingSettings::DefaultMaximumLength;
};
