// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BasePassRendering.inl: Base pass rendering implementations.
		(Due to forward declaration issues)
=============================================================================*/

#pragma once

#include "CoreFwd.h"

class FMeshMaterialShader;
class FPrimitiveSceneProxy;
class FRHICommandList;
class FSceneView;
class FVertexFactory;
class FViewInfo;
struct FMeshBatch;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;

template<typename LightMapPolicyType>
void TBasePassVertexShaderPolicyParamType<LightMapPolicyType>::GetShaderBindings(
	const FScene* Scene,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshPassProcessorRenderState& DrawRenderState,
	const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

	if (Scene)
	{
		FRHIUniformBuffer* ReflectionCaptureUniformBuffer = Scene->UniformBuffers.ReflectionCaptureUniformBuffer.GetReference();
		ShaderBindings.Add(ReflectionCaptureBuffer, ReflectionCaptureUniformBuffer);
	}
	else
	{
		ShaderBindings.Add(ReflectionCaptureBuffer, DrawRenderState.GetReflectionCaptureUniformBuffer());
	}

	LightMapPolicyType::GetVertexShaderBindings(
		PrimitiveSceneProxy,
		ShaderElementData.LightMapPolicyElementData,
		this,
		ShaderBindings);
}

template<typename LightMapPolicyType>
void TBasePassVertexShaderPolicyParamType<LightMapPolicyType>::GetElementShaderBindings(
	const FShaderMapPointerTable& PointerTable,
	const FScene* Scene, 
	const FSceneView* ViewIfDynamicMeshCommand, 
	const FVertexFactory* VertexFactory,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMeshBatch& MeshBatch,
	const FMeshBatchElement& BatchElement, 
	const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams) const
{
	FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
}

template<typename LightMapPolicyType>
void TBasePassPixelShaderPolicyParamType<LightMapPolicyType>::GetShaderBindings(
	const FScene* Scene,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material,
	const FMeshPassProcessorRenderState& DrawRenderState,
	const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings) const
{
	FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

	if (Scene)
	{
		FRHIUniformBuffer* ReflectionCaptureUniformBuffer = Scene->UniformBuffers.ReflectionCaptureUniformBuffer.GetReference();
		ShaderBindings.Add(ReflectionCaptureBuffer, ReflectionCaptureUniformBuffer);
	}
	else
	{
		ShaderBindings.Add(ReflectionCaptureBuffer, DrawRenderState.GetReflectionCaptureUniformBuffer());
	}

	LightMapPolicyType::GetPixelShaderBindings(
		PrimitiveSceneProxy,
		ShaderElementData.LightMapPolicyElementData,
		this,
		ShaderBindings);
}