// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

DECLARE_LOG_CATEGORY_EXTERN(LogComputeFramework, Log, All);

namespace ComputeFramework
{
	/** Returns true if ComputeFramework is supported. */
	ENGINE_API bool IsEnabled(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform);

	/** Rebuild all compute graphs. */
	ENGINE_API void RebuildComputeGraphs();
}
