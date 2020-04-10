// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundModulationParameter.h"

#include "Async/Async.h"
#include "AudioDevice.h"
#include "Math/TransformCalculus.h"
#include "UObject/Object.h"


FSoundModulationParameterSettings::FSoundModulationParameterSettings()
	: Value(0.0f)
	, Operator(ESoundModulatorOperator::None)
	, Modulator(nullptr)
{
}

namespace Audio
{
	void FModulationParameter::Init(FDeviceId InDeviceId, uint32 InParentId, bool bInIsBuffered, float InValueMin, float InValueMax)
	{
		DeviceId = InDeviceId;
		ParentId = InParentId;
		bIsBuffered = bInIsBuffered;
		ValueMin = InValueMin;
		ValueMax = InValueMax;
	}

	bool FModulationParameter::ProcessControl(float* RESTRICT InBuffer, int32 InNumSamples)
	{
		check(bIsBuffered);

		float LastTarget = ValueTarget;
		ESoundModulatorOperator CurrentOperator = ESoundModulatorOperator::None;
		bool bIsActive = false;

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			ValueTarget = Handle.GetValue(DefaultValue);
			CurrentOperator = Operator;
		}

		if (Buffer.Num() != InNumSamples)
		{
			Buffer.Reset();
			Buffer.AddUninitialized(InNumSamples);
		}
		FMemory::Memcpy(Buffer.GetData(), InBuffer, sizeof(float) * InNumSamples);

		const bool bTargetChanged = !FMath::IsNearlyEqual(LastTarget, ValueTarget);
		if (!bIsActive || CurrentOperator == ESoundModulatorOperator::None)
		{
			return bTargetChanged;
		}

