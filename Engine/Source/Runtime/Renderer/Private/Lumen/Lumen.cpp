// Copyright Epic Games, Inc. All Rights Reserved.

#include "Lumen.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "SceneRendering.h"

static TAutoConsoleVariable<int32> CVarLumenAsyncCompute(
	TEXT("r.Lumen.AsyncCompute"),
	1,
	TEXT("Whether Lumen should use async compute if supported."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenThreadGroupSize32(
	TEXT("r.Lumen.ThreadGroupSize32"),
	1,
	TEXT("Whether to prefer dispatches in groups of 32 threads on HW which supports it (instead of standard 64)."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

bool Lumen::UseAsyncCompute(const FViewFamilyInfo& ViewFamily)
{
	// #lumen_todo: support async path also for HWRT
	return GSupportsEfficientAsyncCompute 
		&& CVarLumenAsyncCompute.GetValueOnRenderThread() != 0
		&& !Lumen::UseHardwareRayTracing(ViewFamily);
}

bool Lumen::UseThreadGroupSize32()
{
	return GRHISupportsWaveOperations && GRHIMinimumWaveSize <= 32 && CVarLumenThreadGroupSize32.GetValueOnRenderThread() != 0;
}