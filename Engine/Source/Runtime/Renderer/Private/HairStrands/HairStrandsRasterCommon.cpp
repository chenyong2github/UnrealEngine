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

// this is temporary until we split the voxelize and DOM path
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDeepShadowPassUniformParameters, "DeepShadowPass");

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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FDeepShadowPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FDeepShadowPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FDeepShadowPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FDeepShadowPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FDeepShadowPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
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

template<bool bVoxelizeMaterial>
class FVoxelMeshPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FVoxelMeshPS, MeshMaterial);

public:

	FVoxelMeshPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FDeepShadowPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		DensityTexture.Bind(Initializer.ParameterMap, TEXT("DensityTexture"));
		if (bVoxelizeMaterial)
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
		OutEnvironment.SetDefine(TEXT("SUPPORT_TANGENT_PROPERTY"), bVoxelizeMaterial ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("SUPPORT_MATERIAL_PROPERTY"), bVoxelizeMaterial ? 1 : 0);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << DensityTexture;
		if (bVoxelizeMaterial)
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
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshPS<true>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainVoxel"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVoxelMeshPS<false>, TEXT("/Engine/Private/HairStrands/HairStrandsDeepShadowPS.usf"), TEXT("MainVoxel"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FDeepShadowMeshProcessor : public FMeshPassProcessor
{
public:

	FDeepShadowMeshProcessor(
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

	EHairStrandsRasterPassType DeepShadowPassType;
	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FDeepShadowMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
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
		const ERasterizerCullMode MeshCullMode = DeepShadowPassType == EHairStrandsRasterPassType::FrontDepth ? ComputeMeshCullMode(MeshBatch, Material) : CM_None;

		if (DeepShadowPassType == EHairStrandsRasterPassType::FrontDepth)
			Process<FDeepShadowDepthMeshVS, FDeepShadowDepthMeshPS>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (DeepShadowPassType == EHairStrandsRasterPassType::DeepOpacityMap)
			Process<FDeepShadowDomMeshVS, FDeepShadowDomMeshPS>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (DeepShadowPassType == EHairStrandsRasterPassType::Voxelization)
			Process<FVoxelMeshVS<false>, FVoxelMeshPS<false>>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
		else if (DeepShadowPassType == EHairStrandsRasterPassType::VoxelizationMaterial)
			Process<FVoxelMeshVS<true>, FVoxelMeshPS<true>>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MeshFillMode, MeshCullMode);
	}
}

// Vertex is either FDeepShadowDepthMeshVS, FDeepShadowDomMeshVS, or FVoxelMeshVS
// Pixel  is either FDeepShadowDepthMeshPS, FDeepShadowDomMeshPS, or FVoxelMeshPS
template<typename VertexShaderType, typename PixelShaderType>
void FDeepShadowMeshProcessor::Process(
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

FDeepShadowMeshProcessor::FDeepShadowMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	FDynamicPassMeshDrawListContext* InDrawListContext,
	const EHairStrandsRasterPassType PType)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, DeepShadowPassType(PType)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

/////////////////////////////////////////////////////////////////////////////////////////

void RasterHairStrands(
	FRHICommandList& RHICmdList,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType ShadowPassType,
	const FIntRect& AtlasRect,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformShaderParameters,
	const TUniformBufferRef<FDeepShadowPassUniformParameters>& DeepShadowPassUniformParameters)
{
	check(RHICmdList.IsInsideRenderPass());
	check(IsInRenderingThread());

	SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime);

	FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, DeepShadowPassUniformParameters);
	DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);

	RHICmdList.SetViewport(AtlasRect.Min.X, AtlasRect.Min.Y, 0.0f, AtlasRect.Max.X, AtlasRect.Max.Y, 1.0f);

	if (ShadowPassType == EHairStrandsRasterPassType::DeepOpacityMap)
	{
		DrawRenderState.SetBlendState(TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	}
	else if (ShadowPassType == EHairStrandsRasterPassType::FrontDepth)
	{
		DrawRenderState.SetBlendState(TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
	}
	else if (ShadowPassType == EHairStrandsRasterPassType::Voxelization || ShadowPassType == EHairStrandsRasterPassType::VoxelizationMaterial)
	{
		DrawRenderState.SetBlendState(TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	}

	FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage; // << Were would thid be stored?
	FMeshCommandOneFrameArray VisibleMeshDrawCommands;
	FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
	FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet);


	FDeepShadowMeshProcessor DeepShadowMeshProcessor(Scene, ViewInfo /* is a SceneView */, DrawRenderState, &ShadowContext, ShadowPassType);

	for (const FHairStrandsClusterData::PrimitiveInfo& PrimitveInfo : PrimitiveSceneInfos)
	{
		const FMeshBatch& MeshBatch = *PrimitveInfo.MeshBatchAndRelevance.Mesh;
		const uint64 BatchElementMask = ~0ull;
		DeepShadowMeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy);
	}

	if (VisibleMeshDrawCommands.Num() > 0)
	{
		FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
		SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
		SubmitMeshDrawCommands(VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
	}
}
