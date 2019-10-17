// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsVisibility.h"
#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "HairStrandsInterface.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "MeshPassProcessor.inl"

DECLARE_GPU_STAT(HairStrandsVisibility);

/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHairVisibilityPassUniformParameters, )
SHADER_PARAMETER(float, MinStrandRadius_Primary)
SHADER_PARAMETER(float, MinStrandRadius_Velocity)
SHADER_PARAMETER(float, HairStrandsVelocityScale)
SHADER_PARAMETER_TEXTURE(Texture2D<float>, MainDepthTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHairVisibilityPassUniformParameters, "HairVisibilityPass");

/////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairStrandsCoveragePassEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsCoveragePassEnable(TEXT("r.HairStrands.CoveragePass"), GHairStrandsCoveragePassEnable, TEXT("Enable accurate coverage pass"));

static int32 GHairStrandsMaterialCompactionEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionEnable(TEXT("r.HairStrands.MaterialCompaction"), GHairStrandsMaterialCompactionEnable, TEXT("Enable extra compaction based on material properties in order to reduce sample per pixel and improve performance."));

static float GHairStrandsMaterialCompactionDepthThreshold = 1.f;
static float GHairStrandsMaterialCompactionTangentThreshold = 10.f;
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionDepthThreshold(TEXT("r.HairStrands.MaterialCompaction.DepthThreshold"), GHairStrandsMaterialCompactionDepthThreshold, TEXT("Compaction threshold for depth value for material compaction (in centimeters). Default 1 cm."));
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionTangentThreshold(TEXT("r.HairStrands.MaterialCompaction.TangentThreshold"), GHairStrandsMaterialCompactionTangentThreshold, TEXT("Compaciton threshold for tangent value for material compaction (in degrees). Default 10 deg."));


static int32 GHairVisibilitySampleCount = 8;
static FAutoConsoleVariableRef CVarHairVisibilitySampleCount(TEXT("r.HairStrands.VisibilitySampleCount"), GHairVisibilitySampleCount, TEXT("Hair strands visibility sample count"));

static int32 GHairClearVisibilityBuffer = 0;
static FAutoConsoleVariableRef CVarHairClearVisibilityBuffer(TEXT("r.HairStrands.VisibilityClear"), GHairClearVisibilityBuffer, TEXT("Clear hair strands visibility buffer"));

static int32 GHairVelocityType = 1; // default is 
static FAutoConsoleVariableRef CVarHairVelocityType(TEXT("r.HairStrands.VelocityType"), GHairVelocityType, TEXT("Type of velocity filtering (0:avg, 1:closest, 2:max). Default is 1."));

static int32 GHairVelocityMagnitudeScale = 100; // Tuned by eye, based on heavy motion (strong head shack)
static FAutoConsoleVariableRef CVarHairVelocityMagnitudeScale(TEXT("r.HairStrands.VelocityMagnitudeScale"), GHairVelocityMagnitudeScale, TEXT("Velocity magnitude (in pixel) at which a hair will reach its pic velocity-rasterization-scale under motion to reduce aliasing. Default is 100."));

/////////////////////////////////////////////////////////////////////////////////////////

enum EHairVisibilityRenderMode
{
	HairVisibilityRenderMode_MSAA,
	HairVisibilityRenderMode_Coverage,
	HairVisibilityRenderModeCount
};

EHairVisibilityRenderMode GetHairVisibilityRenderMode()
{
	return HairVisibilityRenderMode_MSAA;
}

uint32 GetHairVisibilitySampleCount()
{
	return GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? FMath::Clamp(GHairVisibilitySampleCount,1,16) : 1;
}

static bool IsCompatibleWithHairVisibility(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
}


template<EHairVisibilityRenderMode RenderMode>
class FHairVisibilityVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairVisibilityVS,MeshMaterial);

protected:

	FHairVisibilityVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		// deferred
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairVisibilityPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FHairVisibilityVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairVisibility(Parameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const uint32 RenderModeValue = uint32(RenderMode);
		OutEnvironment.SetDefine(TEXT("HAIR_RENDER_MODE"), RenderModeValue);
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_MSAA>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_Coverage>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairVisibilityShaderElementData(uint32 InHairClusterId) : HairClusterId(InHairClusterId) { }
	uint32 HairClusterId;
};

