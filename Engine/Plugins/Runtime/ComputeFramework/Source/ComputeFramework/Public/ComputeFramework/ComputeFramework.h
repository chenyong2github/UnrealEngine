// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

DECLARE_LOG_CATEGORY_EXTERN(LogComputeFramework, Log, All);

namespace ComputeFramework
{
	/** Returns true if ComputeFramework is supported. */
	COMPUTEFRAMEWORK_API bool IsEnabled(EShaderPlatform ShaderPlatform);

	/** Rebuild all compute graphs. */
	COMPUTEFRAMEWORK_API void RebuildComputeGraphs();

	/** Tick shader compilation. */
	COMPUTEFRAMEWORK_API void TickCompilation(float DeltaSeconds);
}
