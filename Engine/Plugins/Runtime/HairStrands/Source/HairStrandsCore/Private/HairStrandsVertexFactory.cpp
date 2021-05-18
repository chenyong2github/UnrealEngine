// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StrandHairVertexFactory.cpp: Strand hair vertex factory implementation
=============================================================================*/

#include "HairStrandsVertexFactory.h"
#include "SceneView.h"
#include "MeshBatch.h"
#include "ShaderParameterUtils.h"
#include "Rendering/ColorVertexBuffer.h"
#include "MeshMaterialShader.h"
#include "HairStrandsInterface.h"
#include "GroomInstance.h"

#define VF_STRANDS_SUPPORT_GPU_SCENE 0

/////////////////////////////////////////////////////////////////////////////////////////

template<typename T> inline void VFS_BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderResourceParameter& Param, T* Value)	{ if (Param.IsBound() && Value) ShaderBindings.Add(Param, Value); }
template<typename T> inline void VFS_BindParam(FMeshDrawSingleShaderBindings& ShaderBindings, const FShaderParameter& Param, const T& Value)	{ if (Param.IsBound()) ShaderBindings.Add(Param, Value); }

class FDummyCulledDispatchVertexIdsBuffer : public FVertexBuffer
{
public:
	FShaderResourceViewRHIRef SRVUint;
	FShaderResourceViewRHIRef SRVFloat;
	FShaderResourceViewRHIRef SRVRGBA;

	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FDummyCulledDispatchVertexIdsBuffer"));
		uint32 NumBytes = sizeof(uint32) * 4;
		VertexBufferRHI = RHICreateBuffer(NumBytes, BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
		uint32* DummyContents = (uint32*)RHILockBuffer(VertexBufferRHI, 0, NumBytes, RLM_WriteOnly);
		DummyContents[0] = DummyContents[1] = DummyContents[2] = DummyContents[3] = 0;
		RHIUnlockBuffer(VertexBufferRHI);

		SRVUint = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_UINT);
		SRVFloat = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_FLOAT);
		SRVRGBA = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R8G8B8A8);
	}

	virtual void ReleaseRHI() override
	{
		VertexBufferRHI.SafeRelease();
		SRVUint.SafeRelease();
		SRVFloat.SafeRelease();
		SRVRGBA.SafeRelease();
	}
};
TGlobalResource<FDummyCulledDispatchVertexIdsBuffer> GDummyCulledDispatchVertexIdsBuffer;

/////////////////////////////////////////////////////////////////////////////////////////
inline FRHIShaderResourceView* GetPositionSRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)			{ return VFInput.Strands.PositionBufferRHISRV; }; 
inline FRHIShaderResourceView* GetPreviousPositionSRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)	{ return VFInput.Strands.PrevPositionBufferRHISRV; }
inline FRHIShaderResourceView* GetAttribute0SRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)		{ return VFInput.Strands.Attribute0BufferRHISRV; }
inline FRHIShaderResourceView* GetAttribute1SRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)		{ return VFInput.Strands.Attribute1BufferRHISRV; }
inline FRHIShaderResourceView* GetMaterialSRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)			{ return VFInput.Strands.MaterialBufferRHISRV; }
inline FRHIShaderResourceView* GetTangentSRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)			{ return VFInput.Strands.TangentBufferRHISRV; }

inline FRHIShaderResourceView* GetPositionOffsetSRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput)	{ return VFInput.Strands.PositionOffsetBufferRHISRV; };
inline FRHIShaderResourceView* GetPreviousPositionOffsetSRV(const FHairGroupPublicData::FVertexFactoryInput& VFInput) { return VFInput.Strands.PrevPositionOffsetBufferRHISRV; }

inline bool  UseStableRasterization(const FHairGroupPublicData::FVertexFactoryInput& VFInput)					{ return VFInput.Strands.bUseStableRasterization; };
inline bool  UseScatterSceneLighting(const FHairGroupPublicData::FVertexFactoryInput& VFInput)					{ return VFInput.Strands.bScatterSceneLighting; };
inline float GetMaxStrandRadius(const FHairGroupPublicData::FVertexFactoryInput& VFInput)						{ return VFInput.Strands.HairRadius; };
inline float GetStrandRootScale(const FHairGroupPublicData::FVertexFactoryInput& VFInput)						{ return VFInput.Strands.HairRootScale; };
inline float GetStrandTipScale(const FHairGroupPublicData::FVertexFactoryInput& VFInput)						{ return VFInput.Strands.HairTipScale; };
inline float GetMaxStrandLength(const FHairGroupPublicData::FVertexFactoryInput& VFInput)						{ return VFInput.Strands.HairLength; };
inline float GetHairDensity(const FHairGroupPublicData::FVertexFactoryInput& VFInput)							{ return VFInput.Strands.HairDensity; };
//inline float GetHasAttribute1(const FHairGroupPublicData::FVertexFactoryInput& VFInput)							{ return VFInput.Strands.HasAttribute1; };
//inline float GetHasMaterial(const FHairGroupPublicData::FVertexFactoryInput& VFInput)							{ return VFInput.Strands.HasMaterial; };

/////////////////////////////////////////////////////////////////////////////////////////

FHairGroupPublicData::FVertexFactoryInput ComputeHairStrandsVertexInputData(const FHairGroupInstance* Instance);

class FHairStrandsVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FHairStrandsVertexFactoryShaderParameters, NonVirtual);
public:

	LAYOUT_FIELD(FShaderParameter, Radius);
	LAYOUT_FIELD(FShaderParameter, RootScale);
	LAYOUT_FIELD(FShaderParameter, TipScale);
	LAYOUT_FIELD(FShaderParameter, Length);
	LAYOUT_FIELD(FShaderParameter, RadiusAtDepth1_Primary);	// unused
	LAYOUT_FIELD(FShaderParameter, RadiusAtDepth1_Velocity);// unused
	LAYOUT_FIELD(FShaderParameter, Density);
	LAYOUT_FIELD(FShaderParameter, Culling);
	LAYOUT_FIELD(FShaderParameter, HasAttribute1);
	LAYOUT_FIELD(FShaderParameter, HasMaterial);
	LAYOUT_FIELD(FShaderParameter, StableRasterization);
	LAYOUT_FIELD(FShaderParameter, ScatterSceneLighing);

	LAYOUT_FIELD(FShaderResourceParameter, PositionOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PreviousPositionOffsetBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, PreviousPositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, Attribute0Buffer);
	LAYOUT_FIELD(FShaderResourceParameter, Attribute1Buffer);
	LAYOUT_FIELD(FShaderResourceParameter, MaterialBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, TangentBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, CulledVertexIdsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, CulledVertexRadiusScaleBuffer);

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		Radius.Bind(ParameterMap, TEXT("HairStrandsVF_Radius"));
		RootScale.Bind(ParameterMap, TEXT("HairStrandsVF_RootScale"));
		TipScale.Bind(ParameterMap, TEXT("HairStrandsVF_TipScale"));
		Length.Bind(ParameterMap, TEXT("HairStrandsVF_Length"));
		Density.Bind(ParameterMap, TEXT("HairStrandsVF_Density"));
		Culling.Bind(ParameterMap, TEXT("HairStrandsVF_CullingEnable"));
		HasAttribute1.Bind(ParameterMap, TEXT("HairStrandsVF_HasAttribute1"));
		HasMaterial.Bind(ParameterMap, TEXT("HairStrandsVF_HasMaterial"));
		StableRasterization.Bind(ParameterMap, TEXT("HairStrandsVF_bUseStableRasterization"));
		ScatterSceneLighing.Bind(ParameterMap, TEXT("HairStrandsVF_bScatterSceneLighing"));

		PositionOffsetBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_PositionOffsetBuffer"));
		PreviousPositionOffsetBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_PreviousPositionOffsetBuffer"));

		PositionBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_PositionBuffer"));
		PreviousPositionBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_PreviousPositionBuffer"));
		Attribute0Buffer.Bind(ParameterMap, TEXT("HairStrandsVF_Attribute0Buffer"));
		Attribute1Buffer.Bind(ParameterMap, TEXT("HairStrandsVF_Attribute1Buffer"));
		MaterialBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_MaterialBuffer"));
		TangentBuffer.Bind(ParameterMap, TEXT("HairStrandsVF_TangentBuffer"));

		CulledVertexIdsBuffer.Bind(ParameterMap, TEXT("CulledVertexIdsBuffer"));
		CulledVertexRadiusScaleBuffer.Bind(ParameterMap, TEXT("CulledVertexRadiusScaleBuffer"));
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
		const FHairStrandsVertexFactory* VF = static_cast<const FHairStrandsVertexFactory*>(VertexFactory);
		const FHairGroupPublicData::FVertexFactoryInput VFInput = ComputeHairStrandsVertexInputData(VF->Data.Instance);

		FShaderResourceViewRHIRef Attribute1SRV = GetAttribute1SRV(VFInput);
		const bool bHasAttribute1 = Attribute1SRV != nullptr;
		if (!bHasAttribute1)
		{
			Attribute1SRV = GDummyCulledDispatchVertexIdsBuffer.SRVRGBA;
		}

		FShaderResourceViewRHIRef MaterialSRV = GetMaterialSRV(VFInput);
		const bool bHasMaterial = MaterialSRV != nullptr;
		if (!bHasMaterial)
		{
			MaterialSRV = GDummyCulledDispatchVertexIdsBuffer.SRVRGBA;
		}

		VFS_BindParam(ShaderBindings, PositionBuffer, GetPositionSRV(VFInput));
		VFS_BindParam(ShaderBindings, PreviousPositionBuffer, GetPreviousPositionSRV(VFInput));
		VFS_BindParam(ShaderBindings, Attribute0Buffer, GetAttribute0SRV(VFInput));
		VFS_BindParam(ShaderBindings, Attribute1Buffer, Attribute1SRV.GetReference());
		VFS_BindParam(ShaderBindings, MaterialBuffer, MaterialSRV.GetReference());
		VFS_BindParam(ShaderBindings, TangentBuffer, GetTangentSRV(VFInput));
		VFS_BindParam(ShaderBindings, Radius, GetMaxStrandRadius(VFInput));
		VFS_BindParam(ShaderBindings, RootScale, GetStrandRootScale(VFInput));
		VFS_BindParam(ShaderBindings, TipScale, GetStrandTipScale(VFInput));
		VFS_BindParam(ShaderBindings, Length, GetMaxStrandLength(VFInput));
		VFS_BindParam(ShaderBindings, PositionOffsetBuffer, GetPositionOffsetSRV(VFInput));
		VFS_BindParam(ShaderBindings, PreviousPositionOffsetBuffer, GetPreviousPositionOffsetSRV(VFInput));
		VFS_BindParam(ShaderBindings, Density, GetHairDensity(VFInput));
		VFS_BindParam(ShaderBindings, StableRasterization, UseStableRasterization(VFInput) ? 1u : 0u);
		VFS_BindParam(ShaderBindings, ScatterSceneLighing, UseScatterSceneLighting(VFInput) ? 1u : 0u);
		VFS_BindParam(ShaderBindings, HasAttribute1, bHasAttribute1 ? 1u : 0u);
		VFS_BindParam(ShaderBindings, HasMaterial, bHasMaterial ? 1u : 0u);
		
		FShaderResourceViewRHIRef CulledDispatchVertexIdsSRV = GDummyCulledDispatchVertexIdsBuffer.SRVUint;
		FShaderResourceViewRHIRef CulledCompactedRadiusScaleBufferSRV = GDummyCulledDispatchVertexIdsBuffer.SRVFloat;

		const FHairGroupPublicData* GroupPublicData = VF->Data.Instance->HairGroupPublicData;
		const bool bCulling = GroupPublicData->GetCullingResultAvailable();
		if (bCulling)
		{
			CulledDispatchVertexIdsSRV = GroupPublicData->GetCulledVertexIdBuffer().SRV;
			CulledCompactedRadiusScaleBufferSRV = GroupPublicData->GetCulledVertexRadiusScaleBuffer().SRV;
		}

		VFS_BindParam(ShaderBindings, Culling, bCulling ? 1 : 0);
		ShaderBindings.Add(CulledVertexIdsBuffer, CulledDispatchVertexIdsSRV);
		ShaderBindings.Add(CulledVertexRadiusScaleBuffer, CulledCompactedRadiusScaleBufferSRV);
	}
};

