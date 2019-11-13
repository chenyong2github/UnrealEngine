// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_AUDIOMODULATION
#if !UE_BUILD_SHIPPING
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationProxy.h"
#include "SoundModulatorLFO.h"


namespace AudioModulation
{
	// Forward Declarations
	struct FReferencedProxies;

	struct FControlBusMixChannelDebugInfo
	{
		float TargetValue;
		float CurrentValue;
	};

	struct FControlBusMixDebugInfo
	{
		FString Name;
		uint32 Id;
		uint32 RefCount;

		TMap<uint32, FControlBusMixChannelDebugInfo> Channels;
	};

	struct FControlBusDebugInfo
	{
		FString Name;
		float DefaultValue;
		float LFOValue;
		float MixValue;
		float Value;
		FVector2D Range;
		uint32 Id;
		uint32 RefCount;
	};

	struct FLFODebugInfo
	{
		FString Name;
		float Value;
		uint32 RefCount;
	};

	class FAudioModulationDebugger
	{
	public:
		FAudioModulationDebugger();

		void UpdateDebugData(const FReferencedProxies& RefProxies);
		bool OnPostHelp(FCommonViewportClient& ViewportClient, const TCHAR* Stream);
		int32 OnRenderStat(FCanvas& Canvas, int32 X, int32 Y, const UFont& Font);
		bool OnToggleStat(FCommonViewportClient& ViewportClient, const TCHAR* Stream);

	private:
		uint8 bActive : 1;
		uint8 bShowRenderStatLFO : 1;
		uint8 bShowRenderStatMix : 1;

		TArray<FControlBusDebugInfo>    FilteredBuses;
		TArray<FLFODebugInfo>           FilteredLFOs;
		TArray<FControlBusMixDebugInfo> FilteredMixes;

		FString BusStringFilter;
		FString LFOStringFilter;
		FString MixStringFilter;
	};
} // namespace AudioModulation
#endif // !UE_BUILD_SHIPPING
#endif // WITH_AUDIOMODULATION
