// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFramework.h"

DEFINE_LOG_CATEGORY(LogComputeFramework);

static int32 GComputeFrameworkMode = 1;
static FAutoConsoleVariableRef CVarComputeFrameworkMode(
	TEXT("r.ComputeFramework.mode"),
	GComputeFrameworkMode,
	TEXT("The mode Compute Framework should operate.\n")
	TEXT("    0: disabled (Default)\n")
	TEXT("    1: enabled\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

bool SupportsComputeFramework(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GComputeFrameworkMode > 0
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& FDataDrivenShaderPlatformInfo::GetSupportsComputeFramework(ShaderPlatform);
}
