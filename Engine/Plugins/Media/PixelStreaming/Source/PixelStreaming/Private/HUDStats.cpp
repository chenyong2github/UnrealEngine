// Copyright Epic Games, Inc. All Rights Reserved.

#include "HUDStats.h"
#include "Engine/Engine.h"


FHUDStats& FHUDStats::Get()
{
	static FHUDStats Stats;
	return Stats;
}

void FHUDStats::Tick()
{
	bool bHUDEnabled = PixelStreamingSettings::CVarPixelStreamingHudStats.GetValueOnGameThread();

	if (!GEngine || !bHUDEnabled)
	{
		return;
	}

	if(bHUDEnabled)
	{
		GAreScreenMessagesEnabled = true;
	}

	// display stats on HUD

	double NowMillis = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
	GEngine->AddOnScreenDebugMessage(1, 0, FColor::Green, *FString::Printf(TEXT("Timestamps: %f ms"), NowMillis), false /* newer on top */, FVector2D{ 5, 5 } /* text scale */);

	GEngine->AddOnScreenDebugMessage(2, 0, FColor::Green, *FString::Printf(TEXT("End to end latency: %.0f ms"), EndToEndLatencyMs.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(3, 0, FColor::Green, *FString::Printf(TEXT("Encoder latency: %.0f ms"), EncoderLatencyMs.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(4, 0, FColor::Green, *FString::Printf(TEXT("Capture latency: %.0f ms"), CaptureLatencyMs.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(5, 0, FColor::Green, *FString::Printf(TEXT("Encoder FPS: %.0f"), EncoderFPS.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(6, 0, FColor::Green, *FString::Printf(TEXT("Render FPS: %.0f"), 1.0f / FApp::GetDeltaTime() ), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(7, 0, FColor::Green, *FString::Printf(TEXT("Encoder bitrate: %.3f Mbps"), EncoderBitrateMbps.Get()), false /* newer on top */);

	GEngine->AddOnScreenDebugMessage(8, 0, FColor::Green, *FString::Printf(TEXT("QP: %d"), EncoderQP), false /* newer on top */);
	
}

void FHUDStats::Reset()
{
	EndToEndLatencyMs.Reset();
	EncoderLatencyMs.Reset();
	EncoderBitrateMbps.Reset();
	CaptureLatencyMs.Reset();
	EncoderQP = 0;
}