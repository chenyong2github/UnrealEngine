// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConvolutionReverb.h"
#include "DSP/Dsp.h"
#include "DSP/ConvolutionAlgorithm.h"
#include "Sound/SoundEffectSubmix.h"
#include "SubmixEffectConvolutionReverb.generated.h"

/** Block size of convolution reverb algorithm. */
UENUM()
enum class ESubmixEffectConvolutionReverbBlockSize : uint8
{
	/** 256 audio frames per a block. */
	BlockSize256,

	/** 512 audio frames per a block. */
	BlockSize512,

	/** 1024 audio frames per a block. */
	BlockSize1024
};

#if WITH_EDITORONLY_DATA
DECLARE_MULTICAST_DELEGATE_OneParam(FAudioImpulseResponsePropertyChange, FPropertyChangedEvent&);
#endif

// ========================================================================
// UAudioImpulseResponse
// UAsset used to represent Imported Impulse Responses
// ========================================================================
UCLASS()
class SYNTHESIS_API UAudioImpulseResponse : public UObject
{
	GENERATED_BODY()

public:
	UAudioImpulseResponse();
	/** The interleaved audio samples used in convolution. */
	UPROPERTY()
	TArray<float> ImpulseResponse;

	/** The number of channels in impulse response. */
	UPROPERTY()
	int32 NumChannels;

	/** The original sample rate of the impulse response. */
	UPROPERTY()
	int32 SampleRate;

	/* Used to account for energy added by convolution with "loud" Impulse Responses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ClamMin = "-60.0", ClampMax = "15.0"))
	float NormalizationVolumeDb;

	UPROPERTY(meta = ( DeprecatedProperty ) )
	TArray<float> IRData_DEPRECATED;

#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// This delegate is called whenever PostEditChangeProperty called.
	FAudioImpulseResponsePropertyChange OnObjectPropertyChanged;
#endif
};


USTRUCT(BlueprintType)
struct SYNTHESIS_API FSubmixEffectConvolutionReverbSettings
{
	GENERATED_USTRUCT_BODY()

	FSubmixEffectConvolutionReverbSettings();

	/* Used to account for energy added by convolution with "loud" Impulse Responses. 
	 * This value is not directly editable in the editor because it is copied from the 
	 * associated UAudioImpulseResponse. */
	UPROPERTY();
	float NormalizationVolumeDb;

	/* Amout of audio to be sent to rear channels in quad/surround configurations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ClampMin = "-60.0", UIMin = "-60.0", ClampMax = "15.0", UIMax = "15.0"))
	float SurroundRearChannelBleedDb;

	/* If true, rear channel bleed sends will have their phase inverted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SumixEffectPreset)
	bool bInvertRearChannelBleedPhase;

	/* If true, send Surround Rear Channel Bleed Amount sends front left to back right and vice versa */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset)
	bool bSurroundRearChannelFlip;

	UPROPERTY(meta = ( DeprecatedProperty ) )
	float SurroundRearChannelBleedAmount_DEPRECATED;

	UPROPERTY(meta = ( DeprecatedProperty ) )
	UAudioImpulseResponse* ImpulseResponse_DEPRECATED;

	UPROPERTY(meta = ( DeprecatedProperty ) )
	bool AllowHardwareAcceleration_DEPRECATED;

};


/** Audio render thread effect object. */
class SYNTHESIS_API FSubmixEffectConvolutionReverb : public FSoundEffectSubmix
{
public:
	// Gain entry into convolution matrix
	struct FConvolutionGainEntry
	{
		int32 InputIndex = 0;
		int32 ImpulseIndex = 0;
		int32 OutputIndex = 0;
		float Gain = 0.f;

		FConvolutionGainEntry(int32 InInputIndex, int32 InImpulseIndex, int32 InOutputIndex, float InGain)
		:	InputIndex(InInputIndex)
		,	ImpulseIndex(InImpulseIndex)
		,	OutputIndex(InOutputIndex)
		,	Gain(InGain)
		{
		}
	};

	// Data used to initialize the convolution algorithm
	struct FConvolutionAlgorithmInitData
	{
		Audio::FConvolutionSettings AlgorithmSettings;
		float ImpulseSampleRate = 0.f;
		float TargetSampleRate = 0.f;
		TArray<FConvolutionGainEntry> Gains;
		TArray<float> Samples;
	};

	typedef int32 FConvolutionAlgorithmID;

	// Data used to track versions of current running convolution.
	struct FVersionData
	{
		FConvolutionAlgorithmID ConvolutionID;

		FVersionData();
		bool operator==(const FVersionData& Other) const;
	};

	
	FSubmixEffectConvolutionReverb() = delete;
	// Construct a convolution object with an existing preset. 
	FSubmixEffectConvolutionReverb(const USubmixEffectConvolutionReverbPreset* InPreset);

	~FSubmixEffectConvolutionReverb();

	// Called on an audio effect at initialization on main thread before audio processing begins.
	virtual void Init(const FSoundEffectSubmixInitData& InitData) override;

