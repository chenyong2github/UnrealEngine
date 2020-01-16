// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsRasterCommon.h"
#include "HairStrandsUtils.h"
#include "PrimitiveSceneProxy.h"
#include "Shader.h"
#include "MeshMaterialShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"

/////////////////////////////////////////////////////////////////////////////////////////
// Deep shadow global parameters
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHairDeepShadowRasterGlobalParameters, )
	SHADER_PARAMETER(FMatrix, WorldToClipMatrix)
	SHADER_PARAMETER(FVector4, SliceValue)
	SHADER_PARAMETER(FIntRect, AtlasRect)
	SHADER_PARAMETER(FIntPoint, ViewportResolution)
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, FrontDepthTexture)
	END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHairDeepShadowRasterGlobalParameters, "DeepRasterPass");

static FHairDeepShadowRasterGlobalParameters ConvertToGlobalPassParameter(const FHairDeepShadowRasterPassParameters* In)
{
	FHairDeepShadowRasterGlobalParameters Out;
	Out.WorldToClipMatrix	= In->WorldToClipMatrix;
	Out.SliceValue			= In->SliceValue;
	Out.AtlasRect			= In->AtlasRect;
	Out.ViewportResolution	= In->ViewportResolution;
	Out.FrontDepthTexture	= In->FrontDepthTexture ? In->FrontDepthTexture->GetRHI() : (FRHITexture*)(GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture);
	return Out;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Voxelization global parameters
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHairVoxelizationRasterGlobalParameters, )
	SHADER_PARAMETER_STRUCT(FVirtualVoxelCommonParameters, VirtualVoxel)
	SHADER_PARAMETER(FMatrix, WorldToClipMatrix)
	SHADER_PARAMETER(FVector, VoxelMinAABB)
	SHADER_PARAMETER(FVector, VoxelMaxAABB)
	SHADER_PARAMETER(FIntVector, VoxelResolution)
	SHADER_PARAMETER(uint32, MacroGroupId)
	SHADER_PARAMETER(FIntPoint, ViewportResolution)
	SHADER_PARAMETER_SRV(StructuredBuffer<FVoxelizationViewInfo>, VoxelizationViewInfoBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHairVoxelizationRasterGlobalParameters, "VoxelRasterPass");

static FHairVoxelizationRasterGlobalParameters ConvertToGlobalPassParameter(const FHairVoxelizationRasterPassParameters* In)
{
	FHairVoxelizationRasterGlobalParameters Out;
	Out.VirtualVoxel	   = In->VirtualVoxel;
	Out.WorldToClipMatrix  = In->WorldToClipMatrix;
	Out.VoxelMinAABB	   = In->VoxelMinAABB;
	Out.VoxelMaxAABB	   = In->VoxelMaxAABB;
	Out.VoxelResolution	   = In->VoxelResolution;
	Out.MacroGroupId	   = In->MacroGroupId;
	Out.ViewportResolution = In->ViewportResolution;
	Out.VoxelizationViewInfoBuffer = In->VoxelizationViewInfoBuffer->GetRHI();
	return Out;
}

/////////////////////////////////////////////////////////////////////////////////////////

class FDeepShadowDepthMeshVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FDeepShadowDepthMeshVS, MeshMaterial);

protected:

	FDeepShadowDepthMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		// deferred
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairDeepShadowRasterGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FDeepShadowDepthMeshVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MESH_RENDER_MODE"), 0);
		OutEnvironment.SetDefine(TEXT("USE_CULLED_CLUSTER"), 1);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FDeepShadowDepthMeshVS, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FDeepShadowDomMeshVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FDeepShadowDomMeshVS, MeshMaterial);

protected:

	FDeepShadowDomMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		// deferred
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairDeepShadowRasterGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FDeepShadowDomMeshVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MESH_RENDER_MODE"), 1);
		OutEnvironment.SetDefine(TEXT("USE_CULLED_CLUSTER"), 1);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FDeepShadowDomMeshVS, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

