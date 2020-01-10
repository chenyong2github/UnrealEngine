// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils.h"

#include "Templates/Atomic.h"

// displays selected stats on HUD
struct FHUDStats
{
	static FHUDStats& Get();

	void Tick();
	void Reset();

	TAtomic<bool> bEnabled{ false };

	static constexpr uint32 SmoothingPeriod = 3 * 60; // kinda 3 secs for 60FPS

	FSmoothedValue<SmoothingPeriod> EndToEndLatencyMs;
	FSmoothedValue<SmoothingPeriod> EncoderLatencyMs;
	FSmoothedValue<SmoothingPeriod> EncoderBitrateMbps;
	FSmoothedValue<SmoothingPeriod> EncoderQP;
};
