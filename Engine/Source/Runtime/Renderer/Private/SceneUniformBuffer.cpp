// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneUniformBuffer.h"
#include "RenderGraphBuilder.h"
#include "SystemTextures.h"

#include "LightSceneData.h"

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(Scene);
IMPLEMENT_STATIC_AND_SHADER_UNIFORM_BUFFER_STRUCT(FSceneUniformParameters, "Scene", Scene);

FSceneUniformBuffer::FSceneUniformBuffer()
: Buffer(nullptr)
, RHIBuffer(nullptr)
, bGPUSceneIsDirty(false)
{
	FMemory::Memzero(CachedParameters);
}

RENDERER_API TRDGUniformBufferRef<FSceneUniformParameters> FSceneUniformBuffer::GetBuffer(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());

	if (Buffer == nullptr || bGPUSceneIsDirty)
	{
		// Never been populated, set up defaults, we defer this since we don't want to create redundant SRVs for the common case where it is not needed.
		if (!bGPUSceneIsDirty)
		{
			FGPUSceneResourceParameters GPUScene{};
			FRDGBufferRef DummyBufferVec4 = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVector4f));
			CachedParameters.GPUScene.GPUSceneInstanceSceneData = GraphBuilder.CreateSRV(DummyBufferVec4);
			CachedParameters.GPUScene.GPUSceneInstancePayloadData = GraphBuilder.CreateSRV(DummyBufferVec4);
			CachedParameters.GPUScene.GPUScenePrimitiveSceneData = GraphBuilder.CreateSRV(DummyBufferVec4);
			CachedParameters.GPUScene.GPUSceneLightmapData = GraphBuilder.CreateSRV(DummyBufferVec4);
			FRDGBufferRef DummyBufferLight = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FLightSceneData));
			CachedParameters.GPUScene.GPUSceneLightData = GraphBuilder.CreateSRV(DummyBufferLight);
			CachedParameters.GPUScene.InstanceDataSOAStride = 0;
			CachedParameters.GPUScene.GPUSceneFrameNumber = 0;
			CachedParameters.GPUScene.NumInstances = 0;
			CachedParameters.GPUScene.NumScenePrimitives = 0;
		}
		// Create and copy cached parameters into the RDG-lifetime struct
		Buffer = GraphBuilder.CreateUniformBuffer(GraphBuilder.AllocObject<FSceneUniformParameters>(CachedParameters));
		RHIBuffer = GraphBuilder.ConvertToExternalUniformBuffer(Buffer); //RT pipeline can't bind RDG UBs, so we must convert to external

		bGPUSceneIsDirty = false;
	}
	return Buffer;
}

RENDERER_API FRHIUniformBuffer* FSceneUniformBuffer::GetBufferRHI(FRDGBuilder& GraphBuilder)
{
	// Ensure the buffer is prepped.
	GetBuffer(GraphBuilder);
	return RHIBuffer;
}

bool FSceneUniformBuffer::Set(const FGPUSceneResourceParameters& GPUScene)
{
	bGPUSceneIsDirty = bGPUSceneIsDirty || FMemory::Memcmp(&CachedParameters.GPUScene, &GPUScene, sizeof(GPUScene)) != 0;
	CachedParameters.GPUScene = GPUScene;
	return bGPUSceneIsDirty;
}