template<bool bVoxelizeMaterial>
class FVoxelMeshVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FVoxelMeshVS, MeshMaterial);

protected:

	FVoxelMeshVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		// deferred
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairVoxelizationRasterGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FVoxelMeshVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MESH_RENDER_MODE"), 2);
		OutEnvironment.SetDefine(TEXT("SUPPORT_TANGENT_PROPERTY"), bVoxelizeMaterial ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("SUPPORT_MATERIAL_PROPERTY"), bVoxelizeMaterial ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("USE_CULLED_CLUSTER"), 1);
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshVS<false>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshVS<true>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FDeepShadowDepthMeshPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FDeepShadowDepthMeshPS, MeshMaterial);

public:

	FDeepShadowDepthMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairDeepShadowRasterGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FDeepShadowDepthMeshPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDeepShadowDepthMeshPS, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainDepth"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FDeepShadowDomMeshPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FDeepShadowDomMeshPS, MeshMaterial);

public:

	FDeepShadowDomMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairDeepShadowRasterGlobalParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FDeepShadowDomMeshPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FDeepShadowDomMeshPS, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainDom"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

enum class EVoxelMeshPSType
{
	Density,
	Material,
	DensityVirtual
};
template<EVoxelMeshPSType VoxelizationType>
class FVoxelMeshPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FVoxelMeshPS, MeshMaterial);

public:

	FVoxelMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairVoxelizationRasterGlobalParameters::StaticStructMetadata.GetShaderVariableName());
		DensityTexture.Bind(Initializer.ParameterMap, TEXT("DensityTexture"));
		if (VoxelizationType == EVoxelMeshPSType::Material)
		{
			TangentXTexture.Bind(Initializer.ParameterMap, TEXT("TangentXTexture"));
			TangentYTexture.Bind(Initializer.ParameterMap, TEXT("TangentYTexture"));
			TangentZTexture.Bind(Initializer.ParameterMap, TEXT("TangentZTexture"));
			MaterialTexture.Bind(Initializer.ParameterMap, TEXT("MaterialTexture"));
		}
	}

	FVoxelMeshPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUPPORT_TANGENT_PROPERTY"), VoxelizationType == EVoxelMeshPSType::Material ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("SUPPORT_MATERIAL_PROPERTY"), VoxelizationType == EVoxelMeshPSType::Material ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("SUPPORT_SPARSE"), VoxelizationType == EVoxelMeshPSType::DensityVirtual ? 1 : 0);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << DensityTexture;
		if (VoxelizationType == EVoxelMeshPSType::Material)
		{
			Ar << TangentXTexture;
			Ar << TangentYTexture;
			Ar << TangentZTexture;
			Ar << MaterialTexture;
		}
		return bShaderHasOutdatedParameters;
	}
private:
	FRWShaderParameter DensityTexture;
	FRWShaderParameter TangentXTexture;
	FRWShaderParameter TangentYTexture;
	FRWShaderParameter TangentZTexture;
	FRWShaderParameter MaterialTexture;
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshPS<EVoxelMeshPSType::Density>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainVoxel"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshPS<EVoxelMeshPSType::Material>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainVoxel"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshPS<EVoxelMeshPSType::DensityVirtual>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainVoxel"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairRasterMeshProcessor : public FMeshPassProcessor
{
public:

	FHairRasterMeshProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FDynamicPassMeshDrawListContext* InDrawListContext,
		const EHairStrandsRasterPassType PType);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	template<typename VertexShaderType, typename PixelShaderType>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	EHairStrandsRasterPassType RasterPassType;
	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairRasterMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);

	if (bIsCompatible
		&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass())
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material);
		const ERasterizerCullMode MeshCullMode = RasterPassType == EHairStrandsRasterPassType::FrontDepth ? ComputeMeshCullMode(MeshBatch, Material) : CM_None;

		if (RasterPassType == EHairStrandsRasterPassType::FrontDepth)
			Process<FDeepShadowDepthMeshVS, FDeepShadowDepthMeshPS>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (RasterPassType == EHairStrandsRasterPassType::DeepOpacityMap)
			Process<FDeepShadowDomMeshVS, FDeepShadowDomMeshPS>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (RasterPassType == EHairStrandsRasterPassType::Voxelization)
			Process<FVoxelMeshVS<false>, FVoxelMeshPS<EVoxelMeshPSType::Density>>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (RasterPassType == EHairStrandsRasterPassType::VoxelizationMaterial)
			Process<FVoxelMeshVS<true>, FVoxelMeshPS<EVoxelMeshPSType::Material>>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (RasterPassType == EHairStrandsRasterPassType::VoxelizationVirtual)
			Process<FVoxelMeshVS<false>, FVoxelMeshPS<EVoxelMeshPSType::DensityVirtual>>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
	}
}

