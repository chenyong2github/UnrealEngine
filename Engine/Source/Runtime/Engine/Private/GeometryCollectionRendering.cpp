// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollectionRendering.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

IMPLEMENT_TYPE_LAYOUT(FGeometryCollectionVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGeometryCollectionVertexFactory, SF_Vertex, FGeometryCollectionVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FGeometryCollectionVertexFactory, "/Engine/Private/LocalVertexFactory.ush", true, true, true, true, true);

bool FGeometryCollectionVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.bIsUsedWithGeometryCollections || Parameters.MaterialParameters.bIsSpecialEngineMaterial)
		&& FLocalVertexFactory::ShouldCompilePermutation(Parameters);
}

//
// Modify compile environment to enable instancing
// @param OutEnvironment - shader compile environment to modify
//
void FGeometryCollectionVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	const bool ContainsManualVertexFetch = OutEnvironment.GetDefinitions().Contains("MANUAL_VERTEX_FETCH");
	if (!ContainsManualVertexFetch && RHISupportsManualVertexFetch(Parameters.Platform))
	{
		OutEnvironment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), TEXT("1"));
	}

	if (RHISupportsManualVertexFetch(Parameters.Platform))
	{
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING"), TEXT("1"));

		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_BONEMAP"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FOR_INSTANCED"), TEXT("0"));
	}

	// Geometry collections use a custom hit proxy per bone
	OutEnvironment.SetDefine(TEXT("USE_PER_VERTEX_HITPROXY_ID"), 1);

	FLocalVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

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