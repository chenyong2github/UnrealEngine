// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OculusAudioReverb.h"
#include "OculusAudioMixer.h"
#include "OculusAudioSettings.h"

#include "Sound/SoundSubmix.h"


void FSubmixEffectOculusReverbPlugin::SetContext(ovrAudioContext* SharedContext)
{
	FScopeLock ScopeLock(&ContextLock);
	Context = SharedContext;
}

void FSubmixEffectOculusReverbPlugin::ClearContext()
{
	Context = nullptr;
}

FSubmixEffectOculusReverbPlugin::FSubmixEffectOculusReverbPlugin()
	: Context(nullptr)
{
}

void FSubmixEffectOculusReverbPlugin::OnProcessAudio(const FSoundEffectSubmixInputData& InputData, FSoundEffectSubmixOutputData& OutputData)
{
	FScopeLock ScopeLock(&ContextLock);
	if (Context != nullptr && *Context != nullptr)
	{
		int Enabled = 0;
		ovrResult Result = OVRA_CALL(ovrAudio_IsEnabled)(*Context, ovrAudioEnable_LateReverberation, &Enabled);
		OVR_AUDIO_CHECK(Result, "Failed to check if reverb is Enabled");

		if (Enabled != 0)
		{
			uint32_t Status = 0;
			Result = OVRA_CALL(ovrAudio_MixInSharedReverbInterleaved)(*Context, &Status, OutputData.AudioBuffer->GetData());
			OVR_AUDIO_CHECK(Result, "Failed to process reverb");
		}
	}
}

void OculusAudioReverb::SetContext(ovrAudioContext* SharedContext)
{
	if (SharedContext != nullptr)
	{
		Context = SharedContext;
	}

	Submix->SetContext(SharedContext);
}

void OculusAudioReverb::ClearContext()
{
	Context = nullptr;
	for (FSubmixEffectOculusReverbPlugin* Submix : Submixes)
	{
		Submix->ClearContext();
	}
}

FSoundEffectSubmixPtr OculusAudioReverb::GetEffectSubmix()
{
	if (!Submix.IsValid())
	{
		Submix = MakeShared<FSubmixEffectOculusReverbPlugin>();
		Submix->SetEnabled(true);
	}

	return StaticCastSharedPtr<FSoundEffectSubmix, FSubmixEffectOculusReverbPlugin, ESPMode::ThreadSafe>(Submix);
}

USoundSubmix* OculusAudioReverb::GetSubmix()
{
	const UOculusAudioSettings* Settings = GetDefault<UOculusAudioSettings>();
	check(Settings);
	USoundSubmix* ReverbSubmix = Cast<USoundSubmix>(Settings->OutputSubmix.TryLoad());
	if (!ReverbSubmix)
	{
		static const FString DefaultSubmixName = TEXT("Oculus Reverb Submix");
		UE_LOG(LogAudio, Error, TEXT("Failed to load Oculus Reverb Submix from object path '%s' in OculusSettings. Creating '%s' as stub."),
			*Settings->OutputSubmix.GetAssetPathString(),
			*DefaultSubmixName);

		ReverbSubmix = NewObject<USoundSubmix>(USoundSubmix::StaticClass(), *DefaultSubmixName);
		ReverbSubmix->bMuteWhenBackgrounded = true;
	}

	return ReverbSubmix;
}