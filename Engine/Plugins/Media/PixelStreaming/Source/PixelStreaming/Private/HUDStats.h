// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils.h"
#include "PixelStreamingSettings.h"

// displays selected stats on HUD
struct FHUDStats
{
	static FHUDStats& Get();

	void Tick();
	void Reset();

	static constexpr uint32 SmoothingPeriod = 3 * 60; // kinda 3 secs for 60FPS

	// Note: FSmoothedValue is thread safe.
	FSmoothedValue<SmoothingPeriod> EndToEndLatencyMs;
	FSmoothedValue<SmoothingPeriod> EncoderLatencyMs;
	FSmoothedValue<SmoothingPeriod> CaptureLatencyMs;
	FSmoothedValue<SmoothingPeriod> EncoderBitrateMbps;
	FSmoothedValue<SmoothingPeriod> EncoderFPS;
	uint32_t EncoderQP;
};
