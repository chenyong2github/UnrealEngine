// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorLFO.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"


USoundModulatorLFO::USoundModulatorLFO(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Shape(ESoundModulatorLFOShape::Sine)
	, Amplitude(0.5f)
	, Frequency(1.0f)
	, Offset(0.5f)
	, bLooping(1)
	, bAutoActivate(0)
	, bAutoDeactivate(0)
{
}

void USoundModulatorLFO::BeginDestroy()
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

		auto LFOId = static_cast<const AudioModulation::LFOId>(GetUniqueID());
		ModulationImpl->DeactivateLFO(LFOId);
	}
}

namespace AudioModulation
{
	FModulatorLFOProxy::FModulatorLFOProxy()
		: Id(0)
		, Offset(0.0f)
		, Value(0.0f)
		, bIsActive(false)
		, bAutoDeactivate(false)
	{
	}

	FModulatorLFOProxy::FModulatorLFOProxy(const USoundModulatorLFO& InLFO)
		: Id(static_cast<LFOId>(InLFO.GetUniqueID()))
#if !UE_BUILD_SHIPPING
		, Name(InLFO.GetName())
#endif // !UE_BUILD_SHIPPING
		, Offset(InLFO.Offset)
		, Value(0.0f)
		, bIsActive(false)
		, bAutoDeactivate(InLFO.bAutoDeactivate)
	{
		LFO.SetGain(InLFO.Amplitude);
		LFO.SetFrequency(InLFO.Frequency);
		LFO.SetMode(InLFO.bLooping ? Audio::ELFOMode::Type::Sync : Audio::ELFOMode::OneShot);

		static_assert(static_cast<int32>(ESoundModulatorLFOShape::COUNT) == static_cast<int32>(Audio::ELFO::Type::NumLFOTypes), "LFOShape/ELFO Type mismatch");
		LFO.SetType(static_cast<Audio::ELFO::Type>(InLFO.Shape));

		LFO.Start();
	}

	bool FModulatorLFOProxy::CanDeactivate() const
	{
		return !bIsActive && bAutoDeactivate;
	}

	void FModulatorLFOProxy::ClearIsActive()
	{
		bIsActive = false;
	}

	float FModulatorLFOProxy::GetAmplitude() const
	{
		return LFO.GetGain();
	}

	float FModulatorLFOProxy::GetFreq() const
	{
		return LFO.GetFrequency();
	}

#if !UE_BUILD_SHIPPING
	const FString& FModulatorLFOProxy::GetName() const
	{
		return Name;
	}
#endif // !UE_BUILD_SHIPPING

	LFOId FModulatorLFOProxy::GetId() const
	{
		return Id;
	}

	float FModulatorLFOProxy::GetOffset() const
	{
		return Offset;
	}

	float FModulatorLFOProxy::GetValue() const
	{
		return Value;
	}

	void FModulatorLFOProxy::SetFreq(float InFreq)
	{
		LFO.SetFrequency(InFreq);
	}

	void FModulatorLFOProxy::SetIsActive()
	{
		bIsActive = true;
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