template<EHairVisibilityRenderMode RenderMode>
class FHairVisibilityPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairVisibilityPS, MeshMaterial);

public:

	FHairVisibilityPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FHairVisibilityPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
		HairVisibilityPass_HairClusterIndex.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairClusterIndex"));
	}

	FHairVisibilityPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)	
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const uint32 RenderModeValue = uint32(RenderMode);
		OutEnvironment.SetDefine(TEXT("HAIR_RENDER_MODE"), RenderModeValue);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << HairVisibilityPass_HairClusterIndex;
		return bShaderHasOutdatedParameters;
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FHairVisibilityShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
		ShaderBindings.Add(HairVisibilityPass_HairClusterIndex, ShaderElementData.HairClusterId);
	}

	FShaderParameter HairVisibilityPass_HairClusterIndex;
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_MSAA>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_Coverage>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityProcessor : public FMeshPassProcessor
{
public:
	FHairVisibilityProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		const EHairVisibilityRenderMode InRenderMode,
		FDynamicPassMeshDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairClusterId);

private:
	template<EHairVisibilityRenderMode RenderMode>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const uint32 HairClusterId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	const EHairVisibilityRenderMode RenderMode;
	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0);
}

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairClusterId)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetId() == CompatibleVF->GetId();

	if (bIsCompatible 
		&& bIsHairStrandsFactory
		&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass())
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material);
		if (RenderMode == HairVisibilityRenderMode_MSAA)
			Process<HairVisibilityRenderMode_MSAA>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, MeshFillMode, MeshCullMode);
		if (RenderMode == HairVisibilityRenderMode_Coverage)
			Process<HairVisibilityRenderMode_Coverage>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, MeshFillMode, MeshCullMode);
	}
}

template<EHairVisibilityRenderMode TRenderMode>
void FHairVisibilityProcessor::Process(
	const FMeshBatch& MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const uint32 HairClusterId,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHairVisibilityVS<TRenderMode>,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FHairVisibilityPS<TRenderMode>> PassShaders;
	{
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.VertexShader = MaterialResource.GetShader<FHairVisibilityVS<TRenderMode>>(VertexFactoryType);
		PassShaders.PixelShader  = MaterialResource.GetShader<FHairVisibilityPS<TRenderMode>>(VertexFactoryType);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	FHairVisibilityShaderElementData ShaderElementData(HairClusterId);
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

FHairVisibilityProcessor::FHairVisibilityProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	const EHairVisibilityRenderMode InRenderMode,
	FDynamicPassMeshDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, RenderMode(InRenderMode)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Clear uint texture
class FClearUIntGraphicPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearUIntGraphicPS);
	SHADER_USE_PARAMETER_STRUCT(FClearUIntGraphicPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ClearValue)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FClearUIntGraphicPS, "/Engine/Private/HairStrands/HairStrandsVisibilityClearPS.usf", "ClearPS", SF_Pixel);

// Opaque mask
static void AddClearGraphicPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	const FViewInfo* View,
	const uint32 ClearValue,
	FRDGTextureRef& OutTarget)
{
	check(OutTarget);

	FClearUIntGraphicPS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearUIntGraphicPS::FParameters>();
	Parameters->ClearValue = ClearValue;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTarget, ERenderTargetLoadAction::ENoAction, 0);

	TShaderMapRef<FPostProcessVS> VertexShader(View->ShaderMap);
	TShaderMapRef<FClearUIntGraphicPS> PixelShader(View->ShaderMap);
	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution = OutTarget->Desc.Extent;

	ClearUnusedGraphResources(*PixelShader, Parameters);

	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, View](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, View->ViewUniformBuffer);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);

		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(),Viewport.Height(),
			Viewport.Min.X,  Viewport.Min.Y,
			Viewport.Width(),Viewport.Height(),
			Viewport.Size(),
			Resolution,
			*VertexShader,
			EDRF_UseTriangleOptimization);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FClearUAVTextureParameters, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, TextureUAV)
