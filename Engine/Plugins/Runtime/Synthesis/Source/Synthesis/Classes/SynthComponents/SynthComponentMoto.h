// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "MotoSynthSourceAsset.h"
#include "SynthComponentMoto.generated.h"


UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class SYNTHESIS_API USynthComponentMoto : public USynthComponent
{
	GENERATED_BODY()

	USynthComponentMoto(const FObjectInitializer& ObjInitializer);
	virtual ~USynthComponentMoto();

public:
	/* The moto synth grain source derived from the sound wave asset action to convert from a sound wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth")
	UMotoSynthSource* AccelerationSource;

	/* The moto synth grain source derived from the sound wave asset action to convert from a sound wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth")
	UMotoSynthSource* DecelerationSource;

	/* Enables a synth tone to play with the grain engine (helps with RPM settings and validations). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth")
	bool bEnableSynthTone = false;

	/* Sets volume of the synth tone if its enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "bEnableSynthTone"))
	float SynthToneVolume = 1.0f;

	/* Enables a the granular engine playback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth")
	bool bEnableGranularEngine = true;

	/* Sets the volume of the granular engine if it's enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "bEnableGranularEngine"))
	float GranularEngineVolume = 1.0f;

	/* Sets the starting RPM of the engine */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotoSynth", meta = (ClampMin = "500.0", ClampMax = "20000.0", UIMin = "500.0", UIMax = "20000.0"))
	float RPM = 1000.0f;

	/** Sets the RPM of the granular engine directly. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	void SetRPM(float InRPM, float InTimeSec);

	/** Retrieves RPM range of the moto synth, taking into account the acceleration and deceleration sources. The min RPM is the largest of the min RPms of either and the max RPM is min of the max RPMs of either. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	void GetRPMRange(float& OutMinRPM, float& OutMaxRPM);

	/** Sets if the synth tone is enabled. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	void SetSynthToneEnabled(bool bInEnabled);
	
	/** Sets the synth tone volume. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	void SetSynthToneVolume(float Volume);

	/** Sets if the granular engine is enabled. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	void SetGranularEngineEnabled(bool bInEnabled);

	/** Sets granular engine volume. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	void SetGranularEngineVolume(float Volume);

	/** Returns if the moto synth is enabled. */
	UFUNCTION(BlueprintCallable, Category = "MotoSynth")
	bool IsEnabled() const;

	virtual ISoundGeneratorPtr CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels) override;

private:
	FVector2D RPMRange;
	ISoundGeneratorPtr MotoSynthEngine;
};
