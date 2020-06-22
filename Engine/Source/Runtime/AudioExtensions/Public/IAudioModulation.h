// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "IAudioExtensionPlugin.h"
#include "Internationalization/Text.h"
#include "UObject/NameTypes.h"

#include "IAudioModulation.generated.h"


// Forward Declarations
class IAudioModulation;
class ISoundModulatable;
class USoundModulatorBase;
class UObject;

#if !UE_BUILD_SHIPPING
class FCanvas;
class FCommonViewportClient;
class FViewport;
class UFont;
#endif // !UE_BUILD_SHIPPING

struct FSoundModulationControls;

namespace Audio
{
	using FModulatorId = uint32;
	using FModulatorTypeId = uint8;

	using FModulationUnitConvertFunction		= TFunction<void(float* RESTRICT /* OutValueLinearToUnitBuffer */, int32 /* InNumSamples */)>;
	using FModulationLinearConversionFunction	= TFunction<void(float* RESTRICT /* OutValueUnitToLinearBuffer */, int32 /* InNumSamples */)>;
	using FModulationMixFunction				= TFunction<void(float* RESTRICT /* OutBufferLinearA */, const float* RESTRICT /* InBufferLinearB */, int32 /* InNumSamples */)>;

	struct AUDIOEXTENSIONS_API FModulationParameter
	{
		FModulationParameter();

		FName ParameterName;

		// Default value of parameter in unit space
		float DefaultValue = 1.0f;

		// Default minimum value of parameter in unit space
		float MinValue = 0.0f;

		// Default maximum value of parameter in unit space
		float MaxValue = 1.0f;

		// Whether or not the parameter requires conversion to/from unit space (optimization to avoid to/from unit function conversion processing)
		bool bRequiresConversion = false;

		// Function used to convert value buffer from linear space [0.0f, 1.0f] to unit space.
		FModulationUnitConvertFunction UnitFunction;

		// Function used to convert value buffer from unit space to linear [0.0f, 1.0f] space.
		FModulationLinearConversionFunction LinearFunction;

		static const FModulationMixFunction& GetDefaultMixFunction();

		// Function used to mix linear values together.
		FModulationMixFunction MixFunction;
	};

	struct AUDIOEXTENSIONS_API FModulatorHandle
	{
		FModulatorHandle() = default;
		FModulatorHandle(IAudioModulation& InModulation, uint32 InParentId, const USoundModulatorBase& InModulatorBase, FName InParameterName);
		FModulatorHandle(const FModulatorHandle& InOther);
		FModulatorHandle(FModulatorHandle&& InOther);

		~FModulatorHandle();

		FModulatorHandle& operator=(const FModulatorHandle& InOther);
		FModulatorHandle& operator=(FModulatorHandle&& InOther);

		FModulatorId GetId() const;
		const FModulationParameter& GetParameter() const;
		FModulatorTypeId GetTypeId() const;
		uint32 GetParentId() const;
		bool GetValue(float& OutValue) const;
		bool IsValid() const;

	private:
		FModulationParameter Parameter;
		uint32 ParentId = INDEX_NONE;
		FModulatorTypeId ModulatorTypeId = INDEX_NONE;
		FModulatorId ModulatorId = INDEX_NONE;
		IAudioModulation* Modulation = nullptr;
	};
} // namespace Audio

/*
 * Modulateable controls found on each sound instance
 * processed by the enabled modulation plugin.
 */
struct AUDIOEXTENSIONS_API FSoundModulationControls
{
	float Volume;
	float Pitch;
	float Lowpass;
	float Highpass;

	FSoundModulationControls()
		: Volume(1.0f)
		, Pitch(1.0f)
		, Lowpass(MAX_FILTER_FREQUENCY)
		, Highpass(MIN_FILTER_FREQUENCY)
	{
	}
};

class AUDIOEXTENSIONS_API IAudioModulation
{
public:
	/** Virtual destructor */
	virtual ~IAudioModulation() { }