END_SHADER_PARAMETER_STRUCT()

void AddClearUAVPass(
	FRDGBuilder& GraphBuilder,
	FRDGEventName&& PassName,
	FRDGTextureRef Texture,
	uint32 Value)
{	
	FClearUAVTextureParameters* Parameters = GraphBuilder.AllocParameters< FClearUAVTextureParameters >();
	Parameters->TextureUAV = GraphBuilder.CreateUAV(Texture);

	GraphBuilder.AddPass(
		Forward<FRDGEventName>(PassName),
		Parameters,
		ERDGPassFlags::Compute,
		[Parameters, Texture, Value](FRHICommandList& RHICmdList)
	{
		uint32 ClearValue[4] = { Value, Value, Value, Value };
		FRHIUnorderedAccessView* GlobalCounterUAV = Parameters->TextureUAV->GetRHI();
		RHICmdList.ClearTinyUAV(GlobalCounterUAV, ClearValue);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Copy dispatch count into an indirect buffer 
class FCopyIndirectBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyIndirectBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FCopyIndirectBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ThreadGroupSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CounterTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutArgBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FCopyIndirectBufferCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCopyIndirectArg.usf", "CopyCS", SF_Compute);

static FRDGBufferRef AddCopyIndirectArgPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const uint32 ThreadGroupSize,
	FRDGTextureRef CounterTexture)
{
	check(CounterTexture);

	FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairVisibilityIndirectArgBuffer"));

	FCopyIndirectBufferCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyIndirectBufferCS::FParameters>();
	Parameters->ThreadGroupSize = 32;
	Parameters->CounterTexture = CounterTexture;
	Parameters->OutArgBuffer = GraphBuilder.CreateUAV(OutBuffer);

	TShaderMapRef<FCopyIndirectBufferCS> ComputeShader(View->ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisbilityCopyIndirectArgs"),
		*ComputeShader,
		Parameters,
		FIntVector(1,1,1));

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityPrimitiveIdCompactionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityPrimitiveIdCompactionCS, FGlobalShader);

	class FVendor   : SHADER_PERMUTATION_INT("PERMUTATION_VENDOR", HairVisibilityVendorCount);
	class FVelocity : SHADER_PERMUTATION_INT("PERMUTATION_VELOCITY", 4);
	class FCoverage : SHADER_PERMUTATION_INT("PERMUTATION_COVERAGE", 2);
	class FMaterial : SHADER_PERMUTATION_INT("PERMUTATION_MATERIAL_COMPACTION", 2);
	using FPermutationDomain = TShaderPermutationDomain<FVendor, FVelocity, FCoverage, FMaterial>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, MaxNodeCount)
		SHADER_PARAMETER(uint32, HairVisibilitySampleCount)
		SHADER_PARAMETER(FIntPoint, ResolutionOffset)
		SHADER_PARAMETER(float, DepthTheshold)
		SHADER_PARAMETER(float, CosTangentThreshold)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_IDTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_MaterialTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_AttributeTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_VelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CoverageTexture)
		
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCompactNodeCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCompactNodeIndex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutCategorizationTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutCompactNodeData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutCompactNodeCoord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutVelocityTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCompaction.usf", "MainCS", SF_Compute);