// Vertex is either FDeepShadowDepthMeshVS, FDeepShadowDomMeshVS, or FVoxelMeshVS
// Pixel  is either FDeepShadowDepthMeshPS, FDeepShadowDomMeshPS, or FVoxelMeshPS
template<typename VertexShaderType, typename PixelShaderType>
void FHairRasterMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	TMeshProcessorShaders<
		VertexShaderType,
		FMeshMaterialShader,
		FMeshMaterialShader,
		PixelShaderType> PassShaders;
	{
		const EMaterialTessellationMode MaterialTessellationMode = MaterialResource.GetTessellationMode();
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetId() == CompatibleVF->GetId();
		if (!bIsHairStrandsFactory)
			return;

		PassShaders.DomainShader = nullptr;
		PassShaders.HullShader = nullptr;
		PassShaders.VertexShader = MaterialResource.GetShader<VertexShaderType>(VertexFactoryType);
		PassShaders.PixelShader = MaterialResource.GetShader<PixelShaderType>(VertexFactoryType);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		PassShaders,
		MeshFillMode,
		MeshCullMode,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

FHairRasterMeshProcessor::FHairRasterMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	FDynamicPassMeshDrawListContext* InDrawListContext,
	const EHairStrandsRasterPassType PType)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, RasterPassType(PType)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

/////////////////////////////////////////////////////////////////////////////////////////

