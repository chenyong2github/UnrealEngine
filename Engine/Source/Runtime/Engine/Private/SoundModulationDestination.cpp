// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundModulationDestination.h"

#include "Async/Async.h"
#include "AudioDevice.h"
#include "Math/TransformCalculus.h"
#include "UObject/Object.h"


namespace Audio
{
	FModulationDestination::FModulationDestination(const FModulationDestination& InModulationDestination)
		: DeviceId(InModulationDestination.DeviceId)
		, ValueTarget(InModulationDestination.ValueTarget)
		, bIsBuffered(InModulationDestination.bIsBuffered)
		, bValueLinear(InModulationDestination.bValueLinear)
		, OutputBuffer(InModulationDestination.OutputBuffer)
		, TempBufferLinear(InModulationDestination.TempBufferLinear)
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
		bValueLinear		= InModulationDestination.bValueLinear;
		OutputBuffer		= InModulationDestination.OutputBuffer;
		TempBufferLinear	= InModulationDestination.TempBufferLinear;
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
		bValueLinear		= MoveTemp(InModulationDestination.bValueLinear);
		OutputBuffer		= MoveTemp(InModulationDestination.OutputBuffer);
		TempBufferLinear	= MoveTemp(InModulationDestination.TempBufferLinear);
		Handle				= MoveTemp(InModulationDestination.Handle);
		ParameterName		= MoveTemp(InModulationDestination.ParameterName);
		Parameter			= MoveTemp(InModulationDestination.Parameter);

		InModulationDestination.Init(static_cast<FDeviceId>(INDEX_NONE));

		return *this;
	}

	void FModulationDestination::Init(FDeviceId InDeviceId, bool bInIsBuffered, bool bInValueLinear)
	{
		DeviceId = InDeviceId;
		bIsBuffered = bInIsBuffered;
		bValueLinear = bInValueLinear;

		OutputBuffer.Reset();
		TempBufferLinear.Reset();
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

	void FModulationDestination::Init(FDeviceId InDeviceId, FName InParameterName, bool bInIsBuffered, bool bInValueLinear)
	{
		Init(InDeviceId, bInIsBuffered, bInValueLinear);
		ParameterName = InParameterName;
	}

	void FModulationDestination::ProcessControl(const float* RESTRICT InBufferUnitBase, int32 InNumSamples)
	{
		checkf(bIsBuffered, TEXT("Cannot call this 'ProcessControl' overload with 'bIsBuffered' set to 'false'."));

		bHasProcessed = 1;
		float LastTarget = ValueTarget;
		float NewTargetLinear = Parameter.DefaultValue;

		if (Parameter.bRequiresConversion)
		{
			Parameter.LinearFunction(&NewTargetLinear, 1);
		}

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			if (bIsActive)
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

		FMemory::Memcpy(TempBufferLinear.GetData(), InBufferUnitBase, sizeof(float) * InNumSamples);

		// Convert input buffer to linear space if necessary
		if (Parameter.bRequiresConversion)
		{
			Parameter.LinearFunction(TempBufferLinear.GetData(), TempBufferLinear.Num());
		}

		// Mix mod value and base value buffers in linear space
		Parameter.MixFunction(OutputBuffer.GetData(), TempBufferLinear.GetData(), InNumSamples);

		// Convert result to unit space if necessary
		if (Parameter.bRequiresConversion && !bValueLinear)
		{
			Parameter.UnitFunction(OutputBuffer.GetData(), OutputBuffer.Num());
		}
	}

	bool FModulationDestination::ProcessControl(float InValueUnitBase, int32 InNumSamples)
	{
		bHasProcessed = 1;
		float LastTarget = ValueTarget;
		float NewTargetLinear = Parameter.DefaultValue;

		if (Parameter.bRequiresConversion)
		{
			Parameter.LinearFunction(&NewTargetLinear, 1);
		}

		FScopeLock Lock(&SettingsCritSection);
		{
			bIsActive = Handle.IsValid();
			if (bIsActive)
			{
				Handle.GetValue(NewTargetLinear);
			}
		}

		// Convert base to linear space
		float InValueBaseLinear = InValueUnitBase;
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
		if (Parameter.bRequiresConversion && !bValueLinear)
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
} // namespace Audio