static void AddHairVisibilityPrimitiveIdCompactionPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsClusterDatas& ClusterDatas,
	const uint32 NodeGroupSize,
	const FRDGTextureRef& MSAA_DepthTexture,
	const FRDGTextureRef& MSAA_IDTexture,
	const FRDGTextureRef& MSAA_MaterialTexture,
	const FRDGTextureRef& MSAA_AttributeTexture,
	const FRDGTextureRef& MSAA_VelocityTexture,
	const FRDGTextureRef& CoverageTexture,
	FRDGTextureRef& OutCompactNodeIndex,
	FRDGBufferRef& OutCompactNodeData,
	FRDGBufferRef& OutCompactNodeCoord,
	FRDGTextureRef& OutCategorizationTexture,
	FRDGTextureRef& OutVelocityTexture,
	FRDGBufferRef& OutIndirectArgsBuffer)
{
	check(MSAA_DepthTexture);
	check(MSAA_IDTexture);
	check(MSAA_MaterialTexture);
	check(MSAA_AttributeTexture);

	const FIntPoint Resolution = MSAA_DepthTexture->Desc.Extent;
	
	FRDGTextureRef CompactCounter;
	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = 1;
		Desc.Extent.Y = 1;
		Desc.Depth = 0;
		Desc.Format = PF_R32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		CompactCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactCounter"));
	}

	{
		FRDGTextureDesc Desc;
		Desc.Extent = Resolution;
		Desc.Depth = 0;
		Desc.Format = PF_R32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		OutCompactNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactNodeIndex"));
	}

	{
		FRDGTextureDesc OutputDesc;
		OutputDesc.Extent = Resolution;
		OutputDesc.Format = PF_R16G16B16A16_UINT;
		OutputDesc.NumMips = 1;
		OutputDesc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
		OutCategorizationTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("CategorizationTexture"));
	}
	
	AddClearUAVPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearCompactionCounter"), CompactCounter, 0);
	AddClearUAVPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearCompactionOffsetAndCount"), OutCompactNodeIndex, 0);
	AddClearUAVPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearCategorizationTexture"), OutCategorizationTexture, 0);

	const uint32 HairVisibilitySampleCount = GetHairVisibilitySampleCount();
	const uint32 SampleCount = FMath::RoundUpToPowerOfTwo(HairVisibilitySampleCount);
	const uint32 MaxNodeCount = Resolution.X * Resolution.Y * SampleCount;
	{
		struct NodeData
		{
			uint32 Depth;
			uint32 PrimitiveId_ClusterId;
			uint32 Tangent_Coverage;
			uint32 BaseColor_Roughness;
			uint32 Specular;
		};

		OutCompactNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(NodeData), MaxNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeData"));
	}

	{
		// Pixel coord of the node. Stored as 2*uint16, packed into a single uint32
		OutCompactNodeCoord = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeCoord"));
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FSceneTexturesUniformParameters SceneTextures;
	SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, SceneTextures);

	const bool bWriteOutVelocity = OutVelocityTexture != nullptr;
	FHairVisibilityPrimitiveIdCompactionCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVendor>(GetVendor());
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVelocity>(bWriteOutVelocity ? FMath::Clamp(GHairVelocityType+1, 0, 3) : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FCoverage>(CoverageTexture ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FMaterial>(GHairStrandsMaterialCompactionEnable ? 1 : 0);

	FHairVisibilityPrimitiveIdCompactionCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
	Parameters->MSAA_DepthTexture = MSAA_DepthTexture;
	Parameters->MSAA_IDTexture = MSAA_IDTexture;
	Parameters->MSAA_MaterialTexture = MSAA_MaterialTexture;
	Parameters->MSAA_AttributeTexture = MSAA_AttributeTexture;
	Parameters->CoverageTexture = CoverageTexture;
	Parameters->OutputResolution = Resolution;
	Parameters->MaxNodeCount = MaxNodeCount;
	Parameters->DepthTheshold = FMath::Clamp(GHairStrandsMaterialCompactionDepthThreshold, 0.f, 100.f);
	Parameters->CosTangentThreshold = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(GHairStrandsMaterialCompactionTangentThreshold, 0.f, 90.f)));
	Parameters->HairVisibilitySampleCount = HairVisibilitySampleCount;
	Parameters->SceneTexturesStruct = CreateUniformBufferImmediate(SceneTextures, EUniformBufferUsage::UniformBuffer_SingleDraw);
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->OutCompactNodeCounter = GraphBuilder.CreateUAV(CompactCounter);
	Parameters->OutCompactNodeIndex = GraphBuilder.CreateUAV(OutCompactNodeIndex);
	Parameters->OutCompactNodeData = GraphBuilder.CreateUAV(OutCompactNodeData);
	Parameters->OutCompactNodeCoord = GraphBuilder.CreateUAV(OutCompactNodeCoord);
	Parameters->OutCategorizationTexture = GraphBuilder.CreateUAV(OutCategorizationTexture);

	if (bWriteOutVelocity)
	{
		Parameters->MSAA_VelocityTexture = MSAA_VelocityTexture;
		Parameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);
	}
 
	FIntRect TotalRect = ComputeVisibleHairStrandsClustersRect(View.ViewRect, ClusterDatas);

	// Snap the rect onto thread group boundary
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TotalRect.Min.X = FMath::FloorToInt(float(TotalRect.Min.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Min.Y = FMath::FloorToInt(float(TotalRect.Min.Y) / float(GroupSize.Y)) * GroupSize.Y;
	TotalRect.Max.X = FMath::CeilToInt(float(TotalRect.Max.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Max.Y = FMath::CeilToInt(float(TotalRect.Max.Y) / float(GroupSize.Y)) * GroupSize.Y;
	
	FIntPoint RectResolution(TotalRect.Width(), TotalRect.Height());
	Parameters->ResolutionOffset = FIntPoint(TotalRect.Min.X, TotalRect.Min.Y);

	TShaderMapRef<FHairVisibilityPrimitiveIdCompactionCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisibilityCompaction"),
		*ComputeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(RectResolution, GroupSize));

	OutIndirectArgsBuffer = AddCopyIndirectArgPass(GraphBuilder, &View, NodeGroupSize, CompactCounter);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityFillOpaqueDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityFillOpaqueDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityFillOpaqueDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisibilityDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisibilityIDTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityFillOpaqueDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityFillOpaqueDepthPS.usf", "MainPS", SF_Pixel);

static FRDGTextureRef AddHairVisibilityFillOpaqueDepth(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FIntPoint& Resolution,
	const FHairStrandsClusterDatas& ClusterDatas,
	const FRDGTextureRef& SceneDepthTexture)
{
	FRDGTextureRef OutVisibilityDepthTexture;
	{
		const uint32 MSAASampleCount = FMath::RoundUpToPowerOfTwo(FMath::Clamp(GHairVisibilitySampleCount, 1, 16));

		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_DepthStencil;
		Desc.NumMips = 1;
		Desc.NumSamples = MSAASampleCount;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_DepthStencilTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding::DepthFar;
		Desc.bForceSharedTargetAndShaderResource = true;
		OutVisibilityDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityDepthTexture"));
	}

	FHairVisibilityFillOpaqueDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityFillOpaqueDepthPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneDepthTexture = SceneDepthTexture;
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutVisibilityDepthTexture,
		ERenderTargetLoadAction::EClear,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityFillOpaqueDepthPS> PixelShader(View.ShaderMap);
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	TArray<FIntRect> ClusterRects;
	for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
	{
		ClusterRects.Add(ClusterData.ScreenRect);
	}

	{
		ClearUnusedGraphResources(*PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityFillOpaqueDepth"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, ClusterRects, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);

			for (const FIntRect& ViewRect : ClusterRects)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);
				DrawRectangle(
					RHICmdList,
					0, 0,
					Viewport.Width(), Viewport.Height(),
					Viewport.Min.X,   Viewport.Min.Y,
					Viewport.Width(), Viewport.Height(),
					Viewport.Size(),
					Resolution,
					*VertexShader,
					EDRF_UseTriangleOptimization);
			}
		});
	}


	return OutVisibilityDepthTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FVisibilityPassParameters
{
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

template <typename TPassParameters>
static void AddHairVisibilityCommonPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterDatas& ClusterDatas,
	const EHairVisibilityRenderMode RenderMode,
	TPassParameters* PassParameters)
{
	GraphBuilder.AddPass(
		RenderMode == HairVisibilityRenderMode_MSAA ? RDG_EVENT_NAME("HairStrandsVisibilityPass") : RDG_EVENT_NAME("HairStrandsCoveragePass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &ClusterDatas, RenderMode](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		const FIntPoint Resolution(ViewInfo->ViewRect.Width(), ViewInfo->ViewRect.Height());
		TUniformBufferRef<FHairVisibilityPassUniformParameters> PassUniformBuffer;
		{
			FVector2D PixelVelocity(1.f / (Resolution.X * 2), 1.f / (Resolution.Y * 2));
			const float VelocityMagnitudeScale = FMath::Clamp(GHairVelocityMagnitudeScale, 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);

			// Set the sample count to one as we want the size of the pixel
			const uint32 HairVisibilitySampleCount = RenderMode == HairVisibilityRenderMode_MSAA ? GetHairVisibilitySampleCount() : 1;
			const float RasterizationScaleOverride = RenderMode == HairVisibilityRenderMode_MSAA ? 0 : 1.35f;
			FHairVisibilityPassUniformParameters PassUniformParameters;
			FMinHairRadiusAtDepth1 MinHairRadius = ComputeMinStrandRadiusAtDepth1(FIntPoint(ViewInfo->UnconstrainedViewRect.Width(), ViewInfo->UnconstrainedViewRect.Height()), ViewInfo->FOV, HairVisibilitySampleCount, RasterizationScaleOverride);
			PassUniformParameters.MinStrandRadius_Primary = MinHairRadius.Primary;
			PassUniformParameters.MinStrandRadius_Velocity = MinHairRadius.Velocity;
			PassUniformParameters.HairStrandsVelocityScale = VelocityMagnitudeScale;
			PassUniformBuffer = CreateUniformBufferImmediate(PassUniformParameters, UniformBuffer_SingleDraw, EUniformBufferValidation::None);
		}

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, PassUniformBuffer);
		{
			RHICmdList.SetViewport(0, 0, 0.0f, Resolution.X, Resolution.Y, 1.0f);
			if (RenderMode == HairVisibilityRenderMode_MSAA)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_Coverage)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RED, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}

			FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
			FMeshCommandOneFrameArray VisibleMeshDrawCommands;
			FGraphicsMinimalPipelineStateSet PipelineStateSet;
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet);
			FHairVisibilityProcessor MeshProcessor(Scene, ViewInfo /* is a SceneView */, DrawRenderState, RenderMode, &ShadowContext);

			for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
			{
				for (const FMeshBatchAndRelevance& MeshBatchAndRelevance : ClusterData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, MeshBatchAndRelevance.PrimitiveSceneProxy, -1, ClusterData.ClusterId);
				}
			}

			if (VisibleMeshDrawCommands.Num() > 0)
			{
				FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
				SortAndMergeDynamicPassMeshDrawCommands(ViewInfo->GetFeatureLevel(), VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
				SubmitMeshDrawCommands(VisibleMeshDrawCommands, PipelineStateSet, PrimitiveIdVertexBuffer, 0, false, 1, RHICmdList);
			}
		}
	});
}

static void AddHairVisibilityMSAAPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterDatas& ClusterDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef& VisibilityIdTexture,
	FRDGTextureRef& VisibilityMaterialTexture,
	FRDGTextureRef& VisibilityAttributeTexture,
	FRDGTextureRef& VisibilityVelocityTexture,
	FRDGTextureRef& VisibilityDepthTexture)
{
	const uint32 MSAASampleCount = FMath::RoundUpToPowerOfTwo(FMath::Clamp(GHairVisibilitySampleCount, 1, 16));

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R16G16B16A16_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = MSAASampleCount;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.bForceSharedTargetAndShaderResource = true;
		VisibilityIdTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityIDTexture"));
	}

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R8G8B8A8;
		Desc.NumMips = 1;
		Desc.NumSamples = MSAASampleCount;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));
		Desc.bForceSharedTargetAndShaderResource = true;
		VisibilityMaterialTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityMaterialTexture"));
	}

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R8G8B8A8;
		Desc.NumMips = 1;
		Desc.NumSamples = MSAASampleCount;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));
		Desc.bForceSharedTargetAndShaderResource = true;
		VisibilityAttributeTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityAttributeTexture"));
	}

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_G16R16;
		Desc.NumMips = 1;
		Desc.NumSamples = MSAASampleCount;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));
		Desc.bForceSharedTargetAndShaderResource = true;
		VisibilityVelocityTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityVelocityTexture"));
	}
	AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAIdTexture"), ViewInfo, 0xFFFFFFFF, VisibilityIdTexture);

	// Manually clear RTs as using the Clear action on the RT, issue a global clean on all targets, while still need a special clear 
	// for the PrimitiveId buffer
	// const ERenderTargetLoadAction LoadAction = GHairClearVisibilityBuffer ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;
	if (GHairClearVisibilityBuffer)
	{
		LoadAction = ERenderTargetLoadAction::ELoad;
		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAMaterial"), ViewInfo, 0, VisibilityMaterialTexture);
		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAAttribute"), ViewInfo, 0, VisibilityAttributeTexture);
		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAVelocity"), ViewInfo, 0, VisibilityVelocityTexture);
	}

	FVisibilityPassParameters::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(VisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(VisibilityMaterialTexture,  LoadAction, 0);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(VisibilityAttributeTexture, LoadAction, 0);
	PassParameters->RenderTargets[3] = FRenderTargetBinding(VisibilityVelocityTexture,  LoadAction, 0);

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		VisibilityDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthWrite_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, ClusterDatas, HairVisibilityRenderMode_MSAA, PassParameters);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static FRDGTextureRef AddHairCoveragePass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterDatas& ClusterDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef SceneDepthTexture)
{
	check(SceneDepthTexture->Desc.Extent == Resolution);

	FRDGTextureRef CoverageTexture = nullptr;
	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R32_FLOAT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.bForceSharedTargetAndShaderResource = true;
		CoverageTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairCoverageTexture"));
	}

	FVisibilityPassParameters::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(CoverageTexture, ERenderTargetLoadAction::EClear, 0);

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		SceneDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, ClusterDatas, HairVisibilityRenderMode_Coverage, PassParameters);

	return CoverageTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityDepthTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityDepthPS.usf", "MainPS", SF_Pixel);

