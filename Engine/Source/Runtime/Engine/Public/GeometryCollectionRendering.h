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
public:

	// #note: We are reusing VertexFetch_InstanceTransformBuffer that was created for instanced static mesh 
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		FLocalVertexFactoryShaderParameters::Bind(ParameterMap);

		VertexFetch_InstanceTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceTransformBuffer"));
		VertexFetch_InstancePrevTransformBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstancePrevTransformBuffer"));
		VertexFetch_InstanceBoneMapBufferParameter.Bind(ParameterMap, TEXT("VertexFetch_InstanceBoneMapBuffer"));
	}	

	virtual void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const override;

	void Serialize(FArchive& Ar) override
	{
		FLocalVertexFactoryShaderParameters::Serialize(Ar);
		Ar << VertexFetch_InstanceTransformBufferParameter;
		Ar << VertexFetch_InstancePrevTransformBufferParameter;
		Ar << VertexFetch_InstanceBoneMapBufferParameter;
	}

	virtual uint32 GetSize() const override { return sizeof(*this); }

private:
	FShaderResourceParameter VertexFetch_InstanceTransformBufferParameter;
	FShaderResourceParameter VertexFetch_InstancePrevTransformBufferParameter;
	FShaderResourceParameter VertexFetch_InstanceBoneMapBufferParameter;
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
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType) 
	{
		return (Material->IsUsedWithGeometryCollections() || Material->IsSpecialEngineMaterial())
			&& FLocalVertexFactory::ShouldCompilePermutation(Platform, Material, ShaderType);
	}

	//
	// Modify compile environment to enable instancing
	// @param OutEnvironment - shader compile environment to modify
	//
	static void ModifyCompilationEnvironment(const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		const bool ContainsManualVertexFetch = OutEnvironment.GetDefinitions().Contains("MANUAL_VERTEX_FETCH");
		if (!ContainsManualVertexFetch && RHISupportsManualVertexFetch(Platform))
		{
			OutEnvironment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), TEXT("1"));
		}

		if (RHISupportsManualVertexFetch(Platform))
		{
			OutEnvironment.SetDefine(TEXT("USE_INSTANCING"), TEXT("1"));
			OutEnvironment.SetDefine(TEXT("USE_INSTANCING_EMULATED"), TEXT("0"));
		
			OutEnvironment.SetDefine(TEXT("USE_INSTANCING_BONEMAP"), TEXT("1"));
			OutEnvironment.SetDefine(TEXT("USE_DITHERED_LOD_TRANSITION_FOR_INSTANCED"), TEXT("0"));
		}

		// Geometry collections use a custom hit proxy per bone
		OutEnvironment.SetDefine(TEXT("USE_PER_VERTEX_HITPROXY_ID"), 1);

		FLocalVertexFactory::ModifyCompilationEnvironment(Type, Platform, Material, OutEnvironment);
	}

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

	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency)
	{
		return ShaderFrequency == SF_Vertex ? new FGeometryCollectionVertexFactoryShaderParameters() : NULL;
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
