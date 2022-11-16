// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

class FSceneInterface;

DECLARE_LOG_CATEGORY_EXTERN(LogComputeFramework, Log, All);

namespace ComputeFramework
{
	/** Returns true if ComputeFramework is supported on a platform. */
	COMPUTEFRAMEWORK_API bool IsSupported(EShaderPlatform ShaderPlatform);

	/** Returns true if ComputeFramework is currently enabled. */
	COMPUTEFRAMEWORK_API bool IsEnabled();

	/** Rebuild all compute graphs. */
	COMPUTEFRAMEWORK_API void RebuildComputeGraphs();

	/** Tick shader compilation. */
	COMPUTEFRAMEWORK_API void TickCompilation(float DeltaSeconds);

	/** Flush any enqueued ComputeGraph work for a given execution group. */
	COMPUTEFRAMEWORK_API void FlushWork(FSceneInterface const* InScene, FName InExecutionGroupName);
}
