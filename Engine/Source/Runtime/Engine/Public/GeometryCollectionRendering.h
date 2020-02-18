// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "ShaderParameterUtils.h"

class FGeometryCollectionVertexFactoryShaderParameters : public FLocalVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FGeometryCollectionVertexFactoryShaderParameters, NonVirtual);
public:

	// #note: We are reusing VertexFetch_InstanceTransformBuffer that was created for instanced static mesh 
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		FLocalVertexFactoryShaderParameters::Bind(ParameterMap);

		VertexFetch_InstanceTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceTransformBuffer"));
		VertexFetch_InstancePrevTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstancePrevTransformBuffer"));
		VertexFetch_InstanceBoneMapBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceBoneMapBuffer"));
	}	

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;

private:
	
		LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceTransformBufferParameter)
		LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstancePrevTransformBufferParameter)
		LAYOUT_FIELD(FShaderResourceParameter, VertexFetch_InstanceBoneMapBufferParameter)
	
};

/**
 * A vertex factory for Geometry Collections
 */
struct ENGINE_API FGeometryCollectionVertexFactory : public FLocalVertexFactory
{
    DECLARE_VERTEX_FACTORY_TYPE(FGeometryCollectionVertexFactory);
public:
	FGeometryCollectionVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FLocalVertexFactory(InFeatureLevel, "FGeometryCollectionVertexFactory")
	{
	}

	// Data includes what we need for transform and everything in local vertex factory too
	struct FDataType : public FLocalVertexFactory::FDataType
	{
		FRHIShaderResourceView* InstanceTransformSRV = nullptr;
		FRHIShaderResourceView* InstancePrevTransformSRV = nullptr;
		FRHIShaderResourceView* InstanceBoneMapSRV = nullptr;
	};

	//
	// Permutations are controlled by the material flag
	//
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	//
	// Modify compile environment to enable instancing
	// @param OutEnvironment - shader compile environment to modify
	//
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	//
	// Set the data on the vertex factory
	//
	void SetData(const FDataType& InData)
	{
		FLocalVertexFactory::Data = InData;
		Data = InData;
		UpdateRHI();
	}

	//
	// Copy the data from another vertex factory
	// @param Other - factory to copy from
	//
	void Copy(const FGeometryCollectionVertexFactory& Other)
	{
		FGeometryCollectionVertexFactory* VertexFactory = this;
		const FDataType* DataCopy = &Other.Data;
		ENQUEUE_RENDER_COMMAND(FGeometryCollectionVertexFactoryCopyData)(
			[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->Data = *DataCopy;
		});
		BeginUpdateResourceRHI(this);
	}

	// FRenderResource interface.	
	virtual void InitRHI() override
	{
		FLocalVertexFactory::InitRHI();
	}

	inline void SetInstanceTransformSRV(FRHIShaderResourceView* InstanceTransformSRV)
	{
		Data.InstanceTransformSRV = InstanceTransformSRV;
	}
	
	inline FRHIShaderResourceView* GetInstanceTransformSRV() const
	{
		return Data.InstanceTransformSRV;
	}

	inline void SetInstancePrevTransformSRV(FRHIShaderResourceView* InstancePrevTransformSRV)
	{
		Data.InstancePrevTransformSRV = InstancePrevTransformSRV;
	}

	inline FRHIShaderResourceView* GetInstancePrevTransformSRV() const
	{
		return Data.InstancePrevTransformSRV;
	}

	inline FRHIShaderResourceView* GetInstanceBoneMapSRV() const
	{
		return Data.InstanceBoneMapSRV;
	}
	
private:
	FDataType Data;
};
