// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "ISoundfieldFormat.h"
#include "IAudioEndpoint.h"
#include "ISoundfieldEndpoint.h"
#include "SampleBufferIO.h"
#include "SoundEffectSubmix.h"
#include "SoundSubmixSend.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "DSP/SpectrumAnalyzer.h"

#include "SoundSubmix.generated.h"


// Forward Declarations
class UEdGraph;
class USoundEffectSubmixPreset;
class USoundSubmix;
class ISubmixBufferListener;



/**
* Called when a recorded file has finished writing to disk.
*
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubmixRecordedFileDone, const USoundWave*, ResultingSoundWave);

/**
* Called when a new submix envelope value is generated on the given audio device id (different for multiple PIE). Array is an envelope value for each channel.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubmixEnvelope, const TArray<float>&, Envelope);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSubmixSpectralAnalysis, const TArray<float>&, Magnitudes);


UENUM(BlueprintType)
enum class EFFTSize : uint8
{
	// 512
	DefaultSize,

	// 64
	Min,

	// 256
	Small,

	// 512
	Medium,

	// 1024
	Large,

	// 2048
	VeryLarge, 

	// 4096
	Max
};

UENUM()
enum class EFFTPeakInterpolationMethod : uint8
{
	NearestNeighbor,
	Linear,
	Quadratic,
	ConstantQ,
};

UENUM()
enum class EFFTWindowType : uint8
{
	// No window is applied. Technically a boxcar window.
	None,

	// Mainlobe width of -3 dB and sidelove attenuation of ~-40 dB. Good for COLA.
	Hamming,

	// Mainlobe width of -3 dB and sidelobe attenuation of ~-30dB. Good for COLA.
	Hann,

	// Mainlobe width of -3 dB and sidelobe attenuation of ~-60db. Tricky for COLA.
	Blackman
};

UENUM(BlueprintType)
enum class EAudioSpectrumType : uint8
{
	// Spectrum frequency values are equal to magnitude of frequency.
	MagnitudeSpectrum,

	// Spectrum frequency values are equal to magnitude squared.
	PowerSpectrum,

	// Returns decibels (0.0 dB is 1.0)
	Decibel,
};

struct FSoundSpectrumAnalyzerSettings
{
	// FFTSize used in spectrum analyzer.
	EFFTSize FFTSize;

	// Type of window to apply to audio.
	EFFTWindowType WindowType;

	// Metric used when analyzing spectrum. 
	EAudioSpectrumType SpectrumType;

	// Interpolation method used when getting frequencies.
	EFFTPeakInterpolationMethod  InterpolationMethod;

	// Hopsize between audio windows as a ratio of the FFTSize.
	float HopSize;
};

struct FSoundSpectrumAnalyzerDelegateSettings
{
	// Settings for individual bands.
	TArray<FSoundSubmixSpectralAnalysisBandSettings> BandSettings; 

	// Number of times a second the delegate is triggered. 
	float UpdateRate; 

	// The decibel level considered silence.
	float DecibelNoiseFloor; 

	// If true, returned values are scaled between 0 and 1.
	bool bDoNormalize; 

	// If true, the band values are tracked to always have values between 0 and 1. 
	bool bDoAutoRange; 

	// The time in seconds for the range to expand to a new observed range.
	float AutoRangeAttackTime; 

	// The time in seconds for the range to shrink to a new observed range.
	float AutoRangeReleaseTime;
};


#if WITH_EDITOR

/** Interface for sound submix graph interaction with the AudioEditor module. */
class ISoundSubmixAudioEditor
{
public:
	virtual ~ISoundSubmixAudioEditor() {}

	/** Refreshes the sound class graph links. */
	virtual void RefreshGraphLinks(UEdGraph* SoundClassGraph) = 0;
};
#endif

UCLASS(config = Engine, abstract, hidecategories = Object, editinlinenew, BlueprintType)
class ENGINE_API USoundSubmixBase : public UObject
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITORONLY_DATA
	/** EdGraph based representation of the SoundSubmix */
	UEdGraph* SoundSubmixGraph;
