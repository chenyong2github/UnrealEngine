// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundModulationDestination.h"

#include "Async/Async.h"
#include "AudioDevice.h"
#include "Math/TransformCalculus.h"
#include "UObject/Object.h"


FSoundModulationDefaultSettings::FSoundModulationDefaultSettings()
{
	VolumeModulationDestination.Value = 0.0f;
	PitchModulationDestination.Value = 0.0f;
	HighpassModulationDestination.Value = MIN_FILTER_FREQUENCY;
	LowpassModulationDestination.Value = MAX_FILTER_FREQUENCY;
}

FSoundModulationDefaultRoutingSettings::FSoundModulationDefaultRoutingSettings()
	: FSoundModulationDefaultSettings()
{
}

namespace Audio
{
	FModulationDestination::FModulationDestination(const FModulationDestination& InModulationDestination)
		: DeviceId(InModulationDestination.DeviceId)
		, ValueTarget(InModulationDestination.ValueTarget)
		, bIsBuffered(InModulationDestination.bIsBuffered)
		, bValueNormalized(InModulationDestination.bValueNormalized)
		, OutputBuffer(InModulationDestination.OutputBuffer)
		, ParameterName(InModulationDestination.ParameterName)
	{
		FScopeLock OtherLock(&InModulationDestination.HandleCritSection);
		Handle = InModulationDestination.Handle;
	}

	FModulationDestination::FModulationDestination(FModulationDestination&& InModulationDestination)
		: DeviceId(MoveTemp(InModulationDestination.DeviceId))
		, ValueTarget(MoveTemp(InModulationDestination.ValueTarget))
		, bIsBuffered(MoveTemp(InModulationDestination.bIsBuffered))
		, bValueNormalized(MoveTemp(InModulationDestination.bValueNormalized))
		, OutputBuffer(MoveTemp(InModulationDestination.OutputBuffer))
		, ParameterName(MoveTemp(InModulationDestination.ParameterName))
	{
		FScopeLock OtherLock(&InModulationDestination.HandleCritSection);
		Handle = MoveTemp(InModulationDestination.Handle);
	}

	FModulationDestination& FModulationDestination::operator=(const FModulationDestination& InModulationDestination)
	{
		DeviceId = InModulationDestination.DeviceId;
		ValueTarget = InModulationDestination.ValueTarget;
		bIsBuffered = InModulationDestination.bIsBuffered;
		bValueNormalized = InModulationDestination.bValueNormalized;
		OutputBuffer = InModulationDestination.OutputBuffer;

		{
			FScopeLock OtherLock(&InModulationDestination.HandleCritSection);
			SetHandle(InModulationDestination.Handle);
		}
			
		ParameterName = InModulationDestination.ParameterName;


		return *this;
	}

	FModulationDestination& FModulationDestination::operator=(FModulationDestination&& InModulationDestination)
	{
		DeviceId = MoveTemp(InModulationDestination.DeviceId);
		ValueTarget = MoveTemp(InModulationDestination.ValueTarget);
		bIsBuffered = MoveTemp(InModulationDestination.bIsBuffered);
		bValueNormalized = MoveTemp(InModulationDestination.bValueNormalized);
		bHasProcessed = MoveTemp(InModulationDestination.bHasProcessed);
		OutputBuffer = MoveTemp(InModulationDestination.OutputBuffer);
		{
			FScopeLock OtherLock(&InModulationDestination.HandleCritSection);
			FScopeLock Lock(&HandleCritSection);
			Handle = MoveTemp(InModulationDestination.Handle);
		}


		ParameterName = MoveTemp(InModulationDestination.ParameterName);

		return *this;
	}

	void FModulationDestination::ResetHandle()
	{
		Audio::FModulationParameter Parameter = Audio::GetModulationParameter(ParameterName);

		FScopeLock Lock(&HandleCritSection);
		Handle = FModulatorHandle(MoveTemp(Parameter));
	}

	void FModulationDestination::Init(FDeviceId InDeviceId, bool bInIsBuffered, bool bInValueNormalized)
	{
		Init(InDeviceId, FName(), bInIsBuffered, bInValueNormalized);
	}

