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
#include "SystemTextures.h" 

template<typename T> inline void VFC_BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderResourceParameter& Param, T* Value) { if (Param.IsBound() && Value) ShaderBindings.Add(Param, Value); }
template<typename T> inline void VFC_BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderParameter& Param, const T& Value) { if (Param.IsBound()) ShaderBindings.Add(Param, Value); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline FHairGroupInstance& GetInput(const FHairCardsVertexFactory* VF, uint32 GroupIndex) { return *VF->Data.Instances[GroupIndex]; };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cards based vertex factory
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHairCardsVertexFactoryUniformShaderParameters, "HairCardsVF");

FHairCardsUniformBuffer CreateHairCardsVFUniformBuffer(
	uint32 Current,
	const FHairGroupInstance* Instance,
	const uint32 LODIndex,
	EHairGeometryType GeometryType)
{
	FHairCardsVertexFactoryUniformShaderParameters UniformParameters;

	// #hair_todo: add non-manual vertex fetching support
	// if (RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
	// else
	//  Set default value to unused buffer

	if (GeometryType == EHairGeometryType::Cards)
	{
		const FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[LODIndex];

		// Cards atlas UV are inverted so fetching needs to be inverted on the y-axis
		UniformParameters.bInvertUV = 1;
		UniformParameters.PositionBuffer = LOD.DeformedResource->GetBuffer(Current == 0 ? FHairCardsDeformedResource::Current : FHairCardsDeformedResource::Previous).SRV.GetReference();
		UniformParameters.PreviousPositionBuffer = LOD.DeformedResource->GetBuffer(Current == 0 ? FHairCardsDeformedResource::Previous : FHairCardsDeformedResource::Current).SRV.GetReference();
		UniformParameters.NormalsBuffer = LOD.RestResource->NormalsBuffer.SRV.GetReference();
		UniformParameters.UVsBuffer = LOD.RestResource->UVsBuffer.SRV.GetReference();

		UniformParameters.DepthTexture = LOD.RestResource->DepthTexture;
		UniformParameters.DepthSampler = LOD.RestResource->DepthSampler;
		UniformParameters.TangentTexture = LOD.RestResource->TangentTexture;
		UniformParameters.TangentSampler = LOD.RestResource->TangentSampler;
		UniformParameters.CoverageTexture = LOD.RestResource->CoverageTexture;
		UniformParameters.CoverageSampler = LOD.RestResource->CoverageSampler;
		UniformParameters.AttributeTexture = LOD.RestResource->AttributeTexture;
		UniformParameters.AttributeSampler = LOD.RestResource->AttributeSampler;
	}
	else if (GeometryType == EHairGeometryType::Meshes)
	{
		const FHairGroupInstance::FMeshes::FLOD& LOD = Instance->Meshes.LODs[LODIndex];

		// Meshes UV are not inverted so no need to invert the y-axis
		UniformParameters.bInvertUV = 0;
		UniformParameters.PositionBuffer = LOD.DeformedResource->GetBuffer(Current == 0 ? FHairMeshesDeformedResource::Current : FHairMeshesDeformedResource::Previous).SRV.GetReference();
		UniformParameters.PreviousPositionBuffer = LOD.DeformedResource->GetBuffer(Current == 0 ? FHairMeshesDeformedResource::Previous : FHairMeshesDeformedResource::Current).SRV.GetReference();
		UniformParameters.NormalsBuffer = LOD.RestResource->NormalsBuffer.SRV.GetReference();
		UniformParameters.UVsBuffer = LOD.RestResource->UVsBuffer.SRV.GetReference();

		UniformParameters.DepthTexture = LOD.RestResource->DepthTexture;
		UniformParameters.DepthSampler = LOD.RestResource->DepthSampler;
		UniformParameters.TangentTexture = LOD.RestResource->TangentTexture;
		UniformParameters.TangentSampler = LOD.RestResource->TangentSampler;
		UniformParameters.CoverageTexture = LOD.RestResource->CoverageTexture;
		UniformParameters.CoverageSampler = LOD.RestResource->CoverageSampler;
		UniformParameters.AttributeTexture = LOD.RestResource->AttributeTexture;
		UniformParameters.AttributeSampler = LOD.RestResource->AttributeSampler;
	}

	FRHITexture* DefaultTexture = GSystemTextures.BlackDummy->GetShaderResourceRHI();
	FSamplerStateRHIRef DefaultSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (!UniformParameters.DepthTexture)		{ UniformParameters.DepthTexture = DefaultTexture;	  }
	if (!UniformParameters.TangentTexture)		{ UniformParameters.TangentTexture = DefaultTexture;  }
	if (!UniformParameters.CoverageTexture)		{ UniformParameters.CoverageTexture = DefaultTexture; }
	if (!UniformParameters.AttributeTexture)	{ UniformParameters.AttributeTexture = DefaultTexture;}

	if (!UniformParameters.DepthSampler)		{ UniformParameters.DepthSampler = DefaultSampler;	  }
	if (!UniformParameters.TangentSampler)		{ UniformParameters.TangentSampler = DefaultSampler;  }
	if (!UniformParameters.CoverageSampler)		{ UniformParameters.CoverageSampler = DefaultSampler; }
	if (!UniformParameters.AttributeSampler)	{ UniformParameters.AttributeSampler = DefaultSampler;}

	//return TUniformBufferRef<FHairCardsVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_MultiFrame);
	return TUniformBufferRef<FHairCardsVertexFactoryUniformShaderParameters>::CreateUniformBufferImmediate(UniformParameters, UniformBuffer_MultiFrame);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cards based vertex factory

class FHairCardsVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FHairCardsVertexFactoryShaderParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
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
		
		// Decode VertexFactoryUserData as VertexFactoryUniformBuffer
		FRHIUniformBuffer* VertexFactoryUniformBuffer = nullptr;
		check(Instance.GeometryType == EHairGeometryType::Cards || Instance.GeometryType == EHairGeometryType::Meshes);
		if (Instance.GeometryType == EHairGeometryType::Cards)
		{
			const FHairGroupInstance::FCards::FLOD& LOD = Instance.Cards.LODs[LODIndex];
			check(LOD.UniformBuffer);
			const uint32 UniformIndex = LOD.DeformedResource->GetIndex(FHairCardsDeformedResource::Current);
			VertexFactoryUniformBuffer = LOD.UniformBuffer[UniformIndex];
		}
		else if (Instance.GeometryType == EHairGeometryType::Meshes)
		{
			const FHairGroupInstance::FMeshes::FLOD& LOD = Instance.Meshes.LODs[LODIndex];
			check(LOD.UniformBuffer);
			const uint32 UniformIndex = LOD.DeformedResource->GetIndex(FHairMeshesDeformedResource::Current);
			VertexFactoryUniformBuffer = LOD.UniformBuffer[UniformIndex];
		}

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FHairCardsVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
	}
};

