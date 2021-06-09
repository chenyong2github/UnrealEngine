// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Audio.h"
#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "IAudioModulation.h"

#include "SoundModulationDestination.generated.h"


// Forward Declarations
class USoundModulatorBase;
class UObject;


UENUM(BlueprintType)
enum class EModulationRouting : uint8
{
	/* Disables modulation routing */
	Disable,

	/* Inherits modulation routing (AudioComponent inherits from Sound, Sound inherits from SoundClass) */
	Inherit,

	/* Ignores inherited settings and uses modulation settings on this object */
	Override
};

/** Parameter destination settings allowing modulation control override for parameter destinations opting in to the Modulation System. */
USTRUCT(BlueprintType)
struct ENGINE_API FSoundModulationDestinationSettings
{
	GENERATED_USTRUCT_BODY()

	/** Base value of parameter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
	float Value = 1.0f;

#if WITH_EDITORONLY_DATA
	/** Base value of parameter */
	UPROPERTY(EditAnywhere, Category = Modulation, meta = (DisplayName = "Modulate"))
	bool bEnableModulation = false;
#endif // WITH_EDITORONLY_DATA

	/** Modulation source, which provides value to mix with base value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
	USoundModulatorBase* Modulator = nullptr;
};

/** Default parameter destination settings for source audio object. */
USTRUCT(BlueprintType)
struct ENGINE_API FSoundModulationDefaultSettings
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationDefaultSettings();

	/** Volume modulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation", meta = (DisplayName = "Volume", AudioParam = "Volume"))
	FSoundModulationDestinationSettings VolumeModulationDestination;

	/** Pitch modulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation", meta = (DisplayName = "Pitch", AudioParam = "Pitch"))
	FSoundModulationDestinationSettings PitchModulationDestination;

	/** Highpass modulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation", meta = (DisplayName = "Highpass", AudioParam = "HPFCutoffFrequency"))
	FSoundModulationDestinationSettings HighpassModulationDestination;

	/** Lowpass modulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation", meta = (DisplayName = "Lowpass", AudioParam = "LPFCutoffFrequency"))
	FSoundModulationDestinationSettings LowpassModulationDestination;
};

/** Default parameter destination settings for source audio object. */
USTRUCT(BlueprintType)
struct ENGINE_API FSoundModulationDefaultRoutingSettings : public FSoundModulationDefaultSettings
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationDefaultRoutingSettings();

	/** What volume modulation settings to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation")
	EModulationRouting VolumeRouting = EModulationRouting::Inherit;

	/** What pitch modulation settings to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation")
	EModulationRouting PitchRouting = EModulationRouting::Inherit;

	/** What high-pass modulation settings to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation")
	EModulationRouting HighpassRouting = EModulationRouting::Inherit;

	/** What low-pass modulation settings to use */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modulation")
	EModulationRouting LowpassRouting = EModulationRouting::Inherit;
};

namespace Audio
{
	struct ENGINE_API FModulationDestination
	{
		public:
			FModulationDestination() = default;
			FModulationDestination(const FModulationDestination& InModulationDestination);

			FModulationDestination& operator=(const FModulationDestination& InModulationDestination);
			FModulationDestination& operator=(FModulationDestination&& InModulationDestination);

			/** Initializes the modulation destination
			 * InDeviceId - DeviceId associated with modulation plugin instance
			 * bInIsBuffered - Whether or not to run destination in "buffered mode," which manages an internal buffer to smooth modulation value between process calls
			 * bInValueNormalized - Whether or not to keep the output value in normalized, unitless [0.0f, 1.0f] space
			 */
			void Init(FDeviceId InDeviceId, bool bInIsBuffered = false, bool bInValueNormalized = false);

			/** Initializes the modulation destination
			 * InDeviceId - DeviceId associated with modulation plugin instance
			 * InParameterName - Name of parameter used to mix/convert destination value to/from unit space
			 * bInIsBuffered - Whether or not to run destination in "buffered mode," which manages an internal buffer to smooth modulation value between process calls
			 * bInValueNormalized - Whether or not to keep the output value in normalized, unitless [0.0f, 1.0f] space
			 */
			void Init(FDeviceId InDeviceId, FName InParameterName, bool bInIsBuffered = false, bool bInValueNormalized = false);

			/** returns whether or not destination references an active modulator */
			bool IsActive();

			/* Processes output buffer by modulating the input buffer of base (i.e. carrier) values (in unit space). Asserts if parameter is not set as buffered. */
			void ProcessControl(const float* RESTRICT InBufferUnitBase, int32 InNumSamples);

			/* Updates internal value (or buffer if set to bIsBuffered) to current modulated result using the provided value as the base carrier value to modulate.
			 * Returns true if value was updated.
			 */
			bool ProcessControl(float InValueUnitBase, int32 InNumSamples = 0);

			void UpdateModulator(const USoundModulatorBase* InModulator);
			void UpdateModulator_RenderThread(const USoundModulatorBase* InModulator);

		private:
			FDeviceId DeviceId = INDEX_NONE;

			float ValueTarget = 1.0f;

			uint8 bIsBuffered   = 0;
			uint8 bValueNormalized  = 0;
			uint8 bIsActive     = 0;
			uint8 bHasProcessed = 0;

			FAlignedFloatBuffer OutputBuffer;
			FAlignedFloatBuffer TempBufferNormalized;
			FModulatorHandle Handle;

			FName ParameterName;
			FModulationParameter Parameter;

			FCriticalSection SettingsCritSection;

		public:
			/** Returns buffer of interpolated modulation values */
			FORCEINLINE const FAlignedFloatBuffer& GetBuffer() const
			{
				check(bIsBuffered);
				return OutputBuffer;
			}

			/** Returns whether or not the destination has requested to 
			  * process the control or not. */
			FORCEINLINE bool GetHasProcessed() const
			{
				return bHasProcessed != 0;
			}

			/** Returns sample value last reported by modulator. Returns value in unit space, unless 
			 * 'ValueNormalized' option is set on initialization.
			 */
			FORCEINLINE float GetValue() const
			{
				check(!bIsBuffered);
				return ValueTarget;
			}
	};
} // namespace Audio