	void FModulationDestination::Init(FDeviceId InDeviceId, FName InParameterName, bool bInIsBuffered, bool bInValueNormalized)
	{
		DeviceId = InDeviceId;
		bIsBuffered = bInIsBuffered;
		bValueNormalized = bInValueNormalized;

		OutputBuffer.Reset();
		ParameterName = InParameterName;

		ResetHandle();
	}

	bool FModulationDestination::IsActive()
	{
		FScopeLock Lock(&HandleCritSection);
		return Handle.IsValid();
	}

	bool FModulationDestination::ProcessControl(float InValueUnitBase, int32 InNumSamples)
	{
		bHasProcessed = true;
		float LastTarget = ValueTarget;

		FScopeLock Lock(&HandleCritSection);
		{
			const FModulationParameter& Parameter = Handle.GetParameter();
			float NewTargetNormalized = Parameter.DefaultValue;
			if (Parameter.bRequiresConversion)
			{
				Parameter.NormalizedFunction(NewTargetNormalized);
			}

			if (Handle.IsValid())
			{
				Handle.GetValue(NewTargetNormalized);
			}

			// Convert base to linear space
			float InValueBaseNormalized = InValueUnitBase;
			if (Parameter.bRequiresConversion)
			{
				Parameter.NormalizedFunction(InValueBaseNormalized);
			}

			// Mix in base value
			Parameter.MixFunction(NewTargetNormalized, InValueBaseNormalized);
			ValueTarget = NewTargetNormalized;

			// Convert target to unit space if required
			if (Parameter.bRequiresConversion && !bValueNormalized)
			{
				Parameter.UnitFunction(ValueTarget);
			}
		}

		if (bIsBuffered)
		{
			if (OutputBuffer.Num() != InNumSamples)
			{
				OutputBuffer.Reset();
				OutputBuffer.AddZeroed(InNumSamples);
			}
		}

		// Fade from last target to new if output buffer is active
		if (!OutputBuffer.IsEmpty())
		{
			if (OutputBuffer.Num() % AUDIO_NUM_FLOATS_PER_VECTOR_REGISTER == 0)
			{
				if (FMath::IsNearlyEqual(LastTarget, ValueTarget))
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
				if (FMath::IsNearlyEqual(LastTarget, ValueTarget))
				{
					OutputBuffer.Init(ValueTarget, InNumSamples);
				}
				else
				{
					float SampleValue = LastTarget;
					const float DeltaValue = (ValueTarget - LastTarget) / OutputBuffer.Num();
					for (int32 i = 0; i < OutputBuffer.Num(); ++i)
					{
						OutputBuffer[i] = SampleValue;
						SampleValue += DeltaValue;
					}
				}
			}
		}

		return !FMath::IsNearlyEqual(LastTarget, ValueTarget);
	}

	void FModulationDestination::SetHandle(const FModulatorHandle& InHandle)
	{
		FScopeLock Lock(&HandleCritSection);
		Handle = InHandle;
	}

	void FModulationDestination::SetHandle(FModulatorHandle&& InHandle)
	{
		FScopeLock Lock(&HandleCritSection);
		Handle = MoveTemp(InHandle);
	}

	void FModulationDestination::UpdateModulator(const USoundModulatorBase* InModulator)
	{
		if (!InModulator)
		{
			return;
		}

		Audio::FModulationParameter OutputParam = Audio::GetModulationParameter(ParameterName);
		auto UpdateHandleLambda = [this, ModSettings = InModulator->CreateProxySettings(), Parameter = MoveTemp(OutputParam)]() mutable
		{
			if (FAudioDevice* AudioDevice = FAudioDeviceManager::Get()->GetAudioDeviceRaw(DeviceId))
			{
				if (AudioDevice->IsModulationPluginEnabled() && AudioDevice->ModulationInterface.IsValid())
				{
					if (IAudioModulationManager* Modulation = AudioDevice->ModulationInterface.Get())
					{
						check(ModSettings.IsValid());
						SetHandle(FModulatorHandle(*Modulation, *ModSettings.Get(), MoveTemp(Parameter)));
					}
					return;
				}
			}

			ResetHandle();
		};

		FAudioThread::RunCommandOnAudioThread(MoveTemp(UpdateHandleLambda));
	}
} // namespace Audio