#endif

	// Child submixes to this sound mix
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	TArray<TObjectPtr<USoundSubmixBase>> ChildSubmixes;

protected:
	//~ Begin UObject Interface.
	virtual FString GetDesc() override;
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;

public:
	// Sound Submix Editor functionality
#if WITH_EDITOR

	/**
	* @return true if the child sound class exists in the tree
	*/
	bool RecurseCheckChild(const USoundSubmixBase* ChildSoundSubmix) const;

	/**
	* Add Referenced objects
	*
	* @param	InThis SoundSubmix we are adding references from.
	* @param	Collector Reference Collector
	*/
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

protected:

#if WITH_EDITOR
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject Interface.

private:
	static TArray<USoundSubmixBase*> BackupChildSubmixes;
#endif // WITH_EDITOR
};

/**
 * This submix class can be derived from for submixes that output to a parent submix.
 */
UCLASS(config = Engine, abstract, hidecategories = Object, editinlinenew, BlueprintType)
class ENGINE_API USoundSubmixWithParentBase : public USoundSubmixBase
{
	GENERATED_UCLASS_BODY()
public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	TObjectPtr<USoundSubmixBase> ParentSubmix;

	/**
	* Set the parent submix of this SoundSubmix, removing it as a child from its previous owner
	*
	* @param	InParentSubmix	The New Parent Submix of this
	*/
	void SetParentSubmix(USoundSubmixBase* InParentSubmix);

protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
#endif 
};

// Whether to use linear or decibel values for audio gains
UENUM(BlueprintType)
enum class EGainParamMode : uint8
{
	Linear = 0,
	Decibels,
};

