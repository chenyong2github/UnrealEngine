// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OculusAudioReverb.h"
#include "OculusAudioMixer.h"


void FSubmixEffectOculusReverbPlugin::SetContext(ovrAudioContext* SharedContext)
{
	FScopeLock ScopeLock(&ContextLock);
	Context = SharedContext;
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
	
	for (FSubmixEffectOculusReverbPlugin* Submix : Submixes)
	{
		Submix->SetContext(SharedContext);
	}
}

FSoundEffectSubmix* OculusAudioReverb::GetEffectSubmix(class USoundSubmix* Submix)
{
	FSubmixEffectOculusReverbPlugin* ReverbSubmix = new FSubmixEffectOculusReverbPlugin();
	Submixes.Add(ReverbSubmix);
	return ReverbSubmix;
}
