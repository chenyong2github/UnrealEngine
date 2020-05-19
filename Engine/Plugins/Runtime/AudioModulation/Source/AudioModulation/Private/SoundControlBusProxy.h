// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundModulationProxy.h"
#include "SoundModulatorLFOProxy.h"


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
		ESoundModulatorOperator Operator;

		FControlBusSettings(const USoundControlBusBase& InBus)
			: TModulatorBase<FBusId>(InBus.GetName(), InBus.GetUniqueID())
			, bBypass()
			, DefaultValue(InBus.DefaultValue)
			, Min(InBus.Min)
			, Max(InBus.Max)
			, Operator(InBus.GetOperator())
		{
			for (const USoundBusModulatorBase* Modulator : InBus.Modulators)
			{
				if (const USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
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

		TArray<FLFOHandle> LFOHandles;
		ESoundModulatorOperator Operator;
		FVector2D Range;
	};

	using FBusProxyMap = TMap<FBusId, FControlBusProxy>;
	using FBusHandle = TProxyHandle<FBusId, FControlBusProxy, FControlBusSettings>;
} // namespace AudioModulation