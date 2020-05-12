// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioModulation.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulationValue.h"
#include "SoundModulatorLFOProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	using FBusMixId = uint32;
	extern const FBusMixId InvalidBusMixId;

	class FModulatorBusMixChannelSettings : public TModulatorBase<FBusId>
	{
	public:
		FModulatorBusMixChannelSettings(const FSoundControlBusMixChannel& InChannel);

		FString Address;
		uint32 ClassId;
		FSoundModulationValue Value;
		FControlBusSettings BusSettings;
	};

	class FModulatorBusMixSettings : public TModulatorBase<FBusMixId>
	{
	public:
		FModulatorBusMixSettings(const USoundControlBusMix& InBusMix);

		TArray<FModulatorBusMixChannelSettings> Channels;
	};

	class FModulatorBusMixChannelProxy : public TModulatorBase<FBusId>
	{
	public:

		FModulatorBusMixChannelProxy(const FModulatorBusMixChannelSettings& InSettings, FAudioModulationSystem& OutModSystem);

		FString Address;
		uint32 ClassId;
		FSoundModulationValue Value;
		FBusHandle BusHandle;
	};

	class FModulatorBusMixProxy : public TModulatorProxyRefType<FBusMixId, FModulatorBusMixProxy, FModulatorBusMixSettings>
	{
	public:
		enum class EStatus : uint8
		{
			Enabled,
			Stopping,
			Stopped
		};

		FModulatorBusMixProxy(const FModulatorBusMixSettings& InMix, FAudioModulationSystem& InModSystem);

		FModulatorBusMixProxy& operator =(const FModulatorBusMixSettings& InSettings);

		EStatus GetStatus() const;

		// Resets channel map
		void Reset();

		void SetEnabled(const FModulatorBusMixSettings& InSettings);
		void SetMix(const TArray<FModulatorBusMixChannelSettings>& InChannels);
		void SetMixByFilter(const FString& InAddressFilter, uint32 InFilterClassId, const FSoundModulationValue& InValue);
		void SetStopping();

		void Update(const double Elapsed, FBusProxyMap& ProxyMap);

		using FChannelMap = TMap<FBusId, FModulatorBusMixChannelProxy>;
		FChannelMap Channels;

	private:
		EStatus Status;
	};

	using FBusMixProxyMap = TMap<FBusMixId, FModulatorBusMixProxy>;
	using FBusMixHandle = TProxyHandle<FBusMixId, FModulatorBusMixProxy, FModulatorBusMixSettings>;
} // namespace AudioModulation