static void AddHairVisibilityColorAndDepthPatchPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& VisibilityDepthTexture,
	FRDGTextureRef& OutGBufferBTexture,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutDepthTexture)
{
	FHairVisibilityDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityDepthPS::FParameters>();
	Parameters->HairVisibilityDepthTexture = VisibilityDepthTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutGBufferBTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[1] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityDepthPS> PixelShader(View.ShaderMap);
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(*PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityWriteColorAndDepth"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Greater>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsVisibilityViews RenderHairStrandsVisibilityBuffer(
	FRHICommandListImmediate& RHICmdList, 
	const FScene* Scene, 
	const TArray<FViewInfo>& Views, 
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferBTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneColorTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneDepthTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneVelocityTexture,
	const FHairStrandsClusterViews& ClusterViews)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CLM_RenderHairStrandsVisibility);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsVisibility);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsVisibility);

	FHairStrandsVisibilityViews Output;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			FHairStrandsVisibilityData& VisibilityData = Output.HairDatas.AddDefaulted_GetRef();
			VisibilityData.NodeGroupSize = GetVendorOptimalGroupSize1D();
			const FHairStrandsClusterDatas& ClusterDatas = ClusterViews.Views[ViewIndex];

			if (ClusterDatas.Datas.Num() == 0)
				continue;

			// Use the scene color for computing target resolution as the View.ViewRect, 
			// doesn't include the actual resolution padding which make buffer size 
			// mismatch, and create artifact (e.g. velocity computation)
			const FIntPoint Resolution = InSceneColorTexture->GetDesc().Extent;


			FRDGBuilder GraphBuilder(RHICmdList);
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			FRDGTextureRef SceneGBufferBTexture = GraphBuilder.RegisterExternalTexture(InSceneGBufferBTexture, TEXT("SceneGBufferBTexture"));
			FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(InSceneColorTexture, TEXT("SceneColorTexture"));
			FRDGTextureRef SceneDepthTexture = GraphBuilder.RegisterExternalTexture(InSceneDepthTexture, TEXT("SceneDepthTexture"));
			FRDGTextureRef SceneVelocityTexture = InSceneVelocityTexture ? GraphBuilder.RegisterExternalTexture(InSceneVelocityTexture, TEXT("SceneVelocityTexture")) : nullptr;
			
			const EHairVisibilityRenderMode RenderMode = GetHairVisibilityRenderMode();

			struct FRDGMsaaVisibilityResources
			{
				FRDGTextureRef DepthTexture;
				FRDGTextureRef IdTexture;
				FRDGTextureRef MaterialTexture;
				FRDGTextureRef AttributeTexture;
				FRDGTextureRef VelocityTexture;
			} MsaaVisibilityResources;

			FRDGTextureRef CoverageTexture = nullptr;
			if (GHairStrandsCoveragePassEnable > 0)
			{
				CoverageTexture = AddHairCoveragePass(
					GraphBuilder,
					Scene,
					&View,
					ClusterDatas,
					Resolution, 
					SceneDepthTexture);
			}

			if (RenderMode == HairVisibilityRenderMode_MSAA)
			{
				MsaaVisibilityResources.DepthTexture = AddHairVisibilityFillOpaqueDepth(
					GraphBuilder,
					View,
					Resolution,
					ClusterDatas,
					SceneDepthTexture);

				AddHairVisibilityMSAAPass(
					GraphBuilder, 
					Scene, 
					&View, 
					ClusterDatas, 
					Resolution,
					MsaaVisibilityResources.IdTexture, 
					MsaaVisibilityResources.MaterialTexture, 
					MsaaVisibilityResources.AttributeTexture,
					MsaaVisibilityResources.VelocityTexture,
					MsaaVisibilityResources.DepthTexture); 

				// This is used when compaction is not enabled.
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.IdTexture, &VisibilityData.IDTexture);
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.MaterialTexture, &VisibilityData.MaterialTexture);
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.AttributeTexture, &VisibilityData.AttributeTexture);
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.VelocityTexture, &VisibilityData.VelocityTexture);
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.DepthTexture, &VisibilityData.DepthTexture);
				
				{
					FRDGTextureRef CompactNodeIndex;
					FRDGBufferRef CompactNodeData;
					FRDGBufferRef CompactNodeCoord;
					FRDGTextureRef CategorizationTexture;
					FRDGBufferRef IndirectArgsBuffer;

					AddHairVisibilityPrimitiveIdCompactionPass(
						GraphBuilder,
						View,
						ClusterDatas,
						VisibilityData.NodeGroupSize,
						MsaaVisibilityResources.DepthTexture,
						MsaaVisibilityResources.IdTexture,
						MsaaVisibilityResources.MaterialTexture,
						MsaaVisibilityResources.AttributeTexture,
						MsaaVisibilityResources.VelocityTexture,
						CoverageTexture,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer);
					GraphBuilder.QueueTextureExtraction(CompactNodeIndex,		&VisibilityData.NodeIndex);
					GraphBuilder.QueueTextureExtraction(CategorizationTexture,	&VisibilityData.CategorizationTexture);
					GraphBuilder.QueueBufferExtraction(CompactNodeData,			&VisibilityData.NodeData,					FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(CompactNodeCoord,		&VisibilityData.NodeCoord,					FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer,		&VisibilityData.NodeIndirectArg,			FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
				}
			}
		
			// For fully covered pixels, write: 
			// * black color into the scene color
			// * closest depth
			// * unlit shading model ID 
			AddHairVisibilityColorAndDepthPatchPass(
				GraphBuilder,
				View,
				MsaaVisibilityResources.DepthTexture,
				SceneGBufferBTexture,
				SceneColorTexture,
				SceneDepthTexture);

			GraphBuilder.Execute();

			// #hair_todo: is there a better way to get SRV view of a RDG buffer? should work as long as there is not reuse between the pass
			if (VisibilityData.NodeData)
				VisibilityData.NodeDataSRV = RHICreateShaderResourceView(VisibilityData.NodeData->StructuredBuffer);

			if (VisibilityData.NodeCoord)
				VisibilityData.NodeCoordSRV = RHICreateShaderResourceView(VisibilityData.NodeCoord->StructuredBuffer);
		}
	}

	return Output;
}
