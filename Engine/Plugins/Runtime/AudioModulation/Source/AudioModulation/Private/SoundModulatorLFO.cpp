// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorLFO.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"


USoundBusModulatorLFO::USoundBusModulatorLFO(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Shape(ESoundModulatorLFOShape::Sine)
	, Amplitude(0.5f)
	, Frequency(1.0f)
	, Offset(0.5f)
	, bLooping(1)
{
}

void USoundBusModulatorLFO::BeginDestroy()
{
	Super::BeginDestroy();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FAudioDevice* AudioDevice = World->GetAudioDevice();
	if (!AudioDevice)
	{
		return;
	}

	check(AudioDevice->IsModulationPluginEnabled());
	if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
	{
		auto ModulationImpl = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetImpl();
		check(ModulationImpl);

		auto LFOId = static_cast<const AudioModulation::FLFOId>(GetUniqueID());
		ModulationImpl->DeactivateLFO(LFOId);
	}
}


namespace AudioModulation
{
	FModulatorLFOProxy::FModulatorLFOProxy()
		: Offset(0.0f)
		, Value(0.0f)
	{
	}

	FModulatorLFOProxy::FModulatorLFOProxy(const USoundBusModulatorLFO& InLFO)
		: TModulatorProxyRefBase<FLFOId>(InLFO.GetName(), InLFO.GetUniqueID(), InLFO.bAutoActivate)
		, Offset(InLFO.Offset)
		, Value(0.0f)
	{
		LFO.SetGain(InLFO.Amplitude);
		LFO.SetFrequency(InLFO.Frequency);
		LFO.SetMode(InLFO.bLooping ? Audio::ELFOMode::Type::Sync : Audio::ELFOMode::OneShot);

		static_assert(static_cast<int32>(ESoundModulatorLFOShape::COUNT) == static_cast<int32>(Audio::ELFO::Type::NumLFOTypes), "LFOShape/ELFO Type mismatch");
		LFO.SetType(static_cast<Audio::ELFO::Type>(InLFO.Shape));

		LFO.Start();
	}

	float FModulatorLFOProxy::GetValue() const
	{
		return Value;
	}

	void FModulatorLFOProxy::OnUpdateProxy(const USoundModulatorBase& InModulatorArchetype)
	{
		const FModulatorLFOProxy CopyProxy(*CastChecked<USoundBusModulatorLFO>(&InModulatorArchetype));
		auto UpdateProxy = [this, CopyProxy]()
		{
			check(IsInAudioThread());

			LFO = CopyProxy.LFO;
			Offset = CopyProxy.Offset;
			Value = CopyProxy.Value;
		};

		IsInAudioThread() ? UpdateProxy() : FAudioThread::RunCommandOnAudioThread(UpdateProxy);
	}

	void FModulatorLFOProxy::Update(float InElapsed)
	{
		if (InElapsed > 0.0f && LFO.GetFrequency() > 0.0f)
		{
			const float SampleRate = 1.0f / InElapsed;
			LFO.SetSampleRate(SampleRate);
			LFO.Update();
			Value = LFO.Generate() + Offset;
		}
	}
} // namespace AudioModulation
