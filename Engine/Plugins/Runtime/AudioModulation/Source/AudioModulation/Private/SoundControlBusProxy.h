// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundModulationParameter.h"
#include "SoundModulationProxy.h"
#include "SoundModulationGeneratorLFOProxy.h"


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
		float Min;
		float Max;

		TArray<FModulatorLFOSettings> LFOSettings;
		Audio::FModulationMixFunction MixFunction;

		FControlBusSettings(const USoundControlBusBase& InBus)
			: TModulatorBase<FBusId>(InBus.GetName(), InBus.GetUniqueID())
			, bBypass()
			, DefaultValue(InBus.GetDefaultValue())
			, Min(InBus.GetMin())
			, Max(InBus.GetMax())
			, MixFunction(InBus.GetMixFunction())
		{
			for (const USoundModulationGenerator* Modulator : InBus.Modulators)
			{
				if (const USoundModulationGeneratorLFO* LFO = Cast<USoundModulationGeneratorLFO>(Modulator))
				{
					LFOSettings.Add(*LFO);
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
		const TArray<FLFOHandle>& GetLFOHandles() const;
		float GetLFOValue() const;
		float GetMixValue() const;
		FVector2D GetRange() const;
		float GetValue() const;
		bool IsBypassed() const;
		void MixIn(const float InValue);
		void MixLFO();
		void Reset();

	private:
		void Init(const FControlBusSettings& InSettings);
		float Mix(float ValueA) const;

		float DefaultValue;

		// Cached values
		float LFOValue;
		float MixValue;

		bool bBypass;

		Audio::FModulationMixFunction MixFunction;
		TArray<FLFOHandle> LFOHandles;
		FVector2D Range;
	};

	using FBusProxyMap = TMap<FBusId, FControlBusProxy>;
	using FBusHandle = TProxyHandle<FBusId, FControlBusProxy, FControlBusSettings>;
} // namespace AudioModulation