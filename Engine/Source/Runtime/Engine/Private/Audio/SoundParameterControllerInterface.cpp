// Copyright Epic Games, Inc. All Rights Reserved.
#include "Audio/SoundParameterControllerInterface.h"

#include "ActiveSound.h"
#include "Audio.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "IAudioExtensionPlugin.h"


namespace SoundParameterControllerInterfacePrivate
{
	static const FName ProxyFeatureName("SoundParameterControllerInterface");
} // namespace SoundParameterControllerInterfacePrivate

USoundParameterControllerInterface::USoundParameterControllerInterface(FObjectInitializer const& InObjectInitializer)
	: Super(InObjectInitializer)
{
}

void ISoundParameterControllerInterface::ResetParameters()
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		if (IsPlaying() && !GetDisableParameterUpdatesWhilePlaying())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SoundParameterControllerInterface.ResetParameters"), STAT_AudioResetParameters, STATGROUP_AudioThreadCommands);
			AudioDevice->SendCommandToActiveSounds(GetInstanceOwnerID(), [] (FActiveSound& ActiveSound)
			{
				if (Audio::IParameterTransmitter* Transmitter = ActiveSound.GetTransmitter())
				{
					Transmitter->Reset();
				}
			}, GET_STATID(STAT_AudioResetParameters));
		}
	}
}

void ISoundParameterControllerInterface::SetTriggerParameter(FName InName)
{
	if (InName.IsNone())
	{
		return;
	}

	if (IsPlaying() && !GetDisableParameterUpdatesWhilePlaying())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			FAudioParameter ParamToSet = FAudioParameter(InName, true);
			if (USoundBase* Sound = GetSound())
			{
				TArray<FAudioParameter> Params = { MoveTemp(ParamToSet) };
				Sound->InitParameters(Params, SoundParameterControllerInterfacePrivate::ProxyFeatureName);
				if (Params.Num() == 0)
				{
					// USoundBase::InitParameters(...) can remove parameters. 
					// Exit early if the parameter is removed.
					return;
				}
				ParamToSet = MoveTemp(Params[0]);
			}

			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SoundParameterControllerInterface.ExecuteTriggerParameter"), STAT_AudioExecuteTriggerParameter, STATGROUP_AudioThreadCommands);

			AudioDevice->SendCommandToActiveSounds(GetInstanceOwnerID(), [AudioDevice, Param = MoveTemp(ParamToSet)](FActiveSound& ActiveSound)
			{
				if (Audio::IParameterTransmitter* Transmitter = ActiveSound.GetTransmitter())
				{
					const FName ParamName = Param.ParamName;

					// Must be copied as original version must be preserved in case command is called on multiple ActiveSounds.
					FAudioParameter TempParam = Param;
					if (!Transmitter->SetParameter(MoveTemp(TempParam)))
					{
						UE_LOG(LogAudio, Warning, TEXT("Failed to execute trigger parameter '%s'"), *ParamName.ToString());
					}
				}
			}, GET_STATID(STAT_AudioExecuteTriggerParameter));
		}
	}
}

void ISoundParameterControllerInterface::SetBoolParameter(FName InName, bool InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetBoolArrayParameter(FName InName, const TArray<bool>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetIntParameter(FName InName, int32 InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetIntArrayParameter(FName InName, const TArray<int32>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetFloatParameter(FName InName, float InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetFloatArrayParameter(FName InName, const TArray<float>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetStringParameter(FName InName, const FString& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetStringArrayParameter(FName InName, const TArray<FString>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetObjectParameter(FName InName, UObject* InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetObjectArrayParameter(FName InName, const TArray<UObject*>& InValue)
{
	SetParameterInternal(FAudioParameter(InName, InValue));
}

void ISoundParameterControllerInterface::SetParameter(FAudioParameter&& InValue)
{
	SetParameterInternal(MoveTemp(InValue));
}

void ISoundParameterControllerInterface::SetParameters(TArray<FAudioParameter>&& InValues)
{
	for (const FAudioParameter& Value : InValues)
	{
		TArray<FAudioParameter>& InstanceParameters = GetInstanceParameters();
		if (FAudioParameter* CurrentParam = FAudioParameter::FindOrAddParam(InstanceParameters, Value.ParamName))
		{
			CurrentParam->Merge(Value, false /* bInTakeName */);
		}
	}

	if (IsPlaying() && !GetDisableParameterUpdatesWhilePlaying())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			TArray<FAudioParameter> ParamsToSet;
			if (USoundBase* Sound = GetSound())
			{
				Sound->InitParameters(InValues, SoundParameterControllerInterfacePrivate::ProxyFeatureName);
			}

			ParamsToSet = MoveTemp(InValues);

			if (ParamsToSet.Num() > 0)
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SoundParameterControllerInterface.SetParameters"), STAT_AudioSetParameters, STATGROUP_AudioThreadCommands);
				AudioDevice->SendCommandToActiveSounds(GetInstanceOwnerID(), [AudioDevice, Params = MoveTemp(ParamsToSet)](FActiveSound& ActiveSound)
				{
					if (Audio::IParameterTransmitter* Transmitter = ActiveSound.GetTransmitter())
					{
						for (const FAudioParameter& Param : Params)
						{
							const FName ParamName = Param.ParamName;
							if (!ParamName.IsNone())
							{
								// Must be copied as original version must be preserved in case command is called on multiple ActiveSounds.
								FAudioParameter TempParam = Param;
								if (!Transmitter->SetParameter(MoveTemp(TempParam)))
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
}

void ISoundParameterControllerInterface::SetParameterInternal(FAudioParameter&& InParam)
{
	if (InParam.ParamName.IsNone())
	{
		return;
	}

	TArray<FAudioParameter>& InstanceParameters = GetInstanceParameters();
	if (FAudioParameter* CurrentParam = FAudioParameter::FindOrAddParam(InstanceParameters, InParam.ParamName))
	{
		CurrentParam->Merge(InParam, false /* bInTakeName */);
	}

	if (IsPlaying() && !GetDisableParameterUpdatesWhilePlaying())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			FAudioParameter ParamToSet;
			if (USoundBase* Sound = GetSound())
			{
				TArray<FAudioParameter> Params = { MoveTemp(InParam) };
				Sound->InitParameters(Params, SoundParameterControllerInterfacePrivate::ProxyFeatureName);
				if (Params.Num() == 0)
				{
					// USoundBase::InitParameters(...) can remove parameters. 
					// Exit early if the parameter is removed.
					return;
				}
				ParamToSet = MoveTemp(Params[0]);
			}
			else
			{
				ParamToSet = MoveTemp(InParam);
			}

			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SoundParameterControllerInterface.SetParameter"), STAT_AudioSetParameter, STATGROUP_AudioThreadCommands);

			AudioDevice->SendCommandToActiveSounds(GetInstanceOwnerID(), [AudioDevice, Param = MoveTemp(ParamToSet)](FActiveSound& ActiveSound)
			{
				if (Audio::IParameterTransmitter* Transmitter = ActiveSound.GetTransmitter())
				{
					const FName ParamName = Param.ParamName;

					// Must be copied as original version must be preserved in case command is called on multiple ActiveSounds.
					FAudioParameter TempParam = Param;
					if (!Transmitter->SetParameter(MoveTemp(TempParam)))
					{
						UE_LOG(LogAudio, Warning, TEXT("Failed to set parameter '%s'"), *ParamName.ToString());
					}
				}
			}, GET_STATID(STAT_AudioSetParameter));
		}
	}
}
