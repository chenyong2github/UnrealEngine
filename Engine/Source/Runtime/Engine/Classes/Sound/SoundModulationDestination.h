// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Audio.h"
#include "Containers/ArrayView.h"
#include "DSP/BufferVectorOperations.h"
#include "HAL/CriticalSection.h"
#include "IAudioModulation.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "SoundModulationDestination.generated.h"


// Forward Declarations
class USoundModulatorBase;


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
	TObjectPtr<USoundModulatorBase> Modulator = nullptr;
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
			FModulationDestination(FModulationDestination&& InModulationDestination);

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

			/* Updates internal value (or buffer if set to bIsBuffered) to current modulated result using the provided value as the base carrier value to modulate.
			 * Returns true if value was updated.
			 */
			bool ProcessControl(float InValueUnitBase, int32 InNumSamples = 0);

			void UpdateModulator(const USoundModulatorBase* InModulator);

		private:
			void ResetHandle();
			void SetHandle(const FModulatorHandle& InHandle);
			void SetHandle(FModulatorHandle&& InHandle);

			FDeviceId DeviceId = INDEX_NONE;

			float ValueTarget = 1.0f;

			bool bIsBuffered = false;
			bool bValueNormalized = false;
			bool bHasProcessed = false;

			FAlignedFloatBuffer OutputBuffer;
			FModulatorHandle Handle;

			FName ParameterName;

			mutable FCriticalSection HandleCritSection;

		public:
			/** Returns buffer of interpolated modulation values. If not set to "IsBuffered" when initialized, returns an empty array. */
			FORCEINLINE const FAlignedFloatBuffer& GetBuffer() const
			{
				return OutputBuffer;
			}

			/** Returns whether or not the destination has requested to 
			  * process the control or not. */
			FORCEINLINE bool GetHasProcessed() const
			{
				return bHasProcessed;
			}

			/** Returns sample value last reported by modulator. Returns value in unit space, unless
			 * 'ValueNormalized' option is set on initialization.
			 */
			FORCEINLINE float GetValue() const
			{
				return ValueTarget;
			}
	};
} // namespace Audio