/**
 * Sound Submix class meant for applying an effect to the downmixed sum of multiple audio sources.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType)
class ENGINE_API USoundSubmix : public USoundSubmixWithParentBase
{
	GENERATED_UCLASS_BODY()

public:

	/** Mute this submix when the application is muted or in the background. Used to prevent submix effect tails from continuing when tabbing out of application or if application is muted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	uint8 bMuteWhenBackgrounded : 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	TArray<TObjectPtr<USoundEffectSubmixPreset>> SubmixEffectChain;

	/** Optional settings used by plugins which support ambisonics file playback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundSubmix)
	TObjectPtr<USoundfieldEncodingSettingsBase> AmbisonicsPluginSettings;

	/** The attack time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the envelope value of sounds played with this submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnvelopeFollower, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerAttackTime;

	/** The release time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the envelope value of sounds played with this submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnvelopeFollower, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerReleaseTime;

	/** Whether to treat submix gain levels as linear or decibel values. */
	UPROPERTY(EditAnywhere, Category = SubmixLevel, meta = (InlineCategoryProperty))
	EGainParamMode GainMode;

	/** The output volume of the submix. Applied after submix effects and analysis are performed.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixLevel, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "GainMode == EGainParamMode::Linear", DisplayName = "Output Volume", EditConditionHides))
	float OutputVolume;

	/** The wet level of the submix. Applied after submix effects and analysis are performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixLevel, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "GainMode == EGainParamMode::Linear", DisplayName = "Wet Level", EditConditionHides))
	float WetLevel;

	/** The dry level of the submix. Applied before submix effects and analysis are performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixLevel, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "GainMode == EGainParamMode::Linear", DisplayName = "Dry Level", EditConditionHides))
	float DryLevel;

#if WITH_EDITORONLY_DATA
	/** The output volume of the submix (in dB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixLevel, meta = (ClampMin = "-160.0", ClampMax = "0.0", UIMin = "-60.0", UIMax = "0.0", EditCondition = "GainMode == EGainParamMode::Decibels", DisplayName = "Output Volume (dB)", EditConditionHides))
	float OutputVolumeDB;

	/** The wet level of the submix  (in dB). Applied after submix effects and analysis are performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixLevel, meta = (ClampMin = "-160.0", ClampMax = "0.0", UIMin = "-60.0", UIMax = "0.0", EditCondition = "GainMode == EGainParamMode::Decibels", DisplayName = "Wet Level (dB)", EditConditionHides))
	float WetLevelDB;

	/** The dry level of the submix  (in dB)s. Applied before submix effects and analysis are performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixLevel, meta = (ClampMin = "-160.0", ClampMax = "0.0", UIMin = "-60.0", UIMax = "0.0", EditCondition = "GainMode == EGainParamMode::Decibels", DisplayName = "Dry Level (dB)", EditConditionHides))
	float DryLevelDB;
#endif

	// Blueprint delegate for when a recorded file is finished exporting.
	UPROPERTY(BlueprintAssignable)
	FOnSubmixRecordedFileDone OnSubmixRecordedFileDone;

	// Start recording the audio from this submix.
	UFUNCTION(BlueprintCallable, Category = "Audio|Bounce", meta = (WorldContext = "WorldContextObject", DisplayName = "Start Recording Submix Output"))
	void StartRecordingOutput(const UObject* WorldContextObject, float ExpectedDuration);

	void StartRecordingOutput(FAudioDevice* InDevice, float ExpectedDuration);

	// Finish recording the audio from this submix and export it as a wav file or a USoundWave.
	UFUNCTION(BlueprintCallable, Category = "Audio|Bounce", meta = (WorldContext = "WorldContextObject", DisplayName = "Finish Recording Submix Output"))
	void StopRecordingOutput(const UObject* WorldContextObject, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite = nullptr);

	void StopRecordingOutput(FAudioDevice* InDevice, EAudioRecordingExportType ExportType, const FString& Name, FString Path, USoundWave* ExistingSoundWaveToOverwrite = nullptr);

	// Start envelope following the submix output. Register with OnSubmixEnvelope to receive envelope follower data in BP.
	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void StartEnvelopeFollowing(const UObject* WorldContextObject);

	void StartEnvelopeFollowing(FAudioDevice* InDevice);

	// Start envelope following the submix output. Register with OnSubmixEnvelope to receive envelope follower data in BP.
	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void StopEnvelopeFollowing(const UObject* WorldContextObject);

	void StopEnvelopeFollowing(FAudioDevice* InDevice);

	/**
	 *	Adds an envelope follower delegate to the submix when envelope following is enabled on this submix.
	 *	@param	OnSubmixEnvelopeBP	Event to fire when new envelope data is available.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void AddEnvelopeFollowerDelegate(const UObject* WorldContextObject, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP);

	/**
	 *	Adds a spectral analysis delegate to receive notifications when this submix has spectrum analysis enabled.
	 *	@param	InBandsettings					The frequency bands to analyze and their envelope-following settings.
	 *  @param  OnSubmixSpectralAnalysisBP		Event to fire when new spectral data is available.
	 *	@param	UpdateRate						How often to retrieve the data from the spectral analyzer and broadcast the event. Max is 30 times per second.
	 *	@param  InterpMethod                    Method to used for band peak calculation.
	 *	@param  SpectrumType                    Metric to use when returning spectrum values.
	 *	@param  DecibelNoiseFloor               Decibel Noise Floor to consider as silence silence when using a Decibel Spectrum Type.
	 *	@param  bDoNormalize                    If true, output band values will be normalized between zero and one.
	 *	@param  bDoAutoRange                    If true, output band values will have their ranges automatically adjusted to the minimum and maximum values in the audio. Output band values will be normalized between zero and one.
	 *	@param  AutoRangeAttackTime             The time (in seconds) it takes for the range to expand to 90% of a larger range.
	 *	@param  AutoRangeReleaseTime            The time (in seconds) it takes for the range to shrink to 90% of a smaller range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Spectrum", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 3))
	void AddSpectralAnalysisDelegate(const UObject* WorldContextObject, const TArray<FSoundSubmixSpectralAnalysisBandSettings>& InBandSettings, const FOnSubmixSpectralAnalysisBP& OnSubmixSpectralAnalysisBP, float UpdateRate = 10.f, float DecibelNoiseFloor=-40.f, bool bDoNormalize = true, bool bDoAutoRange = false, float AutoRangeAttackTime = 0.1f, float AutoRangeReleaseTime = 60.f);

	/**
	 *	Remove a spectral analysis delegate.
	 *  @param  OnSubmixSpectralAnalysisBP		The event delegate to remove.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Spectrum", meta = (WorldContext = "WorldContextObject"))
	void RemoveSpectralAnalysisDelegate(const UObject* WorldContextObject, const FOnSubmixSpectralAnalysisBP& OnSubmixSpectralAnalysisBP);

	/** Start spectrum analysis of the audio output. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Analysis", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	void StartSpectralAnalysis(const UObject* WorldContextObject, EFFTSize FFTSize = EFFTSize::DefaultSize, EFFTPeakInterpolationMethod InterpolationMethod = EFFTPeakInterpolationMethod::Linear, EFFTWindowType WindowType = EFFTWindowType::Hann, float HopSize = 0, EAudioSpectrumType SpectrumType = EAudioSpectrumType::MagnitudeSpectrum);

	void StartSpectralAnalysis(FAudioDevice* InDevice, EFFTSize FFTSize = EFFTSize::DefaultSize, EFFTPeakInterpolationMethod InterpolationMethod = EFFTPeakInterpolationMethod::Linear, EFFTWindowType WindowType = EFFTWindowType::Hann, float HopSize = 0, EAudioSpectrumType SpectrumType = EAudioSpectrumType::MagnitudeSpectrum);

	/** Start spectrum analysis of the audio output. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Analysis", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 1))
	void StopSpectralAnalysis(const UObject* WorldContextObject);

	void StopSpectralAnalysis(FAudioDevice* InDevice);

	/** Sets the output volume of the submix. This dynamic volume acts as a multiplier on the OutputVolume property of this submix.  */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	void SetSubmixOutputVolume(const UObject* WorldContextObject, float InOutputVolume);

	static FSoundSpectrumAnalyzerSettings GetSpectrumAnalyzerSettings(EFFTSize FFTSize, EFFTPeakInterpolationMethod InterpolationMethod, EFFTWindowType WindowType, float HopSize, EAudioSpectrumType SpectrumType);

	static FSoundSpectrumAnalyzerDelegateSettings GetSpectrumAnalysisDelegateSettings(const TArray<FSoundSubmixSpectralAnalysisBandSettings>& InBandSettings, float UpdateRate, float DecibelNoiseFloor, bool bDoNormalize, bool bDoAutoRange, float AutoRangeAttackTime, float AutoRangeReleaseTime);
