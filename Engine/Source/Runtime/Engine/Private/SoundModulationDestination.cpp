// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundModulationDestination.h"

#include "Async/Async.h"
#include "AudioDevice.h"
#include "Math/TransformCalculus.h"
#include "UObject/Object.h"


namespace Audio
{
	void FModulationDestination::Init(FDeviceId InDeviceId, uint32 InParentId, bool bInIsBuffered)
	{
		DeviceId = InDeviceId;
		ParentId = InParentId;
		bIsBuffered = bInIsBuffered;
	}

	void FModulationDestination::Init(FDeviceId InDeviceId, uint32 InParentId, FName InParameterName, bool bInIsBuffered)
	{
		Init(InDeviceId, InParentId, bInIsBuffered);
		ParameterName = InParameterName;
	}

	void FModulationDestination::ProcessControl(const float* RESTRICT InBuffer, int32 InNumSamples)
	{
		checkf(bIsBuffered, TEXT("Cannot call this 'ProcessControl' overload with 'bIsBuffered' set to 'false'."));

		float LastTarget = ValueTarget;
		bool bIsActive = false;

		float NewTargetLinear = Parameter.DefaultValue;
		FScopeLock Lock(&SettingsCritSection);
		{
			if (Handle.IsValid())
			{
				Handle.GetValue(NewTargetLinear);
			}
		}
		ValueTarget = NewTargetLinear;

		if (OutputBuffer.Num() != InNumSamples)
		{
			OutputBuffer.Reset();
			OutputBuffer.AddUninitialized(InNumSamples);
		}
		BufferSetToConstantInplace(OutputBuffer, 1.0f);
		FadeBufferFast(OutputBuffer, LastTarget, ValueTarget);

		if (TempBufferLinear.Num() != InNumSamples)
		{
			TempBufferLinear.Reset();
			TempBufferLinear.AddUninitialized(InNumSamples);
		}

		FMemory::Memcpy(TempBufferLinear.GetData(), InBuffer, sizeof(float) * InNumSamples);

		// Convert input buffer to linear space if necessary
		if (Parameter.bRequiresConversion)
		{
			Parameter.LinearFunction(TempBufferLinear.GetData(), TempBufferLinear.Num());
		}

		// Mix mod value and base value buffers in linear space
		Parameter.MixFunction(OutputBuffer.GetData(), TempBufferLinear.GetData(), InNumSamples);

		// Convert result to unit space if necessary
		if (Parameter.bRequiresConversion)
		{
			Parameter.UnitFunction(OutputBuffer.GetData(), OutputBuffer.Num());
		}
	}

	bool FModulationDestination::ProcessControl(float InValueBase, int32 InNumSamples)
	{
		float LastTarget = ValueTarget;
		bool bIsActive = false;

		float NewTargetLinear = Parameter.DefaultValue;
		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			if (bIsActive)
			{
				Handle.GetValue(NewTargetLinear);
			}
		}

		// Convert base to linear space
		float InValueBaseLinear = InValueBase;
		if (Parameter.bRequiresConversion)
		{
			Parameter.LinearFunction(&InValueBaseLinear, 1);
		}

		if (bIsBuffered)
		{
			if (OutputBuffer.Num() != InNumSamples)
			{
				OutputBuffer.Reset();
				OutputBuffer.AddZeroed(InNumSamples);
			}
		}

		// Mix in base value
		Parameter.MixFunction(&NewTargetLinear, &InValueBaseLinear, 1);
		ValueTarget = NewTargetLinear;

		// Convert target to unit space if required
		if (Parameter.bRequiresConversion)
		{
			Parameter.UnitFunction(&ValueTarget, 1);
		}

		// Fade from last target to new if output buffer is active
		if (OutputBuffer.Num() > 0)
		{
			if (OutputBuffer.Num() % 4 == 0)
			{
				if (LastTarget == ValueTarget)
				{
					BufferSetToConstantInplace(OutputBuffer, ValueTarget);
				}
				else
				{
					BufferSetToConstantInplace(OutputBuffer, 1.0f);
					FadeBufferFast(OutputBuffer, LastTarget, ValueTarget);
				}
			}
			else
			{
				float Gain = LastTarget;
				const float DeltaValue = (ValueTarget - LastTarget) / OutputBuffer.Num();
				for (int32 i = 0; i < OutputBuffer.Num(); ++i)
				{
					OutputBuffer[i] *= Gain;
					Gain += DeltaValue;
				}
			}
		}

		return !FMath::IsNearlyEqual(LastTarget, ValueTarget);
	}

	void FModulationDestination::UpdateSettings(const FSoundModulationDestinationSettings& InSettings)
	{
		const TWeakObjectPtr<const USoundModulatorBase> ModPtr(InSettings.Modulator);
		auto UpdateHandleLambda = [this, ModPtr]()
		{
			if (ModPtr.IsValid())
			{
				if (FAudioDevice* AudioDevice = FAudioDeviceManager::Get()->GetAudioDeviceRaw(DeviceId))
				{
					if (AudioDevice->IsModulationPluginEnabled() && AudioDevice->ModulationInterface.IsValid())
					{
						if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
						{
							FScopeLock Lock(&SettingsCritSection);
							Handle = FModulatorHandle(*Modulation, ParentId, *ModPtr.Get(), ParameterName);
							Parameter = Handle.GetParameter();
						}
						return;
					}
				}
			}

			FScopeLock Lock(&SettingsCritSection);
			{
				Handle = FModulatorHandle();
				Parameter = Handle.GetParameter();
			}
		};

		IsInAudioThread()
			? UpdateHandleLambda()
			: AsyncTask(ENamedThreads::AudioThread, MoveTemp(UpdateHandleLambda));
	}
} // namespace Audio
