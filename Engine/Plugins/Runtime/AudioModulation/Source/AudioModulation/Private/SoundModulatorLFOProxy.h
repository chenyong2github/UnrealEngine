// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SoundModulationProxy.h"
#include "SoundModulatorLFO.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	// Modulator Ids
	using FLFOId = uint32;
	extern const FLFOId InvalidLFOId;

	class FModulatorLFOProxy;

	using FLFOProxyMap = TMap<FLFOId, FModulatorLFOProxy>;
	using FLFOHandle = TProxyHandle<FLFOId, FModulatorLFOProxy, USoundBusModulatorLFO>;

	class FModulatorLFOProxy : public TModulatorProxyRefType<FLFOId, FModulatorLFOProxy, USoundBusModulatorLFO>
	{
	public:
		FModulatorLFOProxy();
		FModulatorLFOProxy(const USoundBusModulatorLFO& InLFO, FAudioModulationSystem& InModSystem);

		FModulatorLFOProxy& operator =(const USoundBusModulatorLFO& InLFO);

		float GetValue() const;
		bool IsBypassed() const;
		void Update(float InElapsed);

	private:
		void Init(const USoundBusModulatorLFO& InLFO);

		Audio::FLFO LFO;
		float Offset;
		float Value;
		uint8 bBypass : 1;
	};
} // namespace AudioModulation