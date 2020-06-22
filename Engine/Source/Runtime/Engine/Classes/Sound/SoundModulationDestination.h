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

	/** Subscribed modulator to listen to apply result to base value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
	USoundModulatorBase* Modulator = nullptr;
};


namespace Audio
{
	struct ENGINE_API FModulationDestination
	{
		public:
			FModulationDestination() = default;

			void Init(FDeviceId InDeviceId, uint32 InParentId, bool bInIsBuffered = false);
			void Init(FDeviceId InDeviceId, uint32 InParentId, FName InParameterName, bool bInIsBuffered = false);

			/* Processes output buffer by modulating the input buffer. Asserts if parameter is not set as buffered. */
			void ProcessControl(const float* RESTRICT InBuffer, int32 InNumSamples);

			/* Updates internal target (or buffer if set to bIsBuffered) to new parameter control value as provided by modulation plugin.
			 * Returns true if value was updated.
			 */
			bool ProcessControl(float InValueBase, int32 InNumSamples = 0);

			void UpdateSettings(const FSoundModulationDestinationSettings& InSettings);

		private:
			FDeviceId DeviceId = INDEX_NONE;
			uint32 ParentId = INDEX_NONE;

			float ValueTarget = 0.0f;
			bool bIsBuffered = false;

			AlignedFloatBuffer OutputBuffer;
			AlignedFloatBuffer TempBufferLinear;
			FModulatorHandle Handle;
			FName ParameterName;

			FModulationParameter Parameter;

			FCriticalSection SettingsCritSection;

		public:
			FORCEINLINE const AlignedFloatBuffer& GetBuffer() const
			{
				check(bIsBuffered);
				return OutputBuffer;
			}

			/** Returns sample value last reported by modulator (in unit space) */
			FORCEINLINE float GetValue() const
			{
				check(!bIsBuffered);
				return ValueTarget;
			}
	};
} // namespace Audio