	/** Returns parameter info for the given parameter name */
	virtual Audio::FModulationParameter GetParameter(FName InParamName) { return Audio::FModulationParameter(); }

	/** Initialize the modulation plugin with the same rate and number of sources */
	virtual void Initialize(const FAudioPluginInitializationParams& InitializationParams) { }

	/** Called when a USoundBase type begins playing a sound */
	virtual void OnInitSound(ISoundModulatable& Sound, const USoundModulationPluginSourceSettingsBase& Settings) { }

	/** Called when a source is assigned to a voice */
	virtual void OnInitSource(const uint32 SourceId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings) { }

	/** Called when a source is done playing and is released */
	virtual void OnReleaseSource(const uint32 SourceId) { }

	/** Called when a USoundBase type stops playing any sounds */
	virtual void OnReleaseSound(ISoundModulatable& Sound) { }

#if !UE_BUILD_SHIPPING
	/** Request to post help from active plugin (non-shipping builds only) */
	virtual bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; };

	/** Render stats pertaining to modulation (non-shipping builds only) */
	virtual int32 OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }

	/** Toggle showing render stats pertaining to modulation (non-shipping builds only) */
	virtual bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif //!UE_BUILD_SHIPPING

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) { }

	/** Processes modulated sound controls, returning whether or not controls were modified and an update is pending. */
	virtual bool ProcessControls(const uint32 SourceId, FSoundModulationControls& Controls) { return false; }

	/** Processes all modulators Run on the audio render thread prior to processing audio */
	virtual void ProcessModulators(const double InElapsed) { }

	virtual void UpdateModulator(const USoundModulatorBase& InModulator) { }

protected:
	virtual Audio::FModulatorTypeId RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase, Audio::FModulationParameter& OutParameter) { return INDEX_NONE; }
	virtual void RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId) { }
	virtual bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const { return false; }
	virtual void UnregisterModulator(const Audio::FModulatorHandle& InHandle) { }

	friend Audio::FModulatorHandle;
};

/**
 * Base class for all modulators
 */
UCLASS(config = Engine, abstract, editinlinenew, BlueprintType)
class AUDIOEXTENSIONS_API USoundModulatorBase : public UObject
{
	GENERATED_BODY()

	/** Returns the parameter referenced by the modulator.  The default implementation
	  * assumes value is always [0.0f, 1.0f], mixes multiplicatively, and requires no
	  * unit conversion.
	  */
	virtual void GetParameter(Audio::FModulationParameter& OutParameter) const
	{
		OutParameter = Audio::FModulationParameter();
	}
};

/** Override to provide users with modulation settings custom to individual sounds */
UCLASS(config = Engine, abstract, editinlinenew, BlueprintType)
class AUDIOEXTENSIONS_API USoundModulationPluginSourceSettingsBase : public UObject
{
	GENERATED_BODY()
};

/** Collection of settings available on sound objects */
USTRUCT(BlueprintType)
struct AUDIOEXTENSIONS_API FSoundModulation
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
		TArray<USoundModulationPluginSourceSettingsBase*> Settings;
};

/** Interface to sound that is modulateable, allowing for certain specific
  * behaviors to be controlled on the sound level by the modulation system.
  */
class AUDIOEXTENSIONS_API ISoundModulatable
{
public:
	virtual ~ISoundModulatable() = default;

	/**
	 * Returns the modulation settings of the sound
	 */
	virtual USoundModulationPluginSourceSettingsBase* FindModulationSettings() const = 0;

	/**
	 * Gets the object definition id of the given playing sound's instance
	 */
	virtual uint32 GetObjectId() const = 0;

	/**
	 * Returns number of actively instances of sound playing (including virtualized instances)
	 */
	virtual int32 GetPlayCount() const = 0;

	/**
	 * Returns whether or not sound is an editor preview sound
	 */
	virtual bool IsPreviewSound() const = 0;

	/**
	 * Stops sound.
	 */
	virtual void Stop() = 0;
};
