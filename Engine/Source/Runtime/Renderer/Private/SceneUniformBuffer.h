// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "ShaderParameterMacros.h"

BEGIN_SHADER_PARAMETER_STRUCT(FGPUSceneResourceParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, GPUSceneLightmapData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLightSceneData>, GPUSceneLightData)
	SHADER_PARAMETER(uint32, InstanceDataSOAStride)
	SHADER_PARAMETER(uint32, GPUSceneFrameNumber)
	SHADER_PARAMETER(int32, NumInstances)
	SHADER_PARAMETER(int32, NumScenePrimitives)
END_SHADER_PARAMETER_STRUCT()

/**
 * The RDG data struct that is used in FSceneUniformBuffer.
 */
BEGIN_UNIFORM_BUFFER_STRUCT(FSceneUniformParameters, RENDERER_API)
	SHADER_PARAMETER_STRUCT(FGPUSceneResourceParameters, GPUScene)
END_UNIFORM_BUFFER_STRUCT()

/**
 * Holds scene-scoped parameters and stores these in uniform (constant) buffers for access on GPU.
 */
class FSceneUniformBuffer final
{
public:
	RENDERER_API FSceneUniformBuffer();

	// Get and re-create the UB if the cached parameters are modified.
	RENDERER_API TRDGUniformBufferRef<FSceneUniformParameters> GetBuffer(FRDGBuilder& GraphBuilder);
	RENDERER_API FRHIUniformBuffer* GetBufferRHI(FRDGBuilder& GraphBuilder);

	// Read-only view into the parameters. Use Set() to make changes.
	const FSceneUniformParameters& GetParameters() const
	{
		return CachedParameters;
	}

	/**
	 * Set a field in the parameter struct.
	 * The change will be reflected in any buffer that GetBuffer() returns after this call.
	 * Returns true if anything actually changed.
	 */
	RENDERER_API bool Set(const FGPUSceneResourceParameters& GPUScene);

private:
	FSceneUniformParameters CachedParameters;
	TRDGUniformBufferRef<FSceneUniformParameters> Buffer;
	FRHIUniformBuffer* RHIBuffer;
	bool bGPUSceneIsDirty;
};
