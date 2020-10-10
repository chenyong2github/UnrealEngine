// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDeviceManager.h"
#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundModulationParameter.h"
#include "SoundModulationProxy.h"
#include "SoundModulationGeneratorProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	using FBusId = uint32;
	extern const FBusId InvalidBusId;

	struct FControlBusSettings : public TModulatorBase<FBusId>
	{
		bool bBypass;
		float DefaultValue;

		TArray<FModulationGeneratorSettings> GeneratorSettings;
		Audio::FModulationMixFunction MixFunction;

		FControlBusSettings(const USoundControlBus& InBus, Audio::FDeviceId InDeviceId)
			: TModulatorBase<FBusId>(InBus.GetName(), InBus.GetUniqueID())
			, bBypass()
			, DefaultValue(InBus.GetDefaultNormalizedValue())
			, MixFunction(InBus.GetMixFunction())
		{
			for (const USoundModulationGenerator* Generator : InBus.Generators)
			{
				if (Generator)
				{
					GeneratorSettings.Emplace(FModulationGeneratorSettings(*Generator, InDeviceId));
				}
			}
		}
	};

	class FControlBusProxy : public TModulatorProxyRefType<FBusId, FControlBusProxy, FControlBusSettings>
	{
	public:
		FControlBusProxy();
		FControlBusProxy(const FControlBusSettings& InSettings, FAudioModulationSystem& InModSystem);

		FControlBusProxy& operator =(const FControlBusSettings& InSettings);

		float GetDefaultValue() const;
		const TArray<FGeneratorHandle>& GetGeneratorHandles() const;
		float GetGeneratorValue() const;
		float GetMixValue() const;
		float GetValue() const;
		bool IsBypassed() const;
		void MixIn(const float InValue);
		void MixGenerators();
		void Reset();

	private:
		void Init(const FControlBusSettings& InSettings);
		float Mix(float ValueA) const;

		float DefaultValue;

		// Cached values
		float GeneratorValue;
		float MixValue;

		bool bBypass;

		Audio::FModulationMixFunction MixFunction;
		TArray<FGeneratorHandle> GeneratorHandles;
	};

	using FBusProxyMap = TMap<FBusId, FControlBusProxy>;
	using FBusHandle = TProxyHandle<FBusId, FControlBusProxy, FControlBusSettings>;
} // namespace AudioModulation