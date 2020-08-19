// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationGeneratorLFOProxy.h"

#include "AudioModulation.h"
#include "AudioModulationSystem.h"


namespace AudioModulation
{
	const FLFOId InvalidLFOId = INDEX_NONE;

	FModulatorLFOProxy::FModulatorLFOProxy()
	{
		LFO.SetFrequency(1.0f);
		LFO.Start();
	}

	FModulatorLFOProxy::FModulatorLFOProxy(const FModulatorLFOSettings& InSettings, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), InModSystem)
	{
		Init(InSettings);
	}

	FModulatorLFOProxy& FModulatorLFOProxy::operator =(const FModulatorLFOSettings& InSettings)
	{
		Init(InSettings);
		return *this;
	}

	const Audio::FLFO& FModulatorLFOProxy::GetLFO() const
	{
		return LFO;
	}

	float FModulatorLFOProxy::GetOffset() const
	{
		return Offset;
	}

	float FModulatorLFOProxy::GetValue() const
	{
		return Value;
	}

	void FModulatorLFOProxy::Init(const FModulatorLFOSettings& InLFO)
	{
		Offset = InLFO.Offset;
		Value = 1.0f;
		bBypass = InLFO.bBypass;

		LFO.SetGain(InLFO.Amplitude);
		LFO.SetFrequency(InLFO.Frequency);
		LFO.SetMode(InLFO.bLooping ? Audio::ELFOMode::Type::Sync : Audio::ELFOMode::OneShot);

		static_assert(static_cast<int32>(ESoundModulationGeneratorLFOShape::COUNT) == static_cast<int32>(Audio::ELFO::Type::NumLFOTypes), "LFOShape/ELFO Type mismatch");
		LFO.SetType(static_cast<Audio::ELFO::Type>(InLFO.Shape));
		LFO.Start();
	}

	bool FModulatorLFOProxy::IsBypassed() const
	{
		return bBypass != 0;
	}

	void FModulatorLFOProxy::Update(double InElapsed)
	{
		if (InElapsed > 0.0f && LFO.GetFrequency() > 0.0f)
		{
			const float SampleRate = static_cast<float>(1.0 / InElapsed);
			LFO.SetSampleRate(SampleRate);
			LFO.Update();
			Value = LFO.Generate() + Offset;
		}
	}
} // namespace AudioModulation
