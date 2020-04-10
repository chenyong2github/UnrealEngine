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

	class FControlBusProxy : public TModulatorProxyRefType<FBusId, FControlBusProxy, USoundControlBusBase>
	{
	public:
		FControlBusProxy();
		FControlBusProxy(const USoundControlBusBase& Bus, FAudioModulationSystem& InModSystem);

		FControlBusProxy& operator =(const USoundControlBusBase& InLFO);

		float GetDefaultValue() const;
		const TArray<FLFOHandle>& GetLFOHandles() const;
		float GetLFOValue() const;
		float GetMixValue() const;
		FVector2D GetRange() const;
		float GetValue() const;
		void InitLFOs(const USoundControlBusBase& InBus);
		bool IsBypassed() const;
		void MixIn(const float InValue);
		void MixLFO();
		void Reset();

	private:
		void Init(const USoundControlBusBase& InBus);
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
	using FBusHandle = TProxyHandle<FBusId, FControlBusProxy, USoundControlBusBase>;
} // namespace AudioModulation