// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorLFO.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"
#include "SoundModulationProxy.h"


USoundBusModulatorLFO::USoundBusModulatorLFO(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Shape(ESoundModulatorLFOShape::Sine)
	, Amplitude(0.5f)
	, Frequency(1.0f)
	, Offset(0.5f)
	, bLooping(1)
	, bBypass(0)
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

	if (FAudioDevice* AudioDevice = World->GetAudioDevice())
	{
		check(AudioDevice->IsModulationPluginEnabled());
		if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
		{
			auto ModulationImpl = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetImpl();
			check(ModulationImpl);
			ModulationImpl->DeactivateLFO(*this);
		}
	}
}


namespace AudioModulation
{
	FModulatorLFOProxy::FModulatorLFOProxy()
		: Offset(0.0f)
		, Value(1.0f)
		, bBypass(0)
	{
		LFO.SetFrequency(1.0f);
		LFO.Start();
	}

	FModulatorLFOProxy::FModulatorLFOProxy(const USoundBusModulatorLFO& InLFO)
		: TModulatorProxyRefType(InLFO.GetName(), InLFO.GetUniqueID())
		, Offset(InLFO.Offset)
		, Value(1.0f)
		, bBypass(InLFO.bBypass)
	{
		Init(InLFO);
	}

	FModulatorLFOProxy& FModulatorLFOProxy::operator =(const USoundBusModulatorLFO& InLFO)
	{
		Init(InLFO);
		return *this;
	}

	float FModulatorLFOProxy::GetValue() const
	{
		return Value;
	}

	void FModulatorLFOProxy::Init(const USoundBusModulatorLFO& InLFO)
	{
		Offset = InLFO.Offset;
		Value = 1.0f;
		bBypass = InLFO.bBypass;

		LFO.SetGain(InLFO.Amplitude);
		LFO.SetFrequency(InLFO.Frequency);
		LFO.SetMode(InLFO.bLooping ? Audio::ELFOMode::Type::Sync : Audio::ELFOMode::OneShot);

		static_assert(static_cast<int32>(ESoundModulatorLFOShape::COUNT) == static_cast<int32>(Audio::ELFO::Type::NumLFOTypes), "LFOShape/ELFO Type mismatch");
		LFO.SetType(static_cast<Audio::ELFO::Type>(InLFO.Shape));
		LFO.Start();
	}

	bool FModulatorLFOProxy::IsBypassed() const
	{
		return bBypass != 0;
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
