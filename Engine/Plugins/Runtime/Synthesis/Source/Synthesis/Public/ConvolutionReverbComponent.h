// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/AudioComponent.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Engine/EngineTypes.h"
#include "Engine/DataTable.h"
#include "Sound/SoundEffectSubmix.h"
#include "ImpulseResponseAsset.h"
#include "AudioEffect.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ConvolutionAlgorithmWrapper.h"
#include "ConvolutionReverbComponent.generated.h"

 
// ========================================================================
// FConvolutionReverbSettings:
// 
// settings component for FConvolutionReverb submix effect
// ========================================================================


USTRUCT(BlueprintType)
struct SYNTHESIS_API FConvolutionReverbSettings
{
	GENERATED_USTRUCT_BODY()

	FConvolutionReverbSettings();

	/* Information about your parameter described here will show up in the editor as a tool-tip in the effect preset object */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundEffect)
	UImpulseResponse* ImpulseResponse;

	/* Opt into hardware acceleration of the convolution reverb (if available) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundEffect)
	bool AllowHardwareAcceleration;

	/* Used to account for energy added by convolution with "loud" Impulse Responses.  Not meant to be updated dynamically */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundEffect)
	float PostNormalizationVolume_decibels;

	/* Amout of audio to be sent to rear channels in quad/surround configurations (linear gain, < 0 = phase inverted) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ClampMin = "-5.0", UIMin = "-5.0", ClampMax = "5.0", UIMax = "5.0"))
	float SurroundRearChannelBleedAmount;

	/* If true, send Surround Rear Channel Bleed Amount sends front left to back right and vice versa */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset)
	bool bSurroundRearChannelFlip;
};

// ========================================================================
// UConvolutionReverbPreset:
//
// Class which processes audio streams and uses parameters defined in the preset class.
// This class is instanced for each submix effect preset in a submix effect chain.
// ========================================================================

class SYNTHESIS_API FConvolutionReverb : public FSoundEffectSubmix
{
public:
	FConvolutionReverb();
	~FConvolutionReverb();

	// Being FSoundEffectSubmix Implementation
	// Called on an audio effect at initialization on main thread before audio processing begins.
	virtual void Init(const FSoundEffectSubmixInitData& InitData) override;

	// Called when an audio effect preset is changed
	virtual void OnPresetChanged() override;

	// Process the input block of audio. Called on audio thread.
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

	// We want to receive down-mixed submix audio to stereo input for the reverb effect
	virtual uint32 GetDesiredInputChannelCountOverride() const override;
	// End FSoundEffectSubmix Implementation

private:
	Audio::FConvolutionAlgorithmWrapper ConvolutionAlgorithm;
	TArray<float> ScratchInputBuffer;
	TArray<float> ScratchOutputBuffer;
	TWeakObjectPtr<UImpulseResponse> CachedIrAssetPtr;
	FThreadSafeBool bIrIsDirty; // need to rebuild ConvolutionAlgorithm if true
	bool bSettingsAreDirty;
	bool bIrBeingRebuilt;
	float SampleRate;

	// members to update in OnPresetChanged()
	float CachedOutputGain;
	float RearChanBleed;
	bool bRearChanFlip;
	bool bCachedEnableHardwareAcceleration;
};

// ========================================================================
// UConvolutionReverbPreset
// This code exposes your preset settings and effect class to the editor.
// ========================================================================

UCLASS()
class SYNTHESIS_API UConvolutionReverbPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(ConvolutionReverb)

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetSettings(const FConvolutionReverbSettings& InSettings);

	// ConvolutionReverbPreset Preset Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset)
	FConvolutionReverbSettings Settings;
};