IMPLEMENT_TYPE_LAYOUT(FHairStrandsVertexFactoryShaderParameters);

/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FHairStrandsVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithHairStrands && IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform)) || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FHairStrandsVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	const bool bUseGPUSceneAndPrimitiveIdStream = 
		VF_STRANDS_SUPPORT_GPU_SCENE
		&& Parameters.VertexFactoryType->SupportsPrimitiveIdStream() 
		&& UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform));
	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), bUseGPUSceneAndPrimitiveIdStream);
	OutEnvironment.SetDefine(TEXT("HAIR_STRAND_MESH_FACTORY"), TEXT("1"));
}

void FHairStrandsVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
#if VF_STRANDS_SUPPORT_GPU_SCENE
	if (Type->SupportsPrimitiveIdStream()
		&& UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform)) 
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::StaticStructMetadata.GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(PrimitiveId).Member instead of Primitive.Member."), Type->GetName()));
	}
#endif
}

void FHairStrandsVertexFactory::SetData(const FDataType& InData)
{
	check(IsInRenderingThread());
	Data = InData;
	UpdateRHI();
}

/**
* Copy the data from another vertex factory
* @param Other - factory to copy from
*/
void FHairStrandsVertexFactory::Copy(const FHairStrandsVertexFactory& Other)
{
	FHairStrandsVertexFactory* VertexFactory = this;
	const FDataType* DataCopy = &Other.Data;
	ENQUEUE_RENDER_COMMAND(FHairStrandsVertexFactoryCopyData)(
		[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->Data = *DataCopy;
		});
	BeginUpdateResourceRHI(this);
}

void FHairStrandsVertexFactory::InitResources()
{
	if (bIsInitialized)
		return;

	FVertexFactory::InitResource(); //Call VertexFactory/RenderResources::InitResource() to mark the resource as initialized();

	bIsInitialized = true;
	bNeedsDeclaration = false;
	bSupportsManualVertexFetch = true;

	// We create different streams based on feature level
	check(HasValidFeatureLevel());

	FVertexDeclarationElementList Elements;
	SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, -1);
#if VF_STRANDS_SUPPORT_GPU_SCENE
	// VertexFactory needs to be able to support max possible shader platform and feature level
	// in case if we switch feature level at runtime.
	const bool bCanUseGPUScene = UseGPUScene(GMaxRHIShaderPlatform, GMaxRHIFeatureLevel);
	if (GetType()->SupportsPrimitiveIdStream() && bCanUseGPUScene)
	{
		// When the VF is used for rendering in normal mesh passes, this vertex buffer and offset will be overridden
		Elements.Add(AccessStreamComponent(FVertexStreamComponent(&GPrimitiveIdDummy, 0, 0, PrimitiveIdStreamStride, VET_UInt, EVertexStreamUsage::Instancing), 13));
		SetPrimitiveIdStreamIndex(EVertexInputStreamType::Default, Elements.Last().StreamIndex);
		bNeedsDeclaration = true;
	}
#endif

	if (bNeedsDeclaration)
	{
		check(Streams.Num() > 0);
	}
	InitDeclaration(Elements);
	check(IsValidRef(GetDeclaration()));
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_Vertex,		FHairStrandsVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_Pixel,		FHairStrandsVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_Compute,		FHairStrandsVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FHairStrandsVertexFactory, SF_RayHitGroup,	FHairStrandsVertexFactoryShaderParameters);
#endif

void FHairStrandsVertexFactory::InitRHI()
{
	// Nothing as the initialize runs only on first use
}

void FHairStrandsVertexFactory::ReleaseRHI()
{
	FVertexFactory::ReleaseRHI();
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FHairStrandsVertexFactory,"/Engine/Private/HairStrands/HairStrandsVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
	| (VF_STRANDS_SUPPORT_GPU_SCENE ? EVertexFactoryFlags::SupportsPrimitiveIdStream : EVertexFactoryFlags::None)
);
