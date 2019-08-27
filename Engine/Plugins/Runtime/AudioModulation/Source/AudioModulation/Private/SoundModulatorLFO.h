// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
class USoundModulatorLFO : public USoundModulatorBase
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

	/** Automatically activates/deactivates LFO when sounds referencing LFO asset are playing */
	UPROPERTY(EditAnywhere, Category = Modulation, BlueprintReadWrite)
	uint8 bAutoActivate : 1;
};

namespace AudioModulation
{
	class FModulatorLFOProxy
	{
	public:
		FModulatorLFOProxy();
		FModulatorLFOProxy(const USoundModulatorLFO& InLFO);

		float GetAmplitude() const;
		bool GetAutoActivate() const;
		float GetFreq() const;
		LFOId GetId() const;
#if !UE_BUILD_SHIPPING
		const FString& GetName() const;
#endif // !UE_BUILD_SHIPPING
		float GetOffset() const;
		float GetValue() const;
		void SetFreq(float InFreq);
		void Update(float InElapsed);

		int32 DecRefSound();
		int32 IncRefSound();

	private:
		LFOId Id;

#if !UE_BUILD_SHIPPING
		FString Name;
#endif // !UE_BUILD_SHIPPING

		Audio::FLFO LFO;
		float Offset;
		float Value;

		uint8 bAutoActivate : 1;
		int32 SoundRefCount;
	};
	using LFOProxyMap = TMap<BusId, FModulatorLFOProxy>;
} // namespace AudioModulation
