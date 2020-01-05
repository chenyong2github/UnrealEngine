// Copyright Epic Games, Inc. All Rights Reserved.

#include "HUDStats.h"

#include "Engine/Engine.h"

namespace
{
	bool bHudStatsEnabledCVar = false;

	FAutoConsoleVariableRef CVarHudStats(
		TEXT("PixelStreaming.HUDStats"),
		bHudStatsEnabledCVar,
		TEXT("Whether to show PixelStreaming stats on HUD"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
		{
			FHUDStats& Stats = FHUDStats::Get();
			Stats.bEnabled = bHudStatsEnabledCVar;
			Stats.Reset();
		}),
		ECVF_Cheat
	);
}

FHUDStats& FHUDStats::Get()
{
	static FHUDStats Stats;
	return Stats;
}

void FHUDStats::Tick()
{
	if (!GEngine || !bEnabled)
	{
		return;
	}

	// display stats on HUD

	double NowSecs = rtc::TimeMillis() % 1000000 / 1000.0;
	GEngine->AddOnScreenDebugMessage(1, 0, FColor::Green, *FString::Printf(TEXT("time %.3f ms"), NowSecs), false /* newer on top */, FVector2D{ 5, 5 } /* text scale */);

	GEngine->AddOnScreenDebugMessage(2, 0, FColor::Green, *FString::Printf(TEXT("latency ms: end-to-end %.0f, encoder %.0f"), EndToEndLatencyMs.Get(), EncoderLatencyMs.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(3, 0, FColor::Green, *FString::Printf(TEXT("bitrate %.3f Mbps"), EncoderBitrateMbps.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(4, 0, FColor::Green, *FString::Printf(TEXT("QP: %.0f"), EncoderQP.Get()), false /* newer on top */);
}

void FHUDStats::Reset()
{
	EndToEndLatencyMs.Reset();
	EncoderLatencyMs.Reset();
	EncoderBitrateMbps.Reset();
	EncoderQP.Reset();
}