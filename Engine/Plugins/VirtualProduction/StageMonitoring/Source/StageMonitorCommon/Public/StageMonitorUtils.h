// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "StageMessages.h"

#include "StageMonitorUtils.generated.h"


/**
 * Message containing information about frame timings.
 * Sent at regular intervals
 */
USTRUCT()
struct STAGEMONITORCOMMON_API FFramePerformanceProviderMessage : public FStageProviderPeriodicMessage
{
	GENERATED_BODY()

public:

	FFramePerformanceProviderMessage() = default;

	FFramePerformanceProviderMessage(float GameThreadTime, float RenderThreadTime, float GPUTime, float IdleTime)
		: GameThreadMS(GameThreadTime), RenderThreadMS(RenderThreadTime), GPU_MS(GPUTime), IdleTimeMS(IdleTime)
	{
		extern ENGINE_API float GAverageFPS;
		AverageFPS = GAverageFPS;
	}

	/** Average FrameRate read from GAverageFPS */
	UPROPERTY(VisibleAnywhere, Category = "Performance")
	float AverageFPS = 0.f;

	/** Current GameThread time read from GGameThreadTime in milliseconds */
	UPROPERTY(VisibleAnywhere, Category = "Performance", meta = (Unit = "ms"))
	float GameThreadMS = 0.f;

	/** Current RenderThread time read from GRenderThreadTime in milliseconds */
	UPROPERTY(VisibleAnywhere, Category = "Performance", meta = (Unit = "ms"))
	float RenderThreadMS = 0.f;

	/** Current GPU time read from GGPUFrameTime in milliseconds */
	UPROPERTY(VisibleAnywhere, Category = "Performance", meta = (Unit = "ms"))
	float GPU_MS = 0.f;

	/** Idle time (slept) in milliseconds during last frame */
	UPROPERTY(VisibleAnywhere, Category = "Performance", meta = (Unit = "ms"))
	float IdleTimeMS = 0.f;
};


namespace StageMonitorUtils
{
	STAGEMONITORCOMMON_API FStageInstanceDescriptor GetInstanceDescriptor();
}
