// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "IAudioExtensionPlugin.h"

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
	using FControlModulatorValueMap = TMap<FModulatorId, float>;

	struct AUDIOEXTENSIONS_API FModulatorHandle
	{
		FModulatorHandle();
		FModulatorHandle(IAudioModulation& InModulation, uint32 InParentId, const USoundModulatorBase& InModulatorBase);
		FModulatorHandle(const FModulatorHandle& InOther);
		FModulatorHandle(FModulatorHandle&& InOther);

		~FModulatorHandle();

		FModulatorHandle& operator=(const FModulatorHandle& InOther);
		FModulatorHandle& operator=(FModulatorHandle&& InOther);

		FModulatorId GetId() const;
		uint32 GetParentId() const;
		float GetValue(const float InDefaultValue = 1.0f) const;
		bool IsValid() const;

	private:
		uint32 ParentId;
		FModulatorId ModulatorId;
		IAudioModulation* Modulation;
	};
} // namespace Audio


UENUM()
enum class ESoundModulatorOperator : uint8
{
	/** Modulation is disabled */
	None = 0 UMETA(DisplayName = "None"),

	/** Modulator result is multiplier of input value */
	Multiply,

	/** Modulator result is divisor, input value is dividend */
	Divide,

	/** Take the minimum of the modulator result and input value */
	Min,

	/** Take the maximum of the modulator result and input value */
	Max,

	/** Add modulator result and input value */
	Add,

	/** Subtract modulator result from input value */
	Subtract,

	Count UMETA(Hidden),
};


namespace SoundModulatorOperator
{
	FORCEINLINE float GetDefaultValue(ESoundModulatorOperator InOperator, float InMin, float InMax)
	{
		switch (InOperator)
		{
			case ESoundModulatorOperator::Max:
			{
				return InMin;
			}
			break;

			case ESoundModulatorOperator::Min:
			{
				return InMax;
			}
			break;

			case ESoundModulatorOperator::Multiply:
			case ESoundModulatorOperator::Divide:
			{
				return 1.0f;
			}
			break;

			case ESoundModulatorOperator::Add:
			case ESoundModulatorOperator::Subtract:
			case ESoundModulatorOperator::None:
			{
				return 0.0f;
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 7, "Possible missing operator switch case coverage");
				return NAN;
			}
			break;
		}
	}

	FORCEINLINE float Apply(ESoundModulatorOperator InOperator, float InValueA, float InValueB)
	{
		switch (InOperator)
		{
			case ESoundModulatorOperator::Max:
			{
				return FMath::Max(InValueA, InValueB);
			}
			break;

			case ESoundModulatorOperator::Min:
			{
				return FMath::Min(InValueA, InValueB);
			}
			break;

			case ESoundModulatorOperator::Multiply:
			{
				return InValueA * InValueB;
			}
			break;

			case ESoundModulatorOperator::Divide:
			{
				return InValueA / InValueB;
			}
			break;

			case ESoundModulatorOperator::Add:
			{
				return InValueA + InValueB;
			}
			break;

			case ESoundModulatorOperator::Subtract:
			{
				return InValueA - InValueB;
			}
			break;

			case ESoundModulatorOperator::None:
			{
				return InValueA;
			}
			break;

			default:
			{
				checkf(false, TEXT("Cannot apply 'None' as operator to modulator"));
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 7, "Possible missing operator switch case coverage");
				return NAN;
			}
			break;
		}
	}
} // namespace SoundModulatorOperator

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

	/** Calculates initial volume to determine if sound is audible using base settings data */
	virtual float CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& InSettingsBase) { return 1.0f; }

	/** Initialize the modulation plugin with the same rate and number of sources */
	virtual void Initialize(const FAudioPluginInitializationParams& InitializationParams) { }

	/** Called when a USoundBase type begins playing a sound */
	virtual void OnInitSound(ISoundModulatable& Sound, const USoundModulationPluginSourceSettingsBase& Settings) { }

	/** Called when a source is assigned to a voice */
	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings) { }

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

	/** Run on the audio render thread prior to processing audio */
	virtual void OnBeginAudioRenderThreadUpdate() { }

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) { }

	/** Processes modulated sound controls, returning whether or not controls were modified and an update is pending. */
	virtual bool ProcessControls(const uint32 SourceId, FSoundModulationControls& Controls) { return false; }

	/** Processes all modulators */
	virtual void ProcessModulators(const float Elapsed) { }

	virtual void UpdateModulator(const USoundModulatorBase& InModulator) { }

protected:
	virtual bool RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase) { return false; }
	virtual bool RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId) { return false; }
	virtual bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) { return false; }
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
