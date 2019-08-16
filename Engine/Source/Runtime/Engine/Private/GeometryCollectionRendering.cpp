// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollectionRendering.h"
#include "MeshDrawShaderBindings.h"

IMPLEMENT_VERTEX_FACTORY_TYPE(FGeometryCollectionVertexFactory, "/Engine/Private/LocalVertexFactory.ush", true, true, true, true, true);

void FGeometryCollectionVertexFactoryShaderParameters::GetElementShaderBindings(
	const FSceneInterface* Scene,
	const FSceneView* View,
	const FMeshMaterialShader* Shader,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactory* VertexFactory,
	const FMeshBatchElement& BatchElement,
	class FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams) const
{
	check(VertexFactory->GetType() == &FGeometryCollectionVertexFactory::StaticType);


	const auto* LocalVertexFactory = static_cast<const FGeometryCollectionVertexFactory*>(VertexFactory);
	FRHIUniformBuffer* VertexFactoryUniformBuffer = nullptr;
	VertexFactoryUniformBuffer = LocalVertexFactory->GetUniformBuffer();


	FLocalVertexFactoryShaderParametersBase::GetElementShaderBindingsBase(Scene, View, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, VertexFactoryUniformBuffer, ShaderBindings, VertexStreams);


	// We only want to set the SRV parameters if we support manual vertex fetch.
	if (LocalVertexFactory->SupportsManualVertexFetch(View->GetFeatureLevel()))
	{
		ShaderBindings.Add(VertexFetch_InstanceTransformBufferParameter, LocalVertexFactory->GetInstanceTransformSRV());
		ShaderBindings.Add(VertexFetch_InstancePrevTransformBufferParameter, LocalVertexFactory->GetInstancePrevTransformSRV());
		ShaderBindings.Add(VertexFetch_InstanceBoneMapBufferParameter, LocalVertexFactory->GetInstanceBoneMapSRV());
	}
}