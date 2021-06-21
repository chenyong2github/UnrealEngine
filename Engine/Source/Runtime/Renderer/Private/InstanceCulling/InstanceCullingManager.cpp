// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceCullingManager.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "RendererModule.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "InstanceCulling/InstanceCullingContext.h"

FInstanceCullingManager::~FInstanceCullingManager()
{
	for (auto& LoadBalancer : BatchedCullingScratch.MergedContext.LoadBalancers)
	{
		if (LoadBalancer != nullptr)
		{
			delete LoadBalancer;
		}
	}
}

int32 FInstanceCullingManager::RegisterView(const FViewInfo& ViewInfo)
{
	if (!bIsEnabled)
	{
		return 0;
	}

	Nanite::FPackedViewParams Params;
	Params.ViewMatrices = ViewInfo.ViewMatrices;
	Params.PrevViewMatrices = ViewInfo.PrevViewInfo.ViewMatrices;
	Params.ViewRect = ViewInfo.ViewRect;
	// TODO: faking this here (not needed for culling, until we start involving multi-view and HZB)
	Params.RasterContextSize = ViewInfo.ViewRect.Size();
	return RegisterView(Params);
}



int32 FInstanceCullingManager::RegisterView(const Nanite::FPackedViewParams& Params)
{
	if (!bIsEnabled)
	{
		return 0;
	}
	CullingViews.Add(CreatePackedView(Params));
	return CullingViews.Num() - 1;
}

void FInstanceCullingManager::CullInstances(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FInstanceCullingManager::CullInstances);
	RDG_EVENT_SCOPE(GraphBuilder, "CullInstances");


	CullingIntermediate.CullingViews = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.CullingViews"), CullingViews);
	CullingIntermediate.NumViews = CullingViews.Num();

	CullingIntermediate.DummyUniformBuffer = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
}

void FInstanceCullingManager::BeginDeferredCulling(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene)
{
	FInstanceCullingContext::BuildRenderingCommandsDeferred(GraphBuilder, GPUScene, *this);
}
