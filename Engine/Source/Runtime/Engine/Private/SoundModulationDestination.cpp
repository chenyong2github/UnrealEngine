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
		, TempBufferNormalized(InModulationDestination.TempBufferNormalized)
		, Handle(InModulationDestination.Handle)
		, ParameterName(InModulationDestination.ParameterName)
		, Parameter(InModulationDestination.Parameter)
	{
	}

	FModulationDestination& FModulationDestination::operator=(const FModulationDestination& InModulationDestination)
	{
		DeviceId			= InModulationDestination.DeviceId;
		ValueTarget			= InModulationDestination.ValueTarget;
		bIsBuffered			= InModulationDestination.bIsBuffered;
		bValueNormalized		= InModulationDestination.bValueNormalized;
		OutputBuffer		= InModulationDestination.OutputBuffer;
		TempBufferNormalized	= InModulationDestination.TempBufferNormalized;
		Handle				= InModulationDestination.Handle;
		ParameterName		= InModulationDestination.ParameterName;
		Parameter			= InModulationDestination.Parameter;

		return *this;
	}

	FModulationDestination& FModulationDestination::operator=(FModulationDestination&& InModulationDestination)
	{
		DeviceId			= MoveTemp(InModulationDestination.DeviceId);
		ValueTarget			= MoveTemp(InModulationDestination.ValueTarget);
		bIsBuffered			= MoveTemp(InModulationDestination.bIsBuffered);
		bValueNormalized		= MoveTemp(InModulationDestination.bValueNormalized);
		OutputBuffer		= MoveTemp(InModulationDestination.OutputBuffer);
		TempBufferNormalized	= MoveTemp(InModulationDestination.TempBufferNormalized);
		Handle				= MoveTemp(InModulationDestination.Handle);
		ParameterName		= MoveTemp(InModulationDestination.ParameterName);
		Parameter			= MoveTemp(InModulationDestination.Parameter);

		InModulationDestination.Init(static_cast<FDeviceId>(INDEX_NONE));

		return *this;
	}

	void FModulationDestination::Init(FDeviceId InDeviceId, bool bInIsBuffered, bool bInValueNormalized)
	{
		DeviceId = InDeviceId;
		bIsBuffered = bInIsBuffered;
		bValueNormalized = bInValueNormalized;

		OutputBuffer.Reset();
		TempBufferNormalized.Reset();
		ParameterName = FName();

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = false;
			Handle = FModulatorHandle();
			Parameter = Handle.GetParameter();
		}
	}

	bool FModulationDestination::IsActive()
	{
		FScopeLock Lock(&SettingsCritSection);
		return bIsActive > 0;
	}

	void FModulationDestination::Init(FDeviceId InDeviceId, FName InParameterName, bool bInIsBuffered, bool bInValueNormalized)
	{
		Init(InDeviceId, bInIsBuffered, bInValueNormalized);
		ParameterName = InParameterName;
	}

	void FModulationDestination::ProcessControl(const float* RESTRICT InBufferUnitBase, int32 InNumSamples)
	{
		checkf(bIsBuffered, TEXT("Cannot call this 'ProcessControl' overload with 'bIsBuffered' set to 'false'."));

		bHasProcessed = 1;
		float LastTarget = ValueTarget;
		float NewTargetNormalized = Parameter.DefaultValue;

		if (Parameter.bRequiresConversion)
		{
			Parameter.NormalizedFunction(&NewTargetNormalized, 1);
		}

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			if (bIsActive)
			{
				Handle.GetValue(NewTargetNormalized);
			}
		}
		ValueTarget = NewTargetNormalized;

		if (OutputBuffer.Num() != InNumSamples)
		{
			OutputBuffer.Reset();
			OutputBuffer.AddUninitialized(InNumSamples);
		}
		BufferSetToConstantInplace(OutputBuffer, 1.0f);
		FadeBufferFast(OutputBuffer, LastTarget, ValueTarget);

		if (TempBufferNormalized.Num() != InNumSamples)
		{
			TempBufferNormalized.Reset();
			TempBufferNormalized.AddUninitialized(InNumSamples);
		}

		FMemory::Memcpy(TempBufferNormalized.GetData(), InBufferUnitBase, sizeof(float) * InNumSamples);

		// Convert input buffer to linear space if necessary
		if (Parameter.bRequiresConversion)
		{
			Parameter.NormalizedFunction(TempBufferNormalized.GetData(), TempBufferNormalized.Num());
		}

		// Mix mod value and base value buffers in linear space
		Parameter.MixFunction(OutputBuffer.GetData(), TempBufferNormalized.GetData(), InNumSamples);

		// Convert result to unit space if necessary
		if (Parameter.bRequiresConversion && !bValueNormalized)
		{
			Parameter.UnitFunction(OutputBuffer.GetData(), OutputBuffer.Num());
		}
	}

	bool FModulationDestination::ProcessControl(float InValueUnitBase, int32 InNumSamples)
	{
		bHasProcessed = 1;
		float LastTarget = ValueTarget;
		float NewTargetNormalized = Parameter.DefaultValue;

		if (Parameter.bRequiresConversion)
		{
			Parameter.NormalizedFunction(&NewTargetNormalized, 1);
		}

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			if (bIsActive)
			{
				Handle.GetValue(NewTargetNormalized);
			}
		}

		// Convert base to linear space
		float InValueBaseNormalized = InValueUnitBase;
		if (Parameter.bRequiresConversion)
		{
			Parameter.NormalizedFunction(&InValueBaseNormalized, 1);
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
		Parameter.MixFunction(&NewTargetNormalized, &InValueBaseNormalized, 1);
		ValueTarget = NewTargetNormalized;

		// Convert target to unit space if required
		if (Parameter.bRequiresConversion && !bValueNormalized)
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

	void FModulationDestination::UpdateModulator(const USoundModulatorBase* InModulator)
	{
		const TWeakObjectPtr<const USoundModulatorBase> ModPtr(InModulator);
		auto UpdateHandleLambda = [this, ModPtr]()
		{
			if (FAudioDevice* AudioDevice = FAudioDeviceManager::Get()->GetAudioDeviceRaw(DeviceId))
			{
				if (AudioDevice->IsModulationPluginEnabled() && AudioDevice->ModulationInterface.IsValid())
				{
					if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
					{
						FScopeLock Lock(&SettingsCritSection);
						Handle = FModulatorHandle(*Modulation, ModPtr.Get(), ParameterName);

						// Cache parameter so copy isn't required to be created every process call
						Parameter = Handle.GetParameter();
						bIsActive = Handle.IsValid();
					}
					return;
				}
			}

			FScopeLock Lock(&SettingsCritSection);
			{
				bIsActive = false;
				Handle = FModulatorHandle();
				Parameter = Handle.GetParameter();
			}
		};

		IsInAudioThread()
			? UpdateHandleLambda()
			: AsyncTask(ENamedThreads::AudioThread, MoveTemp(UpdateHandleLambda));
	}

	void FModulationDestination::UpdateModulator_RenderThread(const USoundModulatorBase* InModulator)
	{
		if (IsInAudioThread())
		{
			return;
		}

		if (FAudioDevice* AudioDevice = FAudioDeviceManager::Get()->GetAudioDeviceRaw(DeviceId))
		{
			if (AudioDevice->IsModulationPluginEnabled() && AudioDevice->ModulationInterface.IsValid())
			{
				if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
				{
					FScopeLock Lock(&SettingsCritSection);
					Handle = FModulatorHandle(*Modulation, InModulator, ParameterName);

					//Cache parameter so copy isn't required to be created every process call
					Parameter = Handle.GetParameter();
					bIsActive = Handle.IsValid();
				}
			}
		}
	}
} // namespace Audio
