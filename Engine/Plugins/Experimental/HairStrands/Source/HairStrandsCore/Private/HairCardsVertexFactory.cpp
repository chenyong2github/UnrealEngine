// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StrandHairCardsFactory.cpp: hair cards vertex factory implementation
=============================================================================*/

#include "HairCardsVertexFactory.h"
#include "SceneView.h"
#include "MeshBatch.h"
#include "ShaderParameterUtils.h"
#include "Rendering/ColorVertexBuffer.h"
#include "MeshMaterialShader.h"
#include "HairStrandsInterface.h"
#include "GroomInstance.h"

template<typename T> inline void VFC_BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderResourceParameter& Param, T* Value) { if (Param.IsBound() && Value) ShaderBindings.Add(Param, Value); }
template<typename T> inline void VFC_BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderParameter& Param, const T& Value) { if (Param.IsBound()) ShaderBindings.Add(Param, Value); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FHairGroupInstance& GetInput(const FHairCardsVertexFactory* VF, uint32 GroupIndex) { return *VF->Data.Instances[GroupIndex]; };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cards based vertex factory

class FHairCardsVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FHairCardsVertexFactoryShaderParameters, NonVirtual);
public:
	LAYOUT_FIELD(FShaderParameter, Density);
	LAYOUT_FIELD(FShaderResourceParameter, PositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PreviousPositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, NormalsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, UVsBuffer);
	
	LAYOUT_FIELD(FShaderResourceParameter, CardsAtlasRectBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, CardsDimensionBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, DepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, DepthSampler);
	LAYOUT_FIELD(FShaderResourceParameter, TangentTexture);
	LAYOUT_FIELD(FShaderResourceParameter, TangentSampler);
	LAYOUT_FIELD(FShaderResourceParameter, CoverageTexture);
	LAYOUT_FIELD(FShaderResourceParameter, CoverageSampler);
	LAYOUT_FIELD(FShaderResourceParameter, AttributeTexture);
	LAYOUT_FIELD(FShaderResourceParameter, AttributeSampler); 

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		PositionBuffer.Bind(ParameterMap, TEXT("HairCardsVF_PositionBuffer"));
		PreviousPositionBuffer.Bind(ParameterMap, TEXT("HairCardsVF_PreviousPositionBuffer"));
		NormalsBuffer.Bind(ParameterMap, TEXT("HairCardsVF_NormalsBuffer"));
		UVsBuffer.Bind(ParameterMap, TEXT("HairCardsVF_VertexUVsBuffer"));

		CardsAtlasRectBuffer.Bind(ParameterMap, TEXT("HairCardsVF_CardsAtlasRectBuffer"));
		CardsDimensionBuffer.Bind(ParameterMap, TEXT("HairCardsVF_CardsDimensionBuffer"));

		DepthTexture.Bind(ParameterMap, TEXT("HairCardsVF_DepthTexture"));
		DepthSampler.Bind(ParameterMap, TEXT("HairCardsVF_DepthSampler"));
		TangentTexture.Bind(ParameterMap, TEXT("HairCardsVF_TangentTexture"));
		TangentSampler.Bind(ParameterMap, TEXT("HairCardsVF_TangentSampler"));
		CoverageTexture.Bind(ParameterMap, TEXT("HairCardsVF_CoverageTexture"));
		CoverageSampler.Bind(ParameterMap, TEXT("HairCardsVF_CoverageSampler"));
		AttributeTexture.Bind(ParameterMap, TEXT("HairCardsVF_AttributeTexture"));
		AttributeSampler.Bind(ParameterMap, TEXT("HairCardsVF_AttributeSampler"));
	}

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		const FHairCardsVertexFactory* VF = static_cast<const FHairCardsVertexFactory*>(VertexFactory);

		const FHairGroupPublicData* GroupPublicData = reinterpret_cast<const FHairGroupPublicData*>(BatchElement.VertexFactoryUserData);
		check(GroupPublicData);
		const uint32 GroupIndex = GroupPublicData->GetGroupIndex();
		const uint32 LODIndex = GroupPublicData->GetIntLODIndex();
		const FHairGroupInstance& Instance = GetInput(VF, GroupIndex);
		
		if (Instance.GeometryType == EHairGeometryType::Cards)
		{
			const FHairGroupInstance::FCards::FLOD& LOD = Instance.Cards.LODs[LODIndex];
			VFC_BindParam(ShaderBindings, PositionBuffer, LOD.DeformedResource->GetBuffer(FHairCardsDeformedResource::Current).SRV.GetReference());
			VFC_BindParam(ShaderBindings, PreviousPositionBuffer, LOD.DeformedResource->GetBuffer(FHairCardsDeformedResource::Previous).SRV.GetReference());
			VFC_BindParam(ShaderBindings, NormalsBuffer, LOD.RestResource->NormalsBuffer.SRV.GetReference());
			VFC_BindParam(ShaderBindings, UVsBuffer, LOD.RestResource->UVsBuffer.SRV.GetReference());

			//VFC_BindParam(ShaderBindings, CardsAtlasRectBuffer, nullptr);
			//VFC_BindParam(ShaderBindings, CardsDimensionBuffer, nullptr);

			if (LOD.RestResource->CardsDepthTextureRT)
			{
				ShaderBindings.AddTexture(DepthTexture, DepthSampler, LOD.RestResource->DepthSampler, LOD.RestResource->CardsDepthTextureRT->GetRenderTargetItem().TargetableTexture); 
			}

			if (LOD.RestResource->CardsTangentTextureRT)
			{
				ShaderBindings.AddTexture(TangentTexture, TangentSampler, LOD.RestResource->TangentSampler, LOD.RestResource->CardsTangentTextureRT->GetRenderTargetItem().TargetableTexture);
			}

			if (LOD.RestResource->CardsCoverageTextureRT)
			{
				ShaderBindings.AddTexture(CoverageTexture, CoverageSampler, LOD.RestResource->CoverageSampler, LOD.RestResource->CardsCoverageTextureRT->GetRenderTargetItem().TargetableTexture);
			}

			if (LOD.RestResource->CardsAttributeTextureRT)
			{
				ShaderBindings.AddTexture(AttributeTexture, AttributeSampler, LOD.RestResource->AttributeSampler, LOD.RestResource->CardsAttributeTextureRT->GetRenderTargetItem().TargetableTexture);
			}
		}
		else if (Instance.GeometryType == EHairGeometryType::Meshes)
		{
			const FHairGroupInstance::FMeshes::FLOD& LOD = Instance.Meshes.LODs[LODIndex];
			VFC_BindParam(ShaderBindings, PositionBuffer, LOD.RestResource->PositionBuffer.SRV.GetReference());
			VFC_BindParam(ShaderBindings, PreviousPositionBuffer, LOD.RestResource->PositionBuffer.SRV.GetReference());
			VFC_BindParam(ShaderBindings, NormalsBuffer, LOD.RestResource->NormalsBuffer.SRV.GetReference());
			VFC_BindParam(ShaderBindings, UVsBuffer, LOD.RestResource->UVsBuffer.SRV.GetReference());
		}
	}
};

