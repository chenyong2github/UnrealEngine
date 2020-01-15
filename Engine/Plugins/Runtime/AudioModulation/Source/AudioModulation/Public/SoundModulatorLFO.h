// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DSP/LFO.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulatorBase.h"
#include "SoundModulationValue.h"

#include "SoundModulatorLFO.generated.h"

UENUM(BlueprintType)
enum class ESoundModulatorLFOShape : uint8
{
	Sine			  UMETA(DisplayName = "Sine"),
	UpSaw			  UMETA(DisplayName = "Saw (Up)"),
	DownSaw			  UMETA(DisplayName = "Saw (Down)"),
	Square			  UMETA(DisplayName = "Square"),
	Triangle		  UMETA(DisplayName = "Triangle"),
	Exponential		  UMETA(DisplayName = "Exponential"),
	RandomSampleHold  UMETA(DisplayName = "Random"),

	COUNT UMETA(Hidden)
};

UCLASS(BlueprintType, hidecategories = Object, editinlinenew, MinimalAPI)
class USoundBusModulatorLFO : public USoundBusModulatorBase
{
	GENERATED_UCLASS_BODY()

public:
	virtual void BeginDestroy() override;

	/** Shape of oscillating waveform */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	ESoundModulatorLFOShape Shape;

	/** Amplitude of oscillator */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Amplitude;

	/** Frequency of oscillator */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "20", ClampMin = "0", ClampMax = "20"))
	float Frequency;

	/** Offset of oscillator */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"))
	float Offset;

	/** Whether or not to loop the oscillation more than once */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	uint8 bLooping : 1;

	/** Whether or not to bypass the LFO from being included in mix. */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	uint8 bBypass: 1;
};