protected:

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// State handling for bouncing output.
	TUniquePtr<Audio::FAudioRecordingData> RecordingData;
};
	

/**
 * Sound Submix class meant for use with soundfield formats, such as Ambisonics.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta = (DisplayName = "Sound Submix Soundfield"))
class ENGINE_API USoundfieldSubmix : public USoundSubmixWithParentBase
{
	GENERATED_UCLASS_BODY()

public:
	ISoundfieldFactory* GetSoundfieldFactoryForSubmix() const;
	const USoundfieldEncodingSettingsBase* GetSoundfieldEncodingSettings() const;
	TArray<USoundfieldEffectBase *> GetSoundfieldProcessors() const;

public:
	/** Currently used format. */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = Soundfield)
	FName SoundfieldEncodingFormat;

	//TODO: Make this editable only if SoundfieldEncodingFormat is non-default,
	// and filter classes based on ISoundfieldFactory::GetCustomSettingsClass().
	UPROPERTY(EditAnywhere, Category = Soundfield)
	TObjectPtr<USoundfieldEncodingSettingsBase> EncodingSettings;

	// TODO: make this editable only if SoundfieldEncodingFormat is non-default
	// and filter classes based on USoundfieldProcessorBase::SupportsFormat.
	UPROPERTY(EditAnywhere, Category = Soundfield)
	TArray<TObjectPtr<USoundfieldEffectBase>> SoundfieldEffectChain;

	// Traverses parent submixes until we find a submix that doesn't inherit it's soundfield format.
	FName GetSubmixFormat() const;

	UPROPERTY()
	TSubclassOf<USoundfieldEncodingSettingsBase> EncodingSettingsClass;

	// Traverses parent submixes until we find a submix that specifies encoding settings.
	const USoundfieldEncodingSettingsBase* GetEncodingSettings() const;

	// This function goes through every child submix and the parent submix to ensure that they have a compatible format with this  submix's format.
	void SanitizeLinks();

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

};

