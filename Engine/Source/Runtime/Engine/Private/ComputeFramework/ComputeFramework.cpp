// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFramework.h"

#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeKernelFromText.h"
#include "ShaderCore.h"
#include "UObject/UObjectIterator.h"

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

FAutoConsoleCommand CmdRebuildComputeGraphs(
	TEXT("compute.RebuildComputeGraphs"),
	TEXT("Force all loaded UComputeGraph objects to rebuild."),
	FConsoleCommandDelegate::CreateStatic(ComputeFramework::RebuildComputeGraphs)
);

namespace ComputeFramework
{
	bool IsEnabled(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
	{
		return GComputeFrameworkMode > 0
			&& FDataDrivenShaderPlatformInfo::GetSupportsComputeFramework(ShaderPlatform);
	}

	void RebuildComputeGraphs()
	{
#if WITH_EDITOR
		FlushShaderFileCache();

		for (TObjectIterator<UComputeKernelFromText> It; It; ++It)
		{
			It->ReparseKernelSourceText();
		}
		for (TObjectIterator<UComputeGraph> It; It; ++It)
		{
			It->UpdateResources();
		}
#endif // WITH_EDITOR
	}
}
