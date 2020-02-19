// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioEndpoint.h"

Audio::FPatchInput IAudioEndpoint::PatchNewInput(float ExpectedDurationPerRender, float& OutSampleRate, int32& OutNumChannels)
{
	OutSampleRate = GetSampleRate();
	OutNumChannels = GetNumChannels();

	// For average case scenarios, we need to buffer at least the sum of the number of input frames and the number of output frames per callback.
	// A good heuristic for doing this while retaining some extra space in the buffer is doubling the max of these two values.
	int32 NumSamplesToBuffer = FMath::CeilToInt(ExpectedDurationPerRender * OutNumChannels * OutSampleRate);
	if (EndpointRequiresCallback())
	{
		NumSamplesToBuffer = FMath::Max(GetDesiredNumFrames() * OutNumChannels, NumSamplesToBuffer) * 2;
	}
	else
	{
		NumSamplesToBuffer *= 2;
	}

	return PatchMixer.AddNewInput(NumSamplesToBuffer, 1.0f);
}

void IAudioEndpoint::SetNewSettings(TUniquePtr<IAudioEndpointSettingsProxy>&& InNewSettings)
{
	FScopeLock ScopeLock(&CurrentSettingsCriticalSection);

	CurrentSettings = MoveTemp(InNewSettings);
}

void IAudioEndpoint::ProcessAudioIfNeccessary()
{
	const bool bShouldExecuteCallback = !RenderCallback.IsValid() && EndpointRequiresCallback();
	if (bShouldExecuteCallback)
	{
		RunCallbackSynchronously();
	}
}

int32 IAudioEndpoint::PopAudio(float* OutAudio, int32 NumSamples)
{
	check(OutAudio);
	return PatchMixer.PopAudio(OutAudio, NumSamples, false);
}

void IAudioEndpoint::PollSettings(TFunctionRef<void(const IAudioEndpointSettingsProxy*)> SettingsCallback)
{
	FScopeLock ScopeLock(&CurrentSettingsCriticalSection);
	SettingsCallback(CurrentSettings.Get());
}

void IAudioEndpoint::DisconnectAllInputs()
{
	PatchMixer.DisconnectAllInputs();
}

void IAudioEndpoint::StartRunningAsyncCallback()
{
	if (!ensureMsgf(GetSampleRate() > 0.0f, TEXT("Invalid sample rate returned!")))
	{
		return;
	}

	float CallbackDuration = ((float)GetDesiredNumFrames()) / GetSampleRate();

	RenderCallback.Reset(new Audio::FMixerNullCallback(CallbackDuration, [&]()
	{
		RunCallbackSynchronously();
	}));
}

void IAudioEndpoint::StopRunningAsyncCallback()
{
	RenderCallback.Reset();
}

void IAudioEndpoint::RunCallbackSynchronously()
{
	const int32 NumSamplesToBuffer = GetDesiredNumFrames() * GetNumChannels();

	BufferForRenderCallback.Reset();
	BufferForRenderCallback.AddUninitialized(NumSamplesToBuffer);

	while (PatchMixer.MaxNumberOfSamplesThatCanBePopped() >= NumSamplesToBuffer)
	{
		int32 PopResult = PatchMixer.PopAudio(BufferForRenderCallback.GetData(), BufferForRenderCallback.Num(), false);
		check(PopResult == BufferForRenderCallback.Num() || PopResult < 0);

		const TArrayView<const float> PoppedAudio = TArrayView<const float>(BufferForRenderCallback);

		auto CallbackWithSettings = [&, PoppedAudio](const IAudioEndpointSettingsProxy* InSettings)
		{
			if (!OnAudioCallback(PoppedAudio, GetNumChannels(), InSettings))
			{
				DisconnectAllInputs();
			}
		};

		PollSettings(CallbackWithSettings);
	}
}

TArray<FName> IAudioEndpointFactory::GetAvailableEndpointTypes()
{
	TArray<FName> SoundfieldFormatNames;

	SoundfieldFormatNames.Add(GetTypeNameForDefaultEndpoint());

	TArray<IAudioEndpointFactory*> Factories = IModularFeatures::Get().GetModularFeatureImplementations<IAudioEndpointFactory>(GetModularFeatureName());
	for (IAudioEndpointFactory* Factory : Factories)
	{
		SoundfieldFormatNames.Add(Factory->GetEndpointTypeName());
	}

	return SoundfieldFormatNames;
}
