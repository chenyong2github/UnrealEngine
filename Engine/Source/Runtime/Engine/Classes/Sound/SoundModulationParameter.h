// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Audio.h"
#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "IAudioModulation.h"

#include "SoundModulationParameter.generated.h"


// Forward Declarations
class USoundModulatorBase;
class UObject;


/** Parameter settings allowing modulation control override for systems opting in to the Modulation System. */
USTRUCT(BlueprintType)
struct ENGINE_API FSoundModulationParameterSettings
{
	GENERATED_USTRUCT_BODY()

	FSoundModulationParameterSettings();

	/** Base value of parameter */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
	float Value;

	/** Operator to apply when modulating the default value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
	ESoundModulatorOperator Operator;

	/** Subscribed modulator to listen to apply result to base value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation, meta = (EditCondition = "Operator != ESoundModulatorOperator::None", EditConditionHides))
	USoundModulatorBase* Modulator;
};

namespace Audio
{
	struct ENGINE_API FModulationParameter
	{
		public:
			FModulationParameter() = default;

			void Init(FDeviceId InDeviceId, uint32 InParentId, bool bInIsBuffered = false, float InValueMin = 0.0f, float InValueMax = 1.0f);

			/* Updates internal buffer to modulated result with control value as provided by modulation plugin.
			 * Returns true if control value was updated.
			 */
			bool ProcessControl(float* RESTRICT InBuffer, int32 InNumSamples);

			/* Updates internal target (or buffer if set to bIsBuffered) to new parameter control value as provided by modulation plugin.
			 * Returns true if value was updated.
			 */
			bool ProcessControl(float InValueBase, int32 InNumSamples = 0);

			void SetMax(float InMax);
			void SetMin(float InMin);
			void UpdateSettings(const FSoundModulationParameterSettings& InSettings);

		private:
			FDeviceId DeviceId = INDEX_NONE;
			uint32 ParentId = INDEX_NONE;

			float ValueMin = 0.0f;
			float ValueMax = 1.0f;
			float ValueTarget = 0.0f;
			float DefaultValue = 0.0f;

			ESoundModulatorOperator Operator = ESoundModulatorOperator::None;

			bool bIsBuffered = false;

			AlignedFloatBuffer Buffer;
			FModulatorHandle Handle;

			FCriticalSection SettingsCritSection;

		public:
			FORCEINLINE const AlignedFloatBuffer& GetBuffer() const
			{
				check(bIsBuffered);
				return Buffer;
			}

			FORCEINLINE float GetSample(int32 InSampleIndex) const
			{
				check(bIsBuffered);
				return InSampleIndex < Buffer.Num() ? Buffer[InSampleIndex] : ValueTarget;
			}

			FORCEINLINE float GetTarget() const
			{
				return ValueTarget;
			}
	};
} // namespace Audio