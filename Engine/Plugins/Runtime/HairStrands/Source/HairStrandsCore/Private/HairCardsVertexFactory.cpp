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
// Cards based vertex factory
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHairCardsVertexFactoryUniformShaderParameters, "HairCardsVF");

FHairCardsUniformBuffer CreateHairCardsVFUniformBuffer(
	const FHairGroupInstance* Instance,
	const uint32 LODIndex,
	EHairGeometryType GeometryType, 
	bool bSupportsManualVertexFetch)
{
	FHairCardsVertexFactoryUniformShaderParameters UniformParameters;

	if (GeometryType == EHairGeometryType::Cards)
	{
		const FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[LODIndex];

		// Cards atlas UV are inverted so fetching needs to be inverted on the y-axis
		UniformParameters.bInvertUV = LOD.RestResource->bInvertUV;
		UniformParameters.bUseTextureRootUV = 0;
		UniformParameters.bUseTextureGroupIndex = 0;
		UniformParameters.MaxVertexCount = LOD.RestResource->GetVertexCount();

		// When the geometry is not-dynamic (no binding to skeletal mesh, no simulation), only a single vertex buffer is allocated. 
		// In this case we force the buffer index to 0
		const bool bIsDynamic = LOD.DeformedResource != nullptr;
		if (bIsDynamic)
		{
			UniformParameters.PositionBuffer = LOD.DeformedResource->GetBuffer(FHairCardsDeformedResource::EFrameType::Current).SRV.GetReference();
			UniformParameters.PreviousPositionBuffer = LOD.DeformedResource->GetBuffer(FHairCardsDeformedResource::EFrameType::Previous).SRV.GetReference();
			UniformParameters.NormalsBuffer = LOD.DeformedResource->DeformedNormalBuffer.SRV.GetReference();
		}
		else
		{
			UniformParameters.PositionBuffer = LOD.RestResource->RestPositionBuffer.ShaderResourceViewRHI;
			UniformParameters.PreviousPositionBuffer = LOD.RestResource->RestPositionBuffer.ShaderResourceViewRHI;
			UniformParameters.NormalsBuffer = LOD.RestResource->NormalsBuffer.ShaderResourceViewRHI.GetReference();
		}

		UniformParameters.UVsBuffer = LOD.RestResource->UVsBuffer.ShaderResourceViewRHI.GetReference();

		UniformParameters.DepthTexture = LOD.RestResource->DepthTexture;
		UniformParameters.DepthSampler = LOD.RestResource->DepthSampler;
		UniformParameters.TangentTexture = LOD.RestResource->TangentTexture;
		UniformParameters.TangentSampler = LOD.RestResource->TangentSampler;
		UniformParameters.CoverageTexture = LOD.RestResource->CoverageTexture;
		UniformParameters.CoverageSampler = LOD.RestResource->CoverageSampler;
		UniformParameters.AttributeTexture = LOD.RestResource->AttributeTexture;
		UniformParameters.AttributeSampler = LOD.RestResource->AttributeSampler;
		UniformParameters.AuxilaryDataTexture = LOD.RestResource->AuxilaryDataTexture;
		UniformParameters.AuxilaryDataSampler = LOD.RestResource->AuxilaryDataSampler;
		UniformParameters.GroupIndexTexture = nullptr; // Group index are stored on vertices for cards
		UniformParameters.GroupIndexSampler = nullptr; // Group index are stored on vertices for cards
	}
	else if (GeometryType == EHairGeometryType::Meshes)
	{
		const FHairGroupInstance::FMeshes::FLOD& LOD = Instance->Meshes.LODs[LODIndex];

		// When the geometry is not-dynamic (no binding to skeletal mesh, no simulation), only a single vertex buffer is allocated. 
		// In this case we force the buffer index to 0
		const bool bIsDynamic = LOD.DeformedResource != nullptr;
		if (bIsDynamic)
		{
			UniformParameters.PositionBuffer = LOD.DeformedResource->GetBuffer(FHairMeshesDeformedResource::EFrameType::Current).SRV.GetReference();
			UniformParameters.PreviousPositionBuffer = LOD.DeformedResource->GetBuffer(FHairMeshesDeformedResource::EFrameType::Previous).SRV.GetReference();
		}
		else
		{
			UniformParameters.PositionBuffer = LOD.RestResource->RestPositionBuffer.ShaderResourceViewRHI;
			UniformParameters.PreviousPositionBuffer = LOD.RestResource->RestPositionBuffer.ShaderResourceViewRHI;
		}

		// Meshes UV are not inverted so no need to invert the y-axis
		UniformParameters.bInvertUV = 0;
		UniformParameters.bUseTextureRootUV = 1;
		UniformParameters.bUseTextureGroupIndex = 1;
		UniformParameters.MaxVertexCount = LOD.RestResource->GetVertexCount();
		UniformParameters.NormalsBuffer = LOD.RestResource->NormalsBuffer.ShaderResourceViewRHI.GetReference();
		UniformParameters.UVsBuffer = LOD.RestResource->UVsBuffer.ShaderResourceViewRHI.GetReference();

		UniformParameters.DepthTexture = LOD.RestResource->DepthTexture;
		UniformParameters.DepthSampler = LOD.RestResource->DepthSampler;
		UniformParameters.TangentTexture = LOD.RestResource->TangentTexture;
		UniformParameters.TangentSampler = LOD.RestResource->TangentSampler;
		UniformParameters.CoverageTexture = LOD.RestResource->CoverageTexture;
		UniformParameters.CoverageSampler = LOD.RestResource->CoverageSampler;
		UniformParameters.AttributeTexture = LOD.RestResource->AttributeTexture;
		UniformParameters.AttributeSampler = LOD.RestResource->AttributeSampler;
		UniformParameters.AuxilaryDataTexture = LOD.RestResource->AuxilaryDataTexture;
		UniformParameters.AuxilaryDataSampler = LOD.RestResource->AuxilaryDataSampler;
		UniformParameters.GroupIndexTexture = LOD.RestResource->GroupIndexTexture;
		UniformParameters.GroupIndexSampler = LOD.RestResource->GroupIndexSampler;
	}

	if (!bSupportsManualVertexFetch)
	{
		UniformParameters.PositionBuffer = GNullVertexBuffer.VertexBufferSRV;
		UniformParameters.PreviousPositionBuffer = GNullVertexBuffer.VertexBufferSRV;
		UniformParameters.NormalsBuffer = GNullVertexBuffer.VertexBufferSRV;
		UniformParameters.UVsBuffer = GNullVertexBuffer.VertexBufferSRV;
	}

	FRHITexture* DefaultTexture = GBlackTexture->TextureRHI;
	FSamplerStateRHIRef DefaultSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (!UniformParameters.DepthTexture)		{ UniformParameters.DepthTexture = DefaultTexture;	  }
	if (!UniformParameters.TangentTexture)		{ UniformParameters.TangentTexture = DefaultTexture;  }
	if (!UniformParameters.CoverageTexture)		{ UniformParameters.CoverageTexture = DefaultTexture; }
	if (!UniformParameters.AttributeTexture)	{ UniformParameters.AttributeTexture = DefaultTexture;}
	if (!UniformParameters.AuxilaryDataTexture)	{ UniformParameters.AuxilaryDataTexture = DefaultTexture; }
	if (!UniformParameters.GroupIndexTexture)	{ UniformParameters.GroupIndexTexture = DefaultTexture; }

	if (!UniformParameters.DepthSampler)		{ UniformParameters.DepthSampler = DefaultSampler;	  }
	if (!UniformParameters.TangentSampler)		{ UniformParameters.TangentSampler = DefaultSampler;  }
	if (!UniformParameters.CoverageSampler)		{ UniformParameters.CoverageSampler = DefaultSampler; }
	if (!UniformParameters.AttributeSampler)	{ UniformParameters.AttributeSampler = DefaultSampler;}
	if (!UniformParameters.AuxilaryDataSampler) { UniformParameters.AuxilaryDataSampler = DefaultSampler; }
	if (!UniformParameters.GroupIndexSampler)	{ UniformParameters.GroupIndexSampler = DefaultSampler;}

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
		check(VF);

		const int32 LODIndex = VF->Data.LODIndex;

		const FHairGroupInstance* Instance = VF->Data.Instance;
		const EHairGeometryType InstanceGeometryType = VF->Data.GeometryType;
		check(Instance);
		check(LODIndex >= 0);

		// Decode VertexFactoryUserData as VertexFactoryUniformBuffer
		FRHIUniformBuffer* VertexFactoryUniformBuffer = nullptr;
		check(InstanceGeometryType == EHairGeometryType::Cards || InstanceGeometryType == EHairGeometryType::Meshes);
		if (InstanceGeometryType == EHairGeometryType::Cards)
		{
			const FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[LODIndex];
			check(LOD.UniformBuffer);
			VertexFactoryUniformBuffer = LOD.UniformBuffer;
		}
		else if (InstanceGeometryType == EHairGeometryType::Meshes)
		{
			const FHairGroupInstance::FMeshes::FLOD& LOD = Instance->Meshes.LODs[LODIndex];
			check(LOD.UniformBuffer);
			VertexFactoryUniformBuffer = LOD.UniformBuffer;
		}
		
		check(VertexFactoryUniformBuffer);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FHairCardsVertexFactoryUniformShaderParameters>(), VertexFactoryUniformBuffer);
	}
};

IMPLEMENT_TYPE_LAYOUT(FHairCardsVertexFactoryShaderParameters);

FHairCardsVertexFactory::FHairCardsVertexFactory(FHairGroupInstance* Instance, uint32 LODIndex, EHairGeometryType GeometryType, EShaderPlatform InShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel, const char* InDebugName)
	: FVertexFactory(InFeatureLevel)
	, DebugName(InDebugName)
{
	bSupportsManualVertexFetch = RHISupportsManualVertexFetch(InShaderPlatform);

	Data.Instance = Instance;
	Data.LODIndex = LODIndex;
	Data.GeometryType = GeometryType;
}
/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FHairCardsVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithHairStrands && IsHairStrandsSupported(EHairStrandsShaderType::Cards, Parameters.Platform)) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FHairCardsVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	const bool bUseGPUSceneAndPrimitiveIdStream = Parameters.VertexFactoryType->SupportsPrimitiveIdStream() && UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform));
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bUseGPUSceneAndPrimitiveIdStream);
	OutEnvironment.SetDefine(TEXT("HAIR_CARD_MESH_FACTORY"), TEXT("1"));
	OutEnvironment.SetDefine(TEXT("MANUAL_VERTEX_FETCH"), RHISupportsManualVertexFetch(Parameters.Platform) ? TEXT("1") : TEXT("0"));	
}

void FHairCardsVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
	// This is not relevant to hair cards at the moment, as instancing is not supported anyway (need to bind unique resource per card instance
	#if 0
	if (Type->SupportsPrimitiveIdStream() 
		&& UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform)) 
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::StaticStructMetadata.GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(PrimitiveId).Member instead of Primitive.Member."), Type->GetName()));
	}
	#endif
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

void FHairCardsVertexFactory::InitResources()
{
	if (bIsInitialized)
		return;

	FVertexFactory::InitResource(); //Call VertexFactory/RenderResources::InitResource() to mark the resource as initialized();

	bIsInitialized = true;
	bNeedsDeclaration = true;
	// We create different streams based on feature level
	check(HasValidFeatureLevel());

	// If the platform does not support manual vertex fetching we assume it is a low end platform, and so we don't enable deformation.
	// VertexFactory needs to be able to support max possible shader platform and feature level
	// in case if we switch feature level at runtime.
	FVertexDeclarationElementList Elements;
	SetPrimitiveIdStreamIndex(GetFeatureLevel(), EVertexInputStreamType::Default, -1);
	AddPrimitiveIdStreamElement(EVertexInputStreamType::Default, Elements, 13, 0xff);

	// Note this is a local version of the VF's bSupportsManualVertexFetch, which take into account the feature level
	const bool bManualFetch = SupportsManualVertexFetch(GetFeatureLevel());
	if (!bManualFetch)
	{
		if (Data.GeometryType == EHairGeometryType::Cards)
		{
			const FHairGroupInstance::FCards::FLOD& LOD = Data.Instance->Cards.LODs[Data.LODIndex];
		
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->RestPositionBuffer,	0, 0,									FHairCardsPositionFormat::SizeInByte, FHairCardsPositionFormat::VertexElementType,	EVertexStreamUsage::Default), 0));
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->NormalsBuffer,			0, 0,									FHairCardsNormalFormat::SizeInByte * FHairCardsNormalFormat::ComponentCount,	FHairCardsNormalFormat::VertexElementType,		EVertexStreamUsage::Default), 1));
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->NormalsBuffer,			0, FHairCardsNormalFormat::SizeInByte,	FHairCardsNormalFormat::SizeInByte * FHairCardsNormalFormat::ComponentCount,	FHairCardsNormalFormat::VertexElementType,		EVertexStreamUsage::Default), 2));
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->UVsBuffer,				0, 0,									FHairCardsUVFormat::SizeInByte,		FHairCardsUVFormat::VertexElementType,			EVertexStreamUsage::Default), 3));
		}
		else if (Data.GeometryType == EHairGeometryType::Meshes)
		{
			const FHairGroupInstance::FMeshes::FLOD& LOD = Data.Instance->Meshes.LODs[Data.LODIndex];

			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->RestPositionBuffer,	0, 0,									FHairCardsPositionFormat::SizeInByte, FHairCardsPositionFormat::VertexElementType,	EVertexStreamUsage::Default), 0));
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->NormalsBuffer,			0, 0,									FHairCardsNormalFormat::SizeInByte * FHairCardsNormalFormat::ComponentCount,	FHairCardsNormalFormat::VertexElementType,		EVertexStreamUsage::Default), 1));
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->NormalsBuffer,			0, FHairCardsNormalFormat::SizeInByte,	FHairCardsNormalFormat::SizeInByte * FHairCardsNormalFormat::ComponentCount,	FHairCardsNormalFormat::VertexElementType,		EVertexStreamUsage::Default), 2));
			Elements.Add(AccessStreamComponent(FVertexStreamComponent(&LOD.RestResource->UVsBuffer,				0, 0,									FHairCardsUVFormat::SizeInByte,		FHairCardsUVFormat::VertexElementType,			EVertexStreamUsage::Default), 3));
		}

		bNeedsDeclaration = true;
		check(Streams.Num() > 0);
	}
	InitDeclaration(Elements, EVertexInputStreamType::Default);
	check(IsValidRef(GetDeclaration()));

	FHairGroupInstance* HairInstance = Data.Instance;
	check(HairInstance->HairGroupPublicData);

	if (Data.GeometryType == EHairGeometryType::Cards && IsHairStrandsEnabled(EHairStrandsShaderType::Cards, GMaxRHIShaderPlatform))
	{
		if (HairInstance->Cards.LODs.IsValidIndex(Data.LODIndex))
		{
			const FHairGroupInstance::FCards::FLOD& LOD = HairInstance->Cards.LODs[Data.LODIndex];
			HairInstance->Cards.LODs[Data.LODIndex].UniformBuffer = CreateHairCardsVFUniformBuffer(HairInstance, Data.LODIndex, EHairGeometryType::Cards, bManualFetch);
		}
	}
	else if (Data.GeometryType == EHairGeometryType::Meshes && IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, GMaxRHIShaderPlatform))
	{
		if (HairInstance->Meshes.LODs.IsValidIndex(Data.LODIndex))
		{
			const FHairGroupInstance::FMeshes::FLOD& LOD = HairInstance->Meshes.LODs[Data.LODIndex];
			HairInstance->Meshes.LODs[Data.LODIndex].UniformBuffer = CreateHairCardsVFUniformBuffer(HairInstance, Data.LODIndex, EHairGeometryType::Meshes, bManualFetch);
		}
	}
}

void FHairCardsVertexFactory::InitRHI()
{
	// Nothing as the initialization is done when needed
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

IMPLEMENT_VERTEX_FACTORY_TYPE(FHairCardsVertexFactory,"/Engine/Private/HairStrands/HairCardsVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
);