	// Called when an audio effect preset settings is changed.
	virtual void OnPresetChanged() override;

	// Process the input block of audio. Called on audio thread.
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

	// We want to receive down-mixed submix audio to stereo input for the reverb effect
	virtual uint32 GetDesiredInputChannelCountOverride() const override;

	// Call on the game thread in order to update the impulse response and hardware acceleration
	// used in this submix effect.
	FVersionData UpdateConvolutionAlgorithm(const USubmixEffectConvolutionReverbPreset* InPreset);

	// Sets the convolution algorithm object used if the version data matches the most up-to-date
	// version data tracked within this object. This method must be called on the audio render thread.
	void SetConvolutionAlgorithmIfCurrent(TUniquePtr<Audio::IConvolutionAlgorithm> InAlgo, const FVersionData& InVersionData);

	// Returns the data needed to build a convolution algorithm.
	FConvolutionAlgorithmInitData GetConvolutionAlgorithmInitData() const;

private:

	// Increments the internal verion number and returns a copy of the
	// latest version information.
	FVersionData UpdateVersion();

	// Updates parameters on the render thread.
	void UpdateParameters();

	// Params object for copying between audio render thread and outside threads.
	Audio::TParams<Audio::FConvolutionReverbSettings> Params;

	// ConvolutionReverb performs majority of DSP operations
	Audio::FConvolutionReverb ConvolutionReverb;

	// Audio sample rate
	float SampleRate;

	// Number of input and output channels. These are TAtomic
	// since they can be accessed on multiple threads.
	TAtomic<int32> NumInputChannels;
	TAtomic<int32> NumOutputChannels;

	mutable FCriticalSection VersionDataCriticalSection;
	FVersionData VersionData;

	struct FIRAssetData 
	{
		TArray<float> Samples;
		int32 NumChannels;
		float SampleRate;
		int32 BlockSize;
		bool bEnableHardwareAcceleration;

		FIRAssetData();
	};

	mutable FCriticalSection IRAssetCriticalSection;
	// Internal copy of impulse response asset data.
	FIRAssetData IRAssetData;
};

UCLASS()
class SYNTHESIS_API USubmixEffectConvolutionReverbPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	
	USubmixEffectConvolutionReverbPreset(const FObjectInitializer& ObjectInitializer);

	/* Note: The SubmixEffect boilerplate macros could not be utilized here because
	 * the "CreateNewEffect" implementation differed from those available in the
	 * boilerplate macro.
	 */

	virtual bool CanFilter() const override;

	virtual bool HasAssetActions() const;

	virtual FText GetAssetActionName() const override;

	virtual UClass* GetSupportedClass() const override;

	virtual FSoundEffectBase* CreateNewEffect() const override;

	virtual USoundEffectPreset* CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const override;

	virtual void Init() override;


	FSubmixEffectConvolutionReverbSettings GetSettings() const;

	/** Set the convolution reverb settings */
	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetSettings(const FSubmixEffectConvolutionReverbSettings& InSettings);

	/** Set the convolution reverb impulse response */
	UFUNCTION(BlueprintCallable, BlueprintSetter, Category = "Audio|Effects")
	void SetImpulseResponse(UAudioImpulseResponse* InImpulseResponse);

	/** ConvolutionReverbPreset Preset Settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSettings, Category = SubmixEffectPreset)
	FSubmixEffectConvolutionReverbSettings Settings;

	/** The impulse response used for convolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetImpulseResponse, Category = SubmixEffectPreset)
	UAudioImpulseResponse* ImpulseResponse;

	/** Set the internal block size. This can effect latency and performance. Higher values will result in
	 * lower CPU costs while lower values will result higher CPU costs. Latency may be affected depending
	 * on the interplay between audio engines buffer sizes and this effects block size. Generally, higher
	 * values result in higher latency, and lower values result in lower latency. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = SubmixEffectPreset)
	ESubmixEffectConvolutionReverbBlockSize BlockSize;

	/** Opt into hardware acceleration of the convolution reverb (if available) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = SubmixEffectPreset)
	bool bEnableHardwareAcceleration;
	

#if WITH_EDITORONLY_DATA
	// Binds to the UAudioImpulseRespont::OnObjectPropertyChanged delegate of the current ImpulseResponse
	void BindToImpulseResponseObjectChange();

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// Called when a property changes on teh ImpulseResponse object
	void PostEditChangeImpulseProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif

	virtual void PostLoad() override;

private:

	void UpdateSettings();

	void UpdateDeprecatedProperties();

	// This method requires that the submix effect is registered with a preset.  If this 
	// submix effect is not registered with a preset, then this will not update the convolution
	// algorithm.
	void UpdateEffectConvolutionAlgorithm();

	mutable FCriticalSection SettingsCritSect; 
	FSubmixEffectConvolutionReverbSettings SettingsCopy; 

#if WITH_EDITORONLY_DATA

	TMap<UObject*, FDelegateHandle> DelegateHandles;
#endif
};

