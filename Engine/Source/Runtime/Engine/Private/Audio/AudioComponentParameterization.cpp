// Copyright Epic Games, Inc. All Rights Reserved.
#include "Audio/AudioComponentParameterization.h"

#include "ActiveSound.h"
#include "Audio.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "IAudioExtensionPlugin.h"


void UAudioComponentParameterization::Shutdown()
{
	if (UAudioComponent* OwningComponent = Cast<UAudioComponent>(GetOuter()))
	{
		if (FAudioDevice* AudioDevice = OwningComponent->GetAudioDevice())
		{
			const uint64 MyAudioComponentID = OwningComponent->GetAudioComponentID();
			FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID]() mutable
			{
				if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID))
				{
					if (IAudioInstanceTransmitter* Transmitter = ActiveSound->GetTransmitter())
					{
						Transmitter->Shutdown();
					}
				}
			});
		}
	}
}

void UAudioComponentParameterization::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

void UAudioComponentParameterization::Trigger(FName InName)
{
	// Trigger is just a (true) bool param currently.
	SetValue(InName, true);
}

void UAudioComponentParameterization::SetBool(FName InName, bool InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetBoolArray(FName InName, const TArray<bool>& InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetInt(FName InName, int32 InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetIntArray(FName InName, const TArray<int32>& InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetFloat(FName InName, float InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetFloatArray(FName InName, const TArray<float>& InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetString(FName InName, const FString& InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetStringArray(FName InName, const TArray<FString>& InValue)
{
	SetValue(InName, InValue);
}

void UAudioComponentParameterization::SetObject(FName InName, UObject* InValue)
{
	IAudioProxyDataFactory* ObjectAsFactory = Cast<USoundWave>(InValue);	// FIXME. Possible to query for IAudioProxy support? Metasound used SFINAE

	// The creation of the Proxy object needs to happen where its safe to access UObject, 
	// namely the Game thread. 

	if (ensureAlways(ObjectAsFactory))
	{
		static FName ProxySubsystemName = TEXT("Metasound");
		Audio::FProxyDataInitParams ProxyInitParams;
		ProxyInitParams.NameOfFeatureRequestingProxy = ProxySubsystemName;

		TUniquePtr<Audio::IProxyData> Proxy{ ObjectAsFactory->CreateNewProxyData(ProxyInitParams) };
		SetValue(InName, MoveTemp(Proxy));
	}
}

void UAudioComponentParameterization::SetObjectArray(FName InName, const TArray<UObject*>& InValue)
{
	static FName ProxySubsystemName = TEXT("Metasound");
	Audio::FProxyDataInitParams ProxyInitParams;
	ProxyInitParams.NameOfFeatureRequestingProxy = ProxySubsystemName;

	// The creation of the Proxy object needs to happen where its safe to access UObject, 
	// namely the Game thread. 

	TArray<Audio::IProxyDataPtr> ProxiedInputs;
	for (UObject* i : InValue)
	{
		if (IAudioProxyDataFactory* ProxyFactory = Cast<USoundWave>(i)) // FIXME. Query for IAudioProxyDataFactory
		{
			ProxiedInputs.Emplace(ProxyFactory->CreateNewProxyData(ProxyInitParams));
		}
	}
	SetValue(InName, MoveTemp(ProxiedInputs));
}

template<typename T>
void UAudioComponentParameterization::SetValue(FName InName, T&& InX)
{
	if (InName == NAME_None)
	{
		return;
	}

	if (UAudioComponent* OwningComponent = Cast<UAudioComponent>(GetOuter()))
	{
		if (OwningComponent->IsActive())
		{
			if (FAudioDevice* AudioDevice = OwningComponent->GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetValue"), STAT_AudioSetSoundParameter, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = OwningComponent->GetAudioComponentID();
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InName, InX{ MoveTempIfPossible(InX) }]() mutable
				{
					if (FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID))
					{
						if (IAudioInstanceTransmitter* Transmitter = ActiveSound->GetTransmitter())
						{
							if (!Transmitter->SetParameter(InName, MoveTempIfPossible(InX)))
							{
								UE_LOG(LogAudio, Warning, TEXT("Failed to SetParameter '%s'"), *InName.ToString() );
							}
						}
					}
				}, GET_STATID(STAT_AudioSetSoundParameter));
			}
		}
	}
}
