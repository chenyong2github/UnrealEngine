// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeFramework.h"

#include "ComputeFramework/ComputeFrameworkModule.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphWorker.h"
#include "ComputeFramework/ComputeKernelFromText.h"
#include "ComputeFramework/ComputeKernelShaderCompilationManager.h"
#include "ComputeFramework/ComputeSystem.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RenderGraphBuilder.h"
#include "ShaderCore.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogComputeFramework);

static int32 GComputeFrameworkEnable = 1;
static FAutoConsoleVariableRef CVarComputeFrameworkEnable(
	TEXT("r.ComputeFramework.Enable"),
	GComputeFrameworkEnable,
	TEXT("Enable the Compute Framework.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

FAutoConsoleCommand CmdRebuildComputeGraphs(
	TEXT("r.ComputeFramework.RebuildComputeGraphs"),
	TEXT("Force all loaded UComputeGraph objects to rebuild."),
	FConsoleCommandDelegate::CreateStatic(ComputeFramework::RebuildComputeGraphs)
);

namespace ComputeFramework
{
	bool IsSupported(EShaderPlatform ShaderPlatform)
	{
		return FDataDrivenShaderPlatformInfo::GetSupportsComputeFramework(ShaderPlatform);
	}

	bool IsEnabled()
	{
		return GComputeFrameworkEnable > 0;
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

	void TickCompilation(float DeltaSeconds)
	{
#if WITH_EDITOR
		GComputeKernelShaderCompilationManager.Tick(DeltaSeconds);
#endif // WITH_EDITOR
	}

	void FlushWork(FSceneInterface const* InScene, FName InExecutionGroupName)
	{
		FComputeGraphTaskWorker* ComputeGraphWorker = FComputeFrameworkModule::GetComputeSystem()->GetComputeWorker(InScene);
		if (ensure(ComputeGraphWorker))
		{
			ENQUEUE_RENDER_COMMAND(ComputeFrameworkFlushCommand)(
				[ComputeGraphWorker, InExecutionGroupName](FRHICommandListImmediate& RHICmdList)
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					ComputeGraphWorker->SubmitWork(GraphBuilder, InExecutionGroupName, GMaxRHIFeatureLevel);
					GraphBuilder.Execute();
				});
		}
	}
}