IMPLEMENT_TYPE_LAYOUT(FHairCardsVertexFactoryShaderParameters);

/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FHairCardsVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithHairStrands && Parameters.Platform == EShaderPlatform::SP_PCD3D_SM5) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FHairCardsVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	const bool bUseGPUSceneAndPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream() && UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform));
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bUseGPUSceneAndPrimitiveIdStream);
	OutEnvironment.SetDefine(TEXT("VF_CARDS_HAIR"), TEXT("1"));
	OutEnvironment.SetDefine(TEXT("VF_GPU_SCENE_TEXTURE"), bUseGPUSceneAndPrimitiveIdStream && GPUSceneUseTexture2D(Parameters.Platform));
}

void FHairCardsVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
	if (Type->SupportsPrimitiveIdStream() 
		&& UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform)) 
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::StaticStructMetadata.GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(PrimitiveId).Member instead of Primitive.Member."), Type->GetName()));
	}
}

void FHairCardsVertexFactory::SetData(const FDataType& InData)
{
	check(IsInRenderingThread());
	Data = InData;
	UpdateRHI();
}

/**
* Copy the data from another vertex factory
* @param Other - factory to copy from
*/
void FHairCardsVertexFactory::Copy(const FHairCardsVertexFactory& Other)
{
	FHairCardsVertexFactory* VertexFactory = this;
	const FDataType* DataCopy = &Other.Data;
	ENQUEUE_RENDER_COMMAND(FHairStrandsVertexFactoryCopyData)(
		[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->Data = *DataCopy;
		});
	BeginUpdateResourceRHI(this);
}

void FHairCardsVertexFactory::InitRHI()
{
	bNeedsDeclaration = false;
	bSupportsManualVertexFetch = true;

	// We create different streams based on feature level
	check(HasValidFeatureLevel());

	// VertexFactory needs to be able to support max possible shader platform and feature level
	// in case if we switch feature level at runtime.
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel);

	FVertexDeclarationElementList Elements;
	SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, -1);
	if (GetType()->SupportsPrimitiveIdStream() && bCanUseGPUScene)
	{
		// When the VF is used for rendering in normal mesh passes, this vertex buffer and offset will be overridden
		Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GPrimitiveIdDummy, 0, 0, sizeof(uint32), VET_UInt, EVertexStreamUsage::Instancing), 13));
		SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, Elements.Last().StreamIndex);
		bNeedsDeclaration = true;
	}

	check(Streams.Num() > 0);

	InitDeclaration(Elements);
	check(IsValidRef(GetDeclaration()));
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairCardsVertexFactory, SF_Vertex,		FHairCardsVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairCardsVertexFactory, SF_Pixel,		FHairCardsVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairCardsVertexFactory, SF_Compute,	FHairCardsVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairCardsVertexFactory, SF_RayHitGroup,	FHairCardsVertexFactoryShaderParameters);
#endif

void FHairCardsVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
}

IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FHairCardsVertexFactory,"/Engine/Private/HairStrands/HairCardsVertexFactory.ush",true,false,true,true,true,true,true,false);