IMPLEMENT_TYPE_LAYOUT(FHairCardsVertexFactoryShaderParameters);

/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FHairCardsVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithHairStrands && IsHairStrandsSupported(EHairStrandsShaderType::Cards, Parameters.Platform)) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
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

	// Pre-allocate all the uniform buffers for all the LODs (and current/previous swap-option)
	for (int32 GroupIt = 0, GroupCount=Data.Instances.Num(); GroupIt < GroupCount; GroupIt++)
	{
		FHairGroupInstance* HairInstance = Data.Instances[GroupIt];
		check(HairInstance->HairGroupPublicData);

		// Material - Cards
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards, GMaxRHIShaderPlatform))
		{
			uint32 CardsLODIndex = 0;
			for (const FHairGroupInstance::FCards::FLOD& LOD : HairInstance->Cards.LODs)
			{
				if (LOD.IsValid())
				{
					HairInstance->Cards.LODs[CardsLODIndex].UniformBuffer[0] = CreateHairCardsVFUniformBuffer(0, HairInstance, CardsLODIndex, EHairGeometryType::Cards);
					HairInstance->Cards.LODs[CardsLODIndex].UniformBuffer[1] = CreateHairCardsVFUniformBuffer(1, HairInstance, CardsLODIndex, EHairGeometryType::Cards);
				}
				++CardsLODIndex;
			}
		}

		// Material - Meshes
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, GMaxRHIShaderPlatform))
		{
			uint32 MeshesLODIndex = 0;
			for (const FHairGroupInstance::FMeshes::FLOD& LOD : HairInstance->Meshes.LODs)
			{
				if (LOD.IsValid())
				{
					HairInstance->Meshes.LODs[MeshesLODIndex].UniformBuffer[0] = CreateHairCardsVFUniformBuffer(0, HairInstance, MeshesLODIndex, EHairGeometryType::Meshes);
					HairInstance->Meshes.LODs[MeshesLODIndex].UniformBuffer[1] = CreateHairCardsVFUniformBuffer(1, HairInstance, MeshesLODIndex, EHairGeometryType::Meshes);
				}
				++MeshesLODIndex;
			}
		}
	}
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