template<typename TPassParameter, typename TGlobalParameter>
void AddHairStrandsRasterPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType RasterPassType,
	const FIntRect& ViewportRect,
	const FVector4& HairRenderInfo,
	const FVector& RasterDirection,
	TPassParameter* PassParameters)
{
	auto GetPassName = [](EHairStrandsRasterPassType Type)
	{
		switch (Type)
		{
		case EHairStrandsRasterPassType::DeepOpacityMap:		return RDG_EVENT_NAME("HairStrandsRasterDeepOpacityMap");
		case EHairStrandsRasterPassType::FrontDepth:			return RDG_EVENT_NAME("HairStrandsRasterFrontDepth");
		case EHairStrandsRasterPassType::Voxelization:			return RDG_EVENT_NAME("HairStrandsRasterVoxelization");
		case EHairStrandsRasterPassType::VoxelizationMaterial:	return RDG_EVENT_NAME("HairStrandsRasterVoxelizationMaterial");
		case EHairStrandsRasterPassType::VoxelizationVirtual:	return RDG_EVENT_NAME("HairStrandsRasterVoxelizationVirtual");
		default:												return RDG_EVENT_NAME("Noname");
		}
	};

	GraphBuilder.AddPass(
		GetPassName(RasterPassType),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, RasterPassType, &PrimitiveSceneInfos, ViewportRect, HairRenderInfo, RasterDirection](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime);

		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
		ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo = HairRenderInfo;

		const FVector SavedViewForward = ViewInfo->CachedViewUniformShaderParameters->ViewForward;	
		ViewInfo->CachedViewUniformShaderParameters->ViewForward = RasterDirection;
		ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
		ViewInfo->CachedViewUniformShaderParameters->ViewForward = SavedViewForward;

		TGlobalParameter GlobalPassParameters = ConvertToGlobalPassParameter(PassParameters);
		TUniformBufferRef<TGlobalParameter> GlobalPassParametersBuffer = TUniformBufferRef<TGlobalParameter>::CreateUniformBufferImmediate(GlobalPassParameters, UniformBuffer_SingleFrame);

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, GlobalPassParametersBuffer);
		DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);

		RHICmdList.SetViewport(ViewportRect.Min.X, ViewportRect.Min.Y, 0.0f, ViewportRect.Max.X, ViewportRect.Max.Y, 1.0f);

		if (RasterPassType == EHairStrandsRasterPassType::DeepOpacityMap)
		{
			DrawRenderState.SetBlendState(TStaticBlendState<
				CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
				CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		}
		else if (RasterPassType == EHairStrandsRasterPassType::FrontDepth)
		{
			DrawRenderState.SetBlendState(TStaticBlendState<
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		}
		else if (RasterPassType == EHairStrandsRasterPassType::Voxelization || RasterPassType == EHairStrandsRasterPassType::VoxelizationMaterial)
		{
			DrawRenderState.SetBlendState(TStaticBlendState<
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		}
		else if (RasterPassType == EHairStrandsRasterPassType::VoxelizationVirtual)
		{
			DrawRenderState.SetBlendState(TStaticBlendState<
				CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		}

		FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage; // << Were would thid be stored?
		FMeshCommandOneFrameArray VisibleMeshDrawCommands;
		FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
		FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet);

		FHairRasterMeshProcessor HairRasterMeshProcessor(Scene, ViewInfo /* is a SceneView */, DrawRenderState, &ShadowContext, RasterPassType);

		for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitveInfo : PrimitiveSceneInfos)
		{
			const FMeshBatch& MeshBatch = *PrimitveInfo.MeshBatchAndRelevance.Mesh;
			const uint64 BatchElementMask = ~0ull;
			HairRasterMeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy);
		}

		if (VisibleMeshDrawCommands.Num() > 0)
		{
			FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
			SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
			SubmitMeshDrawCommands(VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
		}
	});
}

void AddHairDeepShadowRasterPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType PassType,
	const FIntRect& ViewportRect,
	const FVector4& HairRenderInfo,
	const FVector& LightDirection,
	FHairDeepShadowRasterPassParameters* PassParameters)
{
	check(PassType == EHairStrandsRasterPassType::FrontDepth || PassType == EHairStrandsRasterPassType::DeepOpacityMap);

	AddHairStrandsRasterPass<FHairDeepShadowRasterPassParameters, FHairDeepShadowRasterGlobalParameters>(
		GraphBuilder, 
		Scene, 
		ViewInfo, 
		PrimitiveSceneInfos, 
		PassType, 
		ViewportRect, 
		HairRenderInfo, 
		LightDirection,
		PassParameters);
}

void AddHairVoxelizationRasterPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType PassType,
	const FIntRect& ViewportRect,
	const FVector4& HairRenderInfo,
	const FVector& RasterDirection,
	FHairVoxelizationRasterPassParameters* PassParameters)
{
	check(PassType == EHairStrandsRasterPassType::Voxelization || PassType == EHairStrandsRasterPassType::VoxelizationMaterial || PassType == EHairStrandsRasterPassType::VoxelizationVirtual);

	AddHairStrandsRasterPass<FHairVoxelizationRasterPassParameters, FHairVoxelizationRasterGlobalParameters>(
		GraphBuilder, 
		Scene, 
		ViewInfo, 
		PrimitiveSceneInfos, 
		PassType, 
		ViewportRect, 
		HairRenderInfo, 
		RasterDirection,
		PassParameters);
}