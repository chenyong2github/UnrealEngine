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
	TArray<USoundSubmixBase*> ChildSubmixes;

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
	USoundSubmixBase* ParentSubmix;

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

/**
 * Sound Submix class meant for applying an effect to the downmixed sum of multiple audio sources.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta=(DisplayName="Effect Submix"))
class ENGINE_API USoundSubmix : public USoundSubmixWithParentBase
{
	GENERATED_UCLASS_BODY()

public:

	/** Mute this submix when the application is muted or in the background. Used to prevent submix effect tails from continuing when tabbing out of application or if application is muted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	uint8 bMuteWhenBackgrounded : 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = SoundSubmix)
	TArray<USoundEffectSubmixPreset*> SubmixEffectChain;

	/** Optional settings used by plugins which support ambisonics file playback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundSubmix)
	USoundfieldEncodingSettingsBase* AmbisonicsPluginSettings;

	/** The attack time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the envelope value of sounds played with this submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnvelopeFollower, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerAttackTime;

	/** The release time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the envelope value of sounds played with this submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnvelopeFollower, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerReleaseTime;

	/** The output volume of the submix. Applied after submix effects and analysis are performed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SoundSubmix, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float OutputVolume;

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

	UFUNCTION(BlueprintCallable, Category = "Audio|EnvelopeFollowing", meta = (WorldContext = "WorldContextObject"))
	void AddEnvelopeFollowerDelegate(const UObject* WorldContextObject, const FOnSubmixEnvelopeBP& OnSubmixEnvelopeBP);

	/** Sets the output volume of the submix. This dynamic volume scales with the OutputVolume property of this submix. */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	void SetSubmixOutputVolume(const UObject* WorldContextObject, float InOutputVolume);

protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// State handling for bouncing output.
	TUniquePtr<Audio::FAudioRecordingData> RecordingData;
};
	

/**
 * Sound Submix class meant for use with soundfield formats, such as Ambisonics.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta = (DisplayName = "Soundfield Submix"))
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
	USoundfieldEncodingSettingsBase* EncodingSettings;

	// TODO: make this editable only if SoundfieldEncodingFormat is non-default
	// and filter classes based on USoundfieldProcessorBase::SupportsFormat.
	UPROPERTY(EditAnywhere, Category = Soundfield)
	TArray<USoundfieldEffectBase*> SoundfieldEffectChain;

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
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta = (DisplayName = "Audio Endpoint Submix"))
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
	UAudioEndpointSettingsBase* EndpointSettings;

};

/**
 * Sound Submix class meant for sending soundfield-encoded audio to an external endpoint, such as a hardware binaural renderer that supports ambisonics.
 */
UCLASS(config = Engine, hidecategories = Object, editinlinenew, BlueprintType, Meta = (DisplayName = "Soundfield Endpoint Submix"))
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

	// This function goes through every child submix and the parent submix to ensure that they have a compatible format with this  submix's format.
	void SanitizeLinks();

	//TODO: Make this editable only if EndpointType is non-default,
	// and filter classes based on ISoundfieldFactory::GetCustomSettingsClass().
	UPROPERTY(EditAnywhere, Category = Endpoint)
	USoundfieldEndpointSettingsBase* EndpointSettings;

	UPROPERTY()
	TSubclassOf<USoundfieldEncodingSettingsBase> EncodingSettingsClass;

	UPROPERTY(EditAnywhere, Category = Soundfield)
	USoundfieldEncodingSettingsBase* EncodingSettings;

	UPROPERTY(EditAnywhere, Category = Soundfield)
	TArray<USoundfieldEffectBase*> SoundfieldEffectChain;

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
