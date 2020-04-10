// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioModulation.h"
#include "SoundModulationPatchProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulationValue.h"
#include "SoundModulatorLFOProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	using FBusMixId = uint32;
	extern const FBusMixId InvalidBusMixId;

	class FModulatorBusMixChannelProxy : public TModulatorProxyBase<FBusId>
	{
	public:

		FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel, FAudioModulationSystem& ModSystem);

		FString Address;
		uint32 ClassId;
		FSoundModulationValue Value;
		FBusHandle BusHandle;
	};

	class FModulatorBusMixProxy : public TModulatorProxyRefType<FBusMixId, FModulatorBusMixProxy, USoundControlBusMix>
	{
	public:
		enum class EStatus : uint8
		{
			Enabled,
			Stopping,
			Stopped
		};

		FModulatorBusMixProxy(const USoundControlBusMix& InMix, FAudioModulationSystem& InModSystem);

		FModulatorBusMixProxy& operator =(const USoundControlBusMix& InBusMix);

		EStatus GetStatus() const;

		// Resets channel map
		void Reset();

		void SetEnabled(const USoundControlBusMix& InBusMix);
		void SetMix(const TArray<FSoundControlBusMixChannel>& InChannels);
		void SetMixByFilter(const FString& InAddressFilter, uint32 InFilterClassId, const FSoundModulationValue& InValue);
		void SetStopping();

		void Update(const float Elapsed, FBusProxyMap& ProxyMap);

		using FChannelMap = TMap<FBusId, FModulatorBusMixChannelProxy>;
		FChannelMap Channels;

	private:
		EStatus Status;
	};

	using FBusMixProxyMap = TMap<FBusMixId, FModulatorBusMixProxy>;
	using FBusMixHandle = TProxyHandle<FBusMixId, FModulatorBusMixProxy, USoundControlBusMix>;
} // namespace AudioModulation