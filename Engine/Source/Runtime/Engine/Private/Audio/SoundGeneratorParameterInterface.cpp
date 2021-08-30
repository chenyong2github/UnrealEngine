// Copyright Epic Games, Inc. All Rights Reserved.
#include "Audio/SoundGeneratorParameterInterface.h"

#include "ActiveSound.h"
#include "Audio.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "IAudioExtensionPlugin.h"


namespace SoundGeneratorParameterInterfacePrivate
{
	static const FName ProxyFeatureName("SoundGeneratorParameterInterface");
} // namespace SoundGeneratorParameterInterfacePrivate

USoundGeneratorParameterInterface::USoundGeneratorParameterInterface(FObjectInitializer const& InObjectInitializer)
	: Super(InObjectInitializer)
{
}

void ISoundGeneratorParameterInterface::ResetParameters()
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		FAudioThread::RunCommandOnAudioThread([AudioDevice, InstanceID = GetInstanceID()]() mutable
		{
			if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(InstanceID))
			{
				if (Audio::IParameterTransmitter* Transmitter = ActiveSound->GetTransmitter())
				{
					Transmitter->Reset();
				}
			}
		});
	}
}

void ISoundGeneratorParameterInterface::SetTriggerParameter(FName InName)
{
	// Trigger is just a (true) bool param currently.
	SetParameterInternal(FAudioParameter(InName, true));
}

void ISoundGeneratorParameterInterface::SetBoolParameter(FName InName, bool InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetBoolArrayParameter(FName InName, const TArray<bool>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetIntParameter(FName InName, int32 InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetIntArrayParameter(FName InName, const TArray<int32>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetFloatParameter(FName InName, float InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetFloatArrayParameter(FName InName, const TArray<float>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetStringParameter(FName InName, const FString& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetStringArrayParameter(FName InName, const TArray<FString>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetObjectParameter(FName InName, UObject* InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetObjectArrayParameter(FName InName, const TArray<UObject*>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundGeneratorParameterInterface::SetParameter(FAudioParameter&& InValue)
{
	SetParameterInternal(MoveTemp(InValue));
}

void ISoundGeneratorParameterInterface::SetParameters(TArray<FAudioParameter>&& InValues)
{
	for (FAudioParameter& Value : InValues)
	{
		TArray<FAudioParameter>& InstanceParameters = GetInstanceParameters();
		if (FAudioParameter* CurrentParam = FAudioParameter::FindOrAddParam(InstanceParameters, Value.ParamName))
		{
			CurrentParam->Merge(Value, false /* bInTakeName */);
		}
	}

	if (IsPlaying())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			TArray<FAudioParameter> ParamsToSet;
			if (USoundBase* Sound = GetSound())
			{
				Sound->InitParameters(InValues, SoundGeneratorParameterInterfacePrivate::ProxyFeatureName);
			}

			ParamsToSet = MoveTemp(InValues);

			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SoundGenerator.SetParameters"), STAT_AudioSetParameters, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([AudioDevice, InstanceID = GetInstanceID(), Params { MoveTemp(ParamsToSet) }]() mutable
			{
				if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(InstanceID))
				{
					if (Audio::IParameterTransmitter* Transmitter = ActiveSound->GetTransmitter())
					{
						for (FAudioParameter& Param : Params)
						{
							const FName ParamName = Param.ParamName;
							if (!Transmitter->SetParameter(MoveTemp(Param)))
							{
								UE_LOG(LogAudio, Warning, TEXT("Failed to set parameter '%s'"), *ParamName.ToString());
							}
						}
					}
				}
			}, GET_STATID(STAT_AudioSetParameters));
		}
	}
}

void ISoundGeneratorParameterInterface::SetParameterInternal(FAudioParameter&& InParam)
{
	if (!InParam.ParamName.IsValid())
	{
		return;
	}

	TArray<FAudioParameter>& InstanceParameters = GetInstanceParameters();
	if (FAudioParameter* CurrentParam = FAudioParameter::FindOrAddParam(InstanceParameters, InParam.ParamName))
	{
		CurrentParam->Merge(InParam, false /* bInTakeName */);
	}

	if (IsPlaying())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			FAudioParameter ParamToSet;
			if (USoundBase* Sound = GetSound())
			{
				TArray<FAudioParameter> Params = { MoveTemp(InParam) };
				Sound->InitParameters(Params, SoundGeneratorParameterInterfacePrivate::ProxyFeatureName);
				ParamToSet = MoveTemp(Params[0]);
			}
			else
			{
				ParamToSet = MoveTemp(InParam);
			}

			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SoundGenerator.SetParameter"), STAT_AudioSetParameter, STATGROUP_AudioThreadCommands);

			FAudioThread::RunCommandOnAudioThread([AudioDevice, InstanceID = GetInstanceID(), Param { MoveTemp(ParamToSet) }]() mutable
			{
				if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(InstanceID))
				{
					if (Audio::IParameterTransmitter* Transmitter = ActiveSound->GetTransmitter())
					{
						const FName ParamName = Param.ParamName;
						if (!Transmitter->SetParameter(MoveTemp(Param)))
						{
							UE_LOG(LogAudio, Warning, TEXT("Failed to set parameter '%s'"), *ParamName.ToString());
						}
					}
				}
			}, GET_STATID(STAT_AudioSetParameter));
		}
	}
}