/**
 * Sound Submix class meant for sending audio to an external endpoint, such as controller haptics or an additional audio device.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta = (DisplayName = "Sound Submix Endpoint"))
class ENGINE_API UEndpointSubmix : public USoundSubmixBase
{
	GENERATED_UCLASS_BODY()

public:
	IAudioEndpointFactory* GetAudioEndpointForSubmix() const;
	const UAudioEndpointSettingsBase* GetEndpointSettings() const;

public:
	/** Currently used format. */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = Endpoint)
	FName EndpointType;

	UPROPERTY()
	TSubclassOf<UAudioEndpointSettingsBase> EndpointSettingsClass;

	//TODO: Make this editable only if EndpointType is non-default,
	// and filter classes based on ISoundfieldFactory::GetCustomSettingsClass().
	UPROPERTY(EditAnywhere, Category = Endpoint)
	TObjectPtr<UAudioEndpointSettingsBase> EndpointSettings;

};

/**
 * Sound Submix class meant for sending soundfield-encoded audio to an external endpoint, such as a hardware binaural renderer that supports ambisonics.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta = (DisplayName = "Sound Submix Soundfield Endpoint"))
class ENGINE_API USoundfieldEndpointSubmix : public USoundSubmixBase
{
	GENERATED_UCLASS_BODY()

public:
	ISoundfieldEndpointFactory* GetSoundfieldEndpointForSubmix() const;
	const USoundfieldEndpointSettingsBase* GetEndpointSettings() const;
	const USoundfieldEncodingSettingsBase* GetEncodingSettings() const;
	TArray<USoundfieldEffectBase*> GetSoundfieldProcessors() const;
public:
	/** Currently used format. */
	UPROPERTY(EditAnywhere, Category = Endpoint, AssetRegistrySearchable)
	FName SoundfieldEndpointType;

	UPROPERTY()
	TSubclassOf<UAudioEndpointSettingsBase> EndpointSettingsClass;

	/**
	* @return true if the child sound class exists in the tree
	*/
	bool RecurseCheckChild(const USoundSubmix* ChildSoundSubmix) const;

	// This function goes through every child submix and the parent submix to ensure that they have a compatible format.
	void SanitizeLinks();

	//TODO: Make this editable only if EndpointType is non-default,
	// and filter classes based on ISoundfieldFactory::GetCustomSettingsClass().
	UPROPERTY(EditAnywhere, Category = Endpoint)
	TObjectPtr<USoundfieldEndpointSettingsBase> EndpointSettings;

	UPROPERTY()
	TSubclassOf<USoundfieldEncodingSettingsBase> EncodingSettingsClass;

	UPROPERTY(EditAnywhere, Category = Soundfield)
	TObjectPtr<USoundfieldEncodingSettingsBase> EncodingSettings;

	UPROPERTY(EditAnywhere, Category = Soundfield)
	TArray<TObjectPtr<USoundfieldEffectBase>> SoundfieldEffectChain;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

namespace SubmixUtils
{
	ENGINE_API bool AreSubmixFormatsCompatible(const USoundSubmixBase* ChildSubmix, const USoundSubmixBase* ParentSubmix);

#if WITH_EDITOR
	ENGINE_API void RefreshEditorForSubmix(const USoundSubmixBase* InSubmix);
#endif
}