		switch (CurrentOperator)
		{
			case ESoundModulatorOperator::Max:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] = FMath::Max(Buffer[Sample], LastTarget + (Sample * ValueDelta));
				}
			}
			break;

			case ESoundModulatorOperator::Min:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] = FMath::Min(Buffer[Sample], LastTarget + (Sample * ValueDelta));
				}
			}
			break;

			case ESoundModulatorOperator::Multiply:
			{
				FadeBufferFast(Buffer, LastTarget, ValueTarget);
			}
			break;

			case ESoundModulatorOperator::Divide:
			{
				// Modulators could hypothetically be negative, but should avoid divide-by-zero
				// Buffer clamp later constricts appropriately for use case
				if (LastTarget == 0.0f)
				{
					LastTarget = SMALL_NUMBER;
				}

				if (ValueTarget == 0.0f)
				{
					ValueTarget = SMALL_NUMBER;
				}

				FadeBufferFast(Buffer, Inverse(LastTarget), Inverse(ValueTarget));
			}
			break;

			case ESoundModulatorOperator::Add:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				AddConstantToBufferInplace(Buffer, LastTarget);
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] += Sample * ValueDelta;
				}
			}
			break;

			case ESoundModulatorOperator::Subtract:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				AddConstantToBufferInplace(Buffer, -LastTarget);
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] -= Sample * ValueDelta;
				}
			}
			break;

			case ESoundModulatorOperator::None:
			default:
			{
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 7, "Possible missing operator switch case coverage");
			}
			break;
		}

		BufferRangeClampFast(Buffer, ValueMin, ValueMax);
		return bTargetChanged;
	}

	bool FModulationParameter::ProcessControl(float InValueBase, int32 InNumSamples)
	{
		float LastTarget = ValueTarget;

		ESoundModulatorOperator CurrentOperator = ESoundModulatorOperator::None;
		bool bIsActive = false;

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			ValueTarget = Handle.GetValue(DefaultValue);
			CurrentOperator = Operator;
		}

		if (!bIsActive || Operator == ESoundModulatorOperator::None)
		{
			Buffer.Reset();
			ValueTarget = InValueBase;
			return !FMath::IsNearlyEqual(LastTarget, ValueTarget);
		}

		if (!bIsBuffered)
		{
			ValueTarget = SoundModulatorOperator::Apply(CurrentOperator, InValueBase, ValueTarget);
			ValueTarget = FMath::Clamp(ValueTarget, ValueMin, ValueMax);
			return !FMath::IsNearlyEqual(LastTarget, ValueTarget);
		}

		if (Buffer.Num() != InNumSamples)
		{
			Buffer.Reset();
			Buffer.AddUninitialized(InNumSamples);
		}

		const bool bTargetChanged = !FMath::IsNearlyEqual(LastTarget, ValueTarget);
		if (!bTargetChanged)
		{
			const float ModValue = SoundModulatorOperator::Apply(CurrentOperator, InValueBase, ValueTarget);
			ValueTarget = FMath::Clamp(ModValue, ValueMin, ValueMax);

			BufferSetToConstantInplace(Buffer, ValueTarget);
			return false;
		}

		BufferSetToConstantInplace(Buffer, InValueBase);

		switch (CurrentOperator)
		{
			case ESoundModulatorOperator::Max:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] = FMath::Max(Buffer[Sample], LastTarget + (Sample * ValueDelta));
				}
			}
			break;

			case ESoundModulatorOperator::Min:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] = FMath::Min(Buffer[Sample], LastTarget + (Sample * ValueDelta));
				}
			}
			break;

			case ESoundModulatorOperator::Multiply:
			{
				FadeBufferFast(Buffer, LastTarget, ValueTarget);
			}
			break;

			case ESoundModulatorOperator::Divide:
			{
				// Modulators could hypothetically be negative, but should avoid divide-by-zero
				// Buffer clamp later constricts appropriately for use case
				if (LastTarget == 0.0f)
				{
					LastTarget = SMALL_NUMBER;
				}

				if (ValueTarget == 0.0f)
				{
					ValueTarget = SMALL_NUMBER;
				}

				FadeBufferFast(Buffer, Inverse(LastTarget), Inverse(ValueTarget));
			}
			break;

			case ESoundModulatorOperator::Add:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				AddConstantToBufferInplace(Buffer, LastTarget);
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] += Sample * ValueDelta;
				}
			}
			break;

			case ESoundModulatorOperator::Subtract:
			{
				const float ValueDelta = (ValueTarget - LastTarget) / InNumSamples;
				AddConstantToBufferInplace(Buffer, -LastTarget);
				for (int32 Sample = 0; Sample < InNumSamples; ++Sample)
				{
					Buffer[Sample] -= Sample * ValueDelta;
				}
			}
			break;

			case ESoundModulatorOperator::None:
			default:
			{
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 7, "Possible missing operator switch case coverage");
			}
			break;
		}

		BufferRangeClampFast(Buffer, ValueMin, ValueMax);
		return bTargetChanged;
	}

	void FModulationParameter::SetMax(float InMax)
	{
		ValueMax = InMax;
	}

	void FModulationParameter::SetMin(float InMin)
	{
		ValueMin = InMin;
	}

	void FModulationParameter::UpdateSettings(const FSoundModulationParameterSettings& InSettings)
	{
		const ESoundModulatorOperator NewOperator = InSettings.Operator;
		const TWeakObjectPtr<const USoundModulatorBase> ModPtr(InSettings.Modulator);
		auto UpdateHandleLambda = [this, ModPtr, NewOperator]()
		{
			const float NewDefaultValue = SoundModulatorOperator::GetDefaultValue(NewOperator, ValueMin, ValueMax);

			if (NewOperator != ESoundModulatorOperator::None && ModPtr.IsValid())
			{
				if (FAudioDevice* AudioDevice = FAudioDeviceManager::Get()->GetAudioDeviceRaw(DeviceId))
				{
					if (AudioDevice->IsModulationPluginEnabled() && AudioDevice->ModulationInterface.IsValid())
					{
						IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get();

						FScopeLock Lock(&SettingsCritSection);
						{
							DefaultValue = NewDefaultValue;
							Operator = NewOperator;
							Handle = FModulatorHandle(*Modulation, ParentId, *ModPtr.Get());
						}
						return;
					}
				}
			}

			FScopeLock Lock(&SettingsCritSection);
			{
				DefaultValue = NewDefaultValue;
				Operator = NewOperator;
				Handle = FModulatorHandle();
			}
		};

		IsInAudioThread()
			? UpdateHandleLambda()
			: AsyncTask(ENamedThreads::AudioThread, MoveTemp(UpdateHandleLambda));
	}
} // namespace Audio
