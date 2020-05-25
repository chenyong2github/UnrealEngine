// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusAudioReverb.h"
#include "OculusAudioMixer.h"
#include "OculusAudioSettings.h"
#include "OculusAudioContextManager.h"

#include "Sound/SoundSubmix.h"


namespace
{
	TSharedPtr<FSubmixEffectOculusReverbPlugin, ESPMode::ThreadSafe> CastEffectToPluginSharedPtr(FSoundEffectSubmixPtr InSubmixEffect)
	{
		return StaticCastSharedPtr<FSubmixEffectOculusReverbPlugin, FSoundEffectSubmix, ESPMode::ThreadSafe>(InSubmixEffect);
	}
} // namespace <>

void FSubmixEffectOculusReverbPlugin::ClearContext()
{
	Context = nullptr;
}

void FSubmixEffectOculusReverbPlugin::Init(const FSoundEffectSubmixInitData& InInitData)
{
	OwningDeviceId = InInitData.DeviceID;
}

void FSubmixEffectOculusReverbPlugin::InitializeContext(const FOculusReverbSubmixInitData& InContextInitData)
{
	Context = FOculusAudioContextManager::GetContextForAudioDevice(OwningDeviceId);
	if (!Context)
	{
		Context = FOculusAudioContextManager::CreateContextForAudioDevice(OwningDeviceId, InContextInitData.BufferLength, InContextInitData.MaxNumSources, InContextInitData.SampleRate);
	}

	check(Context);
}

FSubmixEffectOculusReverbPlugin::FSubmixEffectOculusReverbPlugin()
	: Context(nullptr)
{
}

void FSubmixEffectOculusReverbPlugin::OnProcessAudio(const FSoundEffectSubmixInputData& InputData, FSoundEffectSubmixOutputData& OutputData)
{
	if (ensure(Context))
	{
		int Enabled = 0;
		ovrResult Result = OVRA_CALL(ovrAudio_IsEnabled)(Context, ovrAudioEnable_LateReverberation, &Enabled);
		OVR_AUDIO_CHECK(Result, "Failed to check if reverb is Enabled");

		if (Enabled != 0)
		{
			uint32_t Status = 0;
			Result = OVRA_CALL(ovrAudio_MixInSharedReverbInterleaved)(Context, &Status, OutputData.AudioBuffer->GetData());
			OVR_AUDIO_CHECK(Result, "Failed to process reverb");
		}
	}
}

OculusAudioReverb::OculusAudioReverb()
	: Context(nullptr)
	, ReverbPreset(nullptr)
	, BufferLength(0)
	, MaxNumSources(0)
	, SampleRate(0.0f)
{
}

void OculusAudioReverb::ClearContext()
{
	Context = nullptr;
	if (SubmixEffect.IsValid())
	{
		CastEffectToPluginSharedPtr(SubmixEffect)->ClearContext();
	}
}

FSoundEffectSubmixPtr OculusAudioReverb::GetEffectSubmix()
{
	if (!SubmixEffect.IsValid())
	{
		if (!ReverbPreset)
		{
			ReverbPreset = NewObject<USubmixEffectOculusReverbPluginPreset>();
			ReverbPreset->AddToRoot();
		}

		SubmixEffect = USoundEffectPreset::CreateInstance<FSoundEffectSubmixInitData, FSoundEffectSubmix>(FSoundEffectSubmixInitData(), *ReverbPreset);
		
		if (ensure(SubmixEffect.IsValid()))
		{
			FOculusReverbSubmixInitData InitContextData = { BufferLength, MaxNumSources, SampleRate };
			static_cast<FSubmixEffectOculusReverbPlugin*>(SubmixEffect.Get())->InitializeContext(InitContextData);
			SubmixEffect->SetEnabled(true);
		}
	}

	return SubmixEffect;
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

void OculusAudioReverb::Initialize(const FAudioPluginInitializationParams InitializationParams)
{
	BufferLength = InitializationParams.BufferLength;
	MaxNumSources = InitializationParams.NumSources;
	SampleRate = InitializationParams.SampleRate;
}
