// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_AUDIOMODULATION
#if !UE_BUILD_SHIPPING
#include "SoundModulatorBus.h"
#include "SoundModulatorBusMix.h"
#include "SoundModulatorLFO.h"


namespace AudioModulation
{
	class FAudioModulationDebugger
	{
	public:
		FAudioModulationDebugger();

		void UpdateDebugData(
			const BusProxyMap&    ActiveBuses,
			const BusMixProxyMap& ActiveMixes,
			const LFOProxyMap&    ActiveLFOs);
		bool OnPostHelp(FCommonViewportClient& ViewportClient, const TCHAR* Stream);
		int32 OnRenderStat(FCanvas& Canvas, int32 X, int32 Y, const UFont& Font);
		bool OnToggleStat(FCommonViewportClient& ViewportClient, const TCHAR* Stream);

	private:
		uint8 bActive : 1;
		uint8 bShowRenderStatLFO : 1;
		uint8 bShowRenderStatMix : 1;

		TArray<FModulatorBusProxy>    FilteredBuses;
		TArray<FModulatorLFOProxy>    FilteredLFOs;
		TArray<FModulatorBusMixProxy> FilteredMixes;

		FString BusStringFilter;
		FString LFOStringFilter;
		FString MixStringFilter;
	};
} // namespace AudioModulation
#endif // !UE_BUILD_SHIPPING
#endif // WITH_AUDIOMODULATION
