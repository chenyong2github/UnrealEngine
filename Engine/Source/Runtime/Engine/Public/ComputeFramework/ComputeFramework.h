// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

DECLARE_LOG_CATEGORY_EXTERN(LogComputeFramework, Log, All);

/** Returns true if ComputeFramework is supported. */
extern ENGINE_API bool SupportsComputeFramework(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform);
