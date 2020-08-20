// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SoundModulationGeneratorLFO.h"
#include "SoundModulationProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;
	class FModulatorLFOProxy;

	struct FModulatorLFOSettings;

	// Modulator Ids
	using FLFOId = uint32;
	extern const FLFOId InvalidLFOId;

	using FLFOProxyMap = TMap<FLFOId, FModulatorLFOProxy>;
	
	using FLFOHandle = TProxyHandle<FLFOId, FModulatorLFOProxy, FModulatorLFOSettings>;

	struct FModulatorLFOSettings : public TModulatorBase<FLFOId>
	{
		float Amplitude;
		float Frequency;
		float Offset;

		uint8 bBypass : 1;
		uint8 bLooping : 1;

		ESoundModulationGeneratorLFOShape Shape;

		FModulatorLFOSettings(const USoundModulationGeneratorLFO& InLFO)
			: TModulatorBase<FLFOId>(InLFO.GetName(), InLFO.GetUniqueID())
			, Amplitude(InLFO.Amplitude)
			, Frequency(InLFO.Frequency)
			, Offset(InLFO.Offset)
			, bBypass(InLFO.bBypass)
			, bLooping(InLFO.bLooping)
			, Shape(InLFO.Shape)
		{
		}
	};

	class FModulatorLFOProxy : public TModulatorProxyRefType<FLFOId, FModulatorLFOProxy, FModulatorLFOSettings>
	{
	public:
		FModulatorLFOProxy();
		FModulatorLFOProxy(const FModulatorLFOSettings& InSettings, FAudioModulationSystem& InModSystem);

		FModulatorLFOProxy& operator =(const FModulatorLFOSettings& InLFO);

		const Audio::FLFO& GetLFO() const;
		float GetOffset() const;
		float GetValue() const;
		bool IsBypassed() const;
		void Update(double InElapsed);

	private:
		void Init(const FModulatorLFOSettings& InLFO);

		Audio::FLFO LFO;

		float Offset = 0.0f;
		float Value = 1.0f;
		uint8 bBypass = 0;
	};
} // namespace AudioModulation