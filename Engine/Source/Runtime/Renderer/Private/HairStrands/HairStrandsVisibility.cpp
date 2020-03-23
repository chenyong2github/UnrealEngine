// Copyright Epic Games, Inc. All Rights Reserved.

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

static int32 GHairStrandsViewTransmittancePassEnable = 1;
static FAutoConsoleVariableRef CVarHairStrandsCoveragePassEnable(TEXT("r.HairStrands.ViewTransmittancePass"), GHairStrandsViewTransmittancePassEnable, TEXT("Enable accurate transmittance pass for better rendering of small scale hair strand."));

static int32 GHairStrandsMaterialCompactionEnable = 0;
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionEnable(TEXT("r.HairStrands.MaterialCompaction"), GHairStrandsMaterialCompactionEnable, TEXT("Enable extra compaction based on material properties in order to reduce sample per pixel and improve performance."));

static float GHairStrandsMaterialCompactionDepthThreshold = 1.f;
static float GHairStrandsMaterialCompactionTangentThreshold = 10.f;
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionDepthThreshold(TEXT("r.HairStrands.MaterialCompaction.DepthThreshold"), GHairStrandsMaterialCompactionDepthThreshold, TEXT("Compaction threshold for depth value for material compaction (in centimeters). Default 1 cm."));
static FAutoConsoleVariableRef CVarHairStrandsMaterialCompactionTangentThreshold(TEXT("r.HairStrands.MaterialCompaction.TangentThreshold"), GHairStrandsMaterialCompactionTangentThreshold, TEXT("Compaciton threshold for tangent value for material compaction (in degrees). Default 10 deg."));

static int32 GHairVisibilitySampleCount = 8;
static FAutoConsoleVariableRef CVarHairVisibilitySampleCount(TEXT("r.HairStrands.VisibilitySampleCount"), GHairVisibilitySampleCount, TEXT("Hair strands visibility sample count (4 or 8)"));

static int32 GHairClearVisibilityBuffer = 0;
static FAutoConsoleVariableRef CVarHairClearVisibilityBuffer(TEXT("r.HairStrands.VisibilityClear"), GHairClearVisibilityBuffer, TEXT("Clear hair strands visibility buffer"));

static TAutoConsoleVariable<int32> CVarHairVelocityMagnitudeScale(
	TEXT("r.HairStrands.VelocityMagnitudeScale"),
	100,  // Tuned by eye, based on heavy motion (strong head shack)
	TEXT("Velocity magnitude (in pixel) at which a hair will reach its pic velocity-rasterization-scale under motion to reduce aliasing. Default is 100."));

static int32 GHairVelocityType = 1; // default is 
static FAutoConsoleVariableRef CVarHairVelocityType(TEXT("r.HairStrands.VelocityType"), GHairVelocityType, TEXT("Type of velocity filtering (0:avg, 1:closest, 2:max). Default is 1."));

static int32 GHairVisibilityPPLL = 0;
static FAutoConsoleVariableRef CVarGHairVisibilityPPLL(TEXT("r.HairStrands.VisibilityPPLL"), GHairVisibilityPPLL, TEXT("Hair Visibility uses per pixel linked list"), ECVF_Scalability | ECVF_RenderThreadSafe);
static int32 GHairVisibilityPPLLMeanListElementCountPerPixel = 16;
static FAutoConsoleVariableRef CVarGHairVisibilityPPLLMeanListElementCountPerPixel(TEXT("r.HairStrands.VisibilityPPLLMeanListElementCountPerPixel"), GHairVisibilityPPLLMeanListElementCountPerPixel, TEXT("The mean maximum number of node allowed for all linked list element. It will be width*height*VisibilityPPLLMeanListElementCountPerPixel."));
static int32 GHairVisibilityPPLLMaxRenderNodePerPixel = 16;
static FAutoConsoleVariableRef CVarGHairVisibilityPPLLMeanNodeCountPerPixel(TEXT("r.HairStrands.VisibilityPPLLMaxRenderNodePerPixel"), GHairVisibilityPPLLMaxRenderNodePerPixel, TEXT("The maximum number of node allowed to be independently shaded and composited per pixel. Total amount of node will be width*height*VisibilityPPLLMaxRenderNodePerPixel. The last node is used to aggregate all furthest strands to shade into a single one."));

static int32 GHairStrandsVisibilityMaterialPass = 1;
static FAutoConsoleVariableRef CVarHairStrandsVisibilityMaterialPass(TEXT("r.HairStrands.Visibility.MaterialPass"), GHairStrandsVisibilityMaterialPass, TEXT("Enable the deferred material pass evaluation after the hair visibility is resolved."));

static float GHairStrandsViewHairCountDepthDistanceThreshold = 30.f;
static FAutoConsoleVariableRef CVarHairStrandsViewHairCountDepthDistanceThreshold(TEXT("r.HairStrands.Visibility.HairCount.DistanceThreshold"), GHairStrandsViewHairCountDepthDistanceThreshold, TEXT("Distance threshold defining if opaque depth get injected into the 'view-hair-count' buffer."));

static int32 GHairStrandsVisibilityComputeRaster = 0;
static int32 GHairStrandsVisibilityComputeRasterMaxPixelCount = 8;
static int32 GHairStrandsVisibilityComputeRasterSampleCount = 1;
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRaster(TEXT("r.HairStrands.Visibility.ComputeRaster"), GHairStrandsVisibilityComputeRaster, TEXT("Define the maximal length rasterize in compute."));
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRasterMaxPixelCount(TEXT("r.HairStrands.Visibility.ComputeRaster.MaxPixelCount"), GHairStrandsVisibilityComputeRasterMaxPixelCount, TEXT("Define the maximal length rasterize in compute."));
static FAutoConsoleVariableRef CVarHairStrandsVisibilityComputeRasterSampleCount(TEXT("r.HairStrands.Visibility.ComputeRaster.SampleCount"), GHairStrandsVisibilityComputeRasterSampleCount, TEXT("Define sample count used in rasterize in compute."));

static float GHairStrandsFullCoverageThreshold = 0.98f;
static FAutoConsoleVariableRef CVarHairStrandsFullCoverageThreshold(TEXT("r.HairStrands.Visibility.FullCoverageThreshold"), GHairStrandsFullCoverageThreshold, TEXT("Define the coverage threshold at which a pixel is considered fully covered."));

static int32 GHairStrandsSortHairSampleByDepth = 0;
static FAutoConsoleVariableRef CVarHairStrandsSortHairSampleByDepth(TEXT("r.HairStrands.Visibility.SortByDepth"), GHairStrandsSortHairSampleByDepth, TEXT("Sort hair fragment by depth and update their coverage based on ordered transmittance."));

static int32 GHairStrandsHairCountToTransmittance = 0;
static FAutoConsoleVariableRef CVarHairStrandsHairCountToTransmittance(TEXT("r.HairStrands.Visibility.UseCoverageMappping"), GHairStrandsHairCountToTransmittance, TEXT("Use hair count to coverage transfer function."));

/////////////////////////////////////////////////////////////////////////////////////////

namespace HairStrandsVisibilityInternal
{
	struct NodeData
	{
		uint32 Depth;
		uint32 PrimitiveId_MacroGroupId;
		uint32 Tangent_Coverage;
		uint32 BaseColor_Roughness;
		uint32 Specular;
	};

	// 128 bit alignment
	struct NodeVis
	{
		uint32 Depth;
		uint32 PrimitiveId_MacroGroupId;
		uint32 Coverage_MacroGroupId_Pad;
		uint32 Pad;
	};
}

enum EHairVisibilityRenderMode
{
	HairVisibilityRenderMode_MSAA,
	HairVisibilityRenderMode_Transmittance,
	HairVisibilityRenderMode_PPLL,
	HairVisibilityRenderMode_MSAA_Visibility,
	HairVisibilityRenderMode_TransmittanceAndHairCount,
	HairVisibilityRenderModeCount
};

static EHairVisibilityRenderMode GetHairVisibilityRenderMode()
{
	return GHairVisibilityPPLL > 0 ? HairVisibilityRenderMode_PPLL : HairVisibilityRenderMode_MSAA;
}

static uint32 GetPPLLMeanListElementCountPerPixel()
{
	return GHairVisibilityPPLLMeanListElementCountPerPixel;
}

static uint32 GetPPLLMaxTotalListElementCount(FIntPoint Resolution)
{
	return Resolution.X * Resolution.Y * GetPPLLMeanListElementCountPerPixel();
}

static uint32 GetPPLLMaxRenderNodePerPixel()
{
	// The following must match the FPPLL permutation of FHairVisibilityPrimitiveIdCompactionCS.
	if (GHairVisibilityPPLLMaxRenderNodePerPixel == 0)
	{
		return 0;
	}
	else if (GHairVisibilityPPLLMaxRenderNodePerPixel <= 8)
	{
		return 8;
	}
	else if (GHairVisibilityPPLLMaxRenderNodePerPixel <= 16)
	{
		return 16;
	}
	else //if (GHairVisibilityPPLLMaxRenderNodePerPixel <= 32)
	{
		return 32;
	}
	// If more is needed: please check out EncodeNodeDesc from HairStrandsVisibilityCommon.ush to verify node count representation limitations.
}

static uint32 GetMSAASampleCount()
{
	// Only support 4 or 8 at the moment
	check(GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA);
	return GHairVisibilitySampleCount <= 4 ? 4 : 8;
}

static void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, bool bEnableMSAA, FVector4& OutHairRenderInfo, uint32& OutHairRenderInfoBits)
{
	FVector2D PixelVelocity(1.f / (ViewInfo.ViewRect.Width() * 2), 1.f / (ViewInfo.ViewRect.Height() * 2));
	const float VelocityMagnitudeScale = FMath::Clamp(CVarHairVelocityMagnitudeScale.GetValueOnAnyThread(), 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);

	// In the case we render coverage, we need to override some view uniform shader parameters to account for the change in MSAA sample count.
	const uint32 HairVisibilitySampleCount = bEnableMSAA ? GetMSAASampleCount() : 1;	// The coverage pass does not use MSAA
	const float RasterizationScaleOverride = 0.0f;	// no override
	FMinHairRadiusAtDepth1 MinHairRadius = ComputeMinStrandRadiusAtDepth1(
		FIntPoint(ViewInfo.UnconstrainedViewRect.Width(), ViewInfo.UnconstrainedViewRect.Height()), ViewInfo.FOV, HairVisibilitySampleCount, RasterizationScaleOverride);

	OutHairRenderInfo = PackHairRenderInfo(MinHairRadius.Primary, MinHairRadius.Stable, MinHairRadius.Velocity, VelocityMagnitudeScale);
	OutHairRenderInfoBits = PackHairRenderInfoBits(!ViewInfo.IsPerspectiveProjection(), false);
}

void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, FVector4& OutHairRenderInfo, uint32& OutHairRenderInfoBits)
{
	const bool bMsaaEnable = GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA;
	SetUpViewHairRenderInfo(ViewInfo, bMsaaEnable, OutHairRenderInfo, OutHairRenderInfoBits);
}

static bool IsCompatibleWithHairVisibility(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	return IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters);
}


///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairLightSampleClearVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLightSampleClearVS);
	SHADER_USE_PARAMETER_STRUCT(FHairLightSampleClearVS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, MaxViewportResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairNodeCountTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_VERTEX"), 1);
	}
};

class FHairLightSampleClearPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLightSampleClearPS);
	SHADER_USE_PARAMETER_STRUCT(FHairLightSampleClearPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, MaxViewportResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairNodeCountTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEAR"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairLightSampleClearVS, "/Engine/Private/HairStrands/HairStrandsLightSample.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairLightSampleClearPS, "/Engine/Private/HairStrands/HairStrandsLightSample.usf", "ClearPS", SF_Pixel);

static FRDGTextureRef AddClearLightSamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const uint32 MaxNodeCount,
	const FRDGTextureRef NodeCounter)
{	
	const uint32 SampleTextureResolution = FMath::CeilToInt(FMath::Sqrt(MaxNodeCount));
	FRDGTextureDesc Desc;
	Desc.Extent.X = SampleTextureResolution;
	Desc.Extent.Y = SampleTextureResolution;
	Desc.Depth = 0;
	Desc.Format = PF_FloatRGBA;
	Desc.NumMips = 1;
	Desc.Flags = 0;
	Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable;
	FRDGTextureRef Output = GraphBuilder.CreateTexture(Desc, TEXT("HairLightSample"));

	FHairLightSampleClearPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FHairLightSampleClearPS::FParameters>();
	ParametersPS->MaxViewportResolution = Desc.Extent;
	ParametersPS->HairNodeCountTexture = NodeCounter;
	
	const FIntPoint ViewportResolution = Desc.Extent;
	TShaderMapRef<FHairLightSampleClearVS> VertexShader(View->ShaderMap);
	TShaderMapRef<FHairLightSampleClearPS> PixelShader(View->ShaderMap);

	ParametersPS->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ENoAction);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairLightSampleClearPS"),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, ViewportResolution](FRHICommandList& RHICmdList)
	{
		FHairLightSampleClearVS::FParameters ParametersVS;
		ParametersVS.MaxViewportResolution = ParametersPS->MaxViewportResolution;
		ParametersVS.HairNodeCountTexture = ParametersPS->HairNodeCountTexture;

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

		RHICmdList.SetViewport(0, 0, 0.0f, ViewportResolution.X, ViewportResolution.Y, 1.0f);
		RHICmdList.SetStreamSource(0, nullptr, 0);
		RHICmdList.DrawPrimitive(0, 1, 1);
	});

	return Output;
}

/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialPassParameters, )
	SHADER_PARAMETER(FIntPoint, MaxResolution)
	SHADER_PARAMETER(uint32, MaxSampleCount)
	SHADER_PARAMETER(uint32, NodeGroupSize)
	SHADER_PARAMETER(uint32, bUpdateSampleCoverage)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, NodeIndex)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, NodeCoord)
	SHADER_PARAMETER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndirectArgs)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeData)
	SHADER_PARAMETER_UAV(RWBuffer<float2>, OutNodeVelocity)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialPassParameters, "MaterialPassParameters");

class FHairMaterialVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialVS, MeshMaterial);

protected:
	FHairMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMaterialPassParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FHairMaterialVS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairVisibility(Parameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHairMaterialVS, TEXT("/Engine/Private/HairStrands/HairStrandsMaterialVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairMaterialShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairMaterialShaderElementData(int32 MacroGroupId, int32 MaterialId, int32 PrimitiveId, uint32 LightChannelMask) : MaterialPass_MacroGroupId(MacroGroupId), MaterialPass_MaterialId(MaterialId), MaterialPass_PrimitiveId(PrimitiveId), MaterialPass_LightChannelMask(LightChannelMask){ }
	uint32 MaterialPass_MacroGroupId;
	uint32 MaterialPass_MaterialId;
	uint32 MaterialPass_PrimitiveId;
	uint32 MaterialPass_LightChannelMask;
};

class FHairMaterialPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairMaterialPS, MeshMaterial);

public:
	FHairMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FMaterialPassParameters::StaticStructMetadata.GetShaderVariableName());
		MaterialPass_MacroGroupId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MacroGroupId"));
		MaterialPass_MaterialId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MaterialId"));
		MaterialPass_PrimitiveId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_PrimitiveId"));
		MaterialPass_LightChannelMask.Bind(Initializer.ParameterMap, TEXT("MaterialPass_LightChannelMask"));
	}

	FHairMaterialPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FHairMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
		ShaderBindings.Add(MaterialPass_MacroGroupId, ShaderElementData.MaterialPass_MacroGroupId);
		ShaderBindings.Add(MaterialPass_MaterialId, ShaderElementData.MaterialPass_MaterialId);
		ShaderBindings.Add(MaterialPass_PrimitiveId, ShaderElementData.MaterialPass_PrimitiveId);
		ShaderBindings.Add(MaterialPass_LightChannelMask, ShaderElementData.MaterialPass_LightChannelMask);
	}

private:
	LAYOUT_FIELD(FShaderParameter, MaterialPass_MacroGroupId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_MaterialId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_PrimitiveId);
	LAYOUT_FIELD(FShaderParameter, MaterialPass_LightChannelMask);
};
IMPLEMENT_MATERIAL_SHADER_TYPE(, FHairMaterialPS, TEXT("/Engine/Private/HairStrands/HairStrandsMaterialPS.usf"), TEXT("Main"), SF_Pixel);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairMaterialProcessor : public FMeshPassProcessor
{
public:
	FHairMaterialProcessor(
		const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		FDynamicPassMeshDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 MacroGroupId, int32 HairMaterialId);

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const int32 MacroGroupId,
		const int32 HairMaterialId,
		const int32 HairPrimitiveId,
		const uint32 HairPrimitiveLightChannelMask);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairMaterialProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0);
}

void FHairMaterialProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 MacroGroupId, int32 HairMaterialId)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num()>0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());

	if (bIsCompatible
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		// For the mesh patch to be rendered a single triangle triangle to spawn the necessary amount of thread
		FMeshBatch MeshBatchCopy = MeshBatch;
		for (uint32 ElementIt = 0, ElementCount=uint32(MeshBatch.Elements.Num()); ElementIt < ElementCount; ++ElementIt)
		{
			MeshBatchCopy.Elements[ElementIt].FirstIndex = 0;
			MeshBatchCopy.Elements[ElementIt].NumPrimitives = 1;
			MeshBatchCopy.Elements[ElementIt].NumInstances = 1;
			MeshBatchCopy.Elements[ElementIt].IndirectArgsBuffer = nullptr;
			MeshBatchCopy.Elements[ElementIt].IndirectArgsOffset = 0;
		}

		int32 PrimitiveId = 0;
		int32 ScenePrimitiveId = 0;
		FPrimitiveSceneInfo* SceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
		GetDrawCommandPrimitiveId(SceneInfo, MeshBatch.Elements[0], PrimitiveId, ScenePrimitiveId);
		uint32 LightChannelMask = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetLightingChannelMask() : 0;

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatchCopy, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, MacroGroupId, HairMaterialId, PrimitiveId, LightChannelMask);
	}
}

void FHairMaterialProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const int32 MacroGroupId,
	const int32 HairMaterialId,
	const int32 HairPrimitiveId,
	const uint32 HairPrimitiveLightChannelMask)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHairMaterialVS,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FHairMaterialPS> PassShaders;
	{
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.VertexShader = MaterialResource.GetShader<FHairMaterialVS>(VertexFactoryType);
		PassShaders.PixelShader = MaterialResource.GetShader<FHairMaterialPS>(VertexFactoryType);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	FHairMaterialShaderElementData ShaderElementData(MacroGroupId, HairMaterialId, HairPrimitiveId, HairPrimitiveLightChannelMask);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		PassShaders,
		ERasterizerFillMode::FM_Solid,
		ERasterizerCullMode::CM_CCW,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

FHairMaterialProcessor::FHairMaterialProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	FDynamicPassMeshDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityMaterialPassParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NodeIndex)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NodeCoord)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float2>, OutNodeVelocity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

///////////////////////////////////////////////////////////////////////////////////////////////////
// Patch sample coverage
class FUpdateSampleCoverageCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FUpdateSampleCoverageCS);
	SHADER_USE_PARAMETER_STRUCT(FUpdateSampleCoverageCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NodeIndexAndOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedHairSample>,  InNodeDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPackedHairSample>, OutNodeDataBuffer)
	END_SHADER_PARAMETER_STRUCT()
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FUpdateSampleCoverageCS, "/Engine/Private/HairStrands/HairStrandsVisibilityComputeSampleCoverage.usf", "MainCS", SF_Compute);

static FRDGBufferRef AddUpdateSampleCoveragePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const FRDGTextureRef NodeIndexAndOffset,
	const FRDGBufferRef InNodeDataBuffer)
{
	FRDGBufferRef OutNodeDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(InNodeDataBuffer->Desc.BytesPerElement, InNodeDataBuffer->Desc.NumElements), TEXT("HairCompactNodeData"));

	FUpdateSampleCoverageCS::FParameters* Parameters = GraphBuilder.AllocParameters<FUpdateSampleCoverageCS::FParameters>();
	Parameters->Resolution = NodeIndexAndOffset->Desc.Extent;
	Parameters->NodeIndexAndOffset = NodeIndexAndOffset;
	Parameters->InNodeDataBuffer = GraphBuilder.CreateSRV(InNodeDataBuffer);
	Parameters->OutNodeDataBuffer = GraphBuilder.CreateUAV(OutNodeDataBuffer);

	TShaderMapRef<FUpdateSampleCoverageCS> ComputeShader(View->ShaderMap);

	// Add 64 threads permutation
	const uint32 GroupSizeX = 8;
	const uint32 GroupSizeY = 4;
	const FIntVector DispatchCount = FIntVector(
		(Parameters->Resolution.X + GroupSizeX-1) / GroupSizeX, 
		(Parameters->Resolution.Y + GroupSizeY-1) / GroupSizeY, 
		1);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisbilityUpdateCoverage"),
		ComputeShader,
		Parameters,
		DispatchCount);

	return OutNodeDataBuffer;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
struct FMaterialPassOutput
{
	static const EPixelFormat VelocityFormat = PF_G16R16;
	FRDGBufferRef NodeData = nullptr;
	FRDGBufferRef NodeVelocity = nullptr;
};

static FMaterialPassOutput AddHairMaterialPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const bool bUpdateSampleCoverage,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const uint32 NodeGroupSize,
	FRDGTextureRef CompactNodeIndex,
	FRDGBufferRef CompactNodeVis,
	FRDGBufferRef CompactNodeCoord,
	FRDGBufferRef IndirectArgBuffer)
{
	if (!CompactNodeVis || !CompactNodeIndex)
		return FMaterialPassOutput();

	const uint32 MaxNodeCount = CompactNodeVis->Desc.NumElements;

	FMaterialPassOutput Output;
	Output.NodeData		= GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(HairStrandsVisibilityInternal::NodeData), MaxNodeCount), TEXT("HairCompactNodeData"));
	Output.NodeVelocity = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, CompactNodeVis->Desc.NumElements), TEXT("HairCompactNodeVelocity"));

	const uint32 ResolutionDim = FMath::CeilToInt(FMath::Sqrt(MaxNodeCount));
	const FIntPoint Resolution(ResolutionDim, ResolutionDim);

	FRDGTextureDesc OutputDesc;
	OutputDesc.Extent.X = Resolution.X;
	OutputDesc.Extent.Y = Resolution.Y;
	OutputDesc.Depth = 0;
	OutputDesc.Format = PF_FloatRGBA;
	OutputDesc.NumMips = 1;
	OutputDesc.Flags = 0;
	OutputDesc.TargetableFlags = TexCreate_RenderTargetable;
	FRDGTextureRef OutDummyTexture0 = GraphBuilder.CreateTexture(OutputDesc, TEXT("HairMaterialDummyOutput"));


	// Add resources reference to the pass parameters, in order to get the resource lifetime extended to this pass
	FVisibilityMaterialPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityMaterialPassParameters>();
	PassParameters->NodeIndex		= CompactNodeIndex;
	PassParameters->NodeVis			= GraphBuilder.CreateSRV(CompactNodeVis);
	PassParameters->NodeCoord		= GraphBuilder.CreateSRV(CompactNodeCoord);
	PassParameters->IndirectArgs	= GraphBuilder.CreateSRV(IndirectArgBuffer);
	PassParameters->OutNodeData		= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.NodeData));
	PassParameters->OutNodeVelocity	= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.NodeVelocity, FMaterialPassOutput::VelocityFormat));
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutDummyTexture0, ERenderTargetLoadAction::EClear, 0);


	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMaterialPass"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, MaxNodeCount, Resolution, NodeGroupSize, bUpdateSampleCoverage](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FMaterialPassParameters MaterialPassParameters;
		MaterialPassParameters.bUpdateSampleCoverage = bUpdateSampleCoverage ? 1 : 0;
		MaterialPassParameters.MaxResolution	= Resolution;
		MaterialPassParameters.NodeGroupSize	= NodeGroupSize;
		MaterialPassParameters.MaxSampleCount	= MaxNodeCount;
		MaterialPassParameters.NodeIndex		= PassParameters->NodeIndex->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
		MaterialPassParameters.NodeCoord		= PassParameters->NodeCoord->GetRHI();
		MaterialPassParameters.NodeVis			= PassParameters->NodeVis->GetRHI();
		MaterialPassParameters.IndirectArgs		= PassParameters->IndirectArgs->GetRHI();
		MaterialPassParameters.OutNodeData 		= PassParameters->OutNodeData->GetRHI();
		MaterialPassParameters.OutNodeVelocity 	= PassParameters->OutNodeVelocity->GetRHI();
		TUniformBufferRef<FMaterialPassParameters> MaterialPassParametersBuffer = TUniformBufferRef<FMaterialPassParameters>::CreateUniformBufferImmediate(MaterialPassParameters, UniformBuffer_SingleFrame);

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, MaterialPassParametersBuffer);
		// Note: this reference needs to persistent until SubmitMeshDrawCommands() is called, as DrawRenderState does not ref count 
		// the view uniform buffer (raw pointer). It is only within the MeshProcessor that the uniform buffer get reference
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
		{
			const bool bEnableMSAA = false;
			SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits);
			ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
			DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);
		}

		{
			RHICmdList.SetViewport(0, 0, 0.0f, Resolution.X, Resolution.Y, 1.0f);
			DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState <false, CF_Always> ::GetRHI());
			
			FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
			FMeshCommandOneFrameArray VisibleMeshDrawCommands;
			FGraphicsMinimalPipelineStateSet PipelineStateSet;
			// @todo loadtime arnes: do we need to pass this along to somewhere?
			bool NeedsShaderInitialization;
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet, NeedsShaderInitialization);
			FHairMaterialProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, &ShadowContext);

			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
			{
				for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId);
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

	return Output;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVelocityCS: public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVelocityCS, FGlobalShader);

	class FVendor : SHADER_PERMUTATION_INT("PERMUTATION_VENDOR", HairVisibilityVendorCount);
	class FVelocity : SHADER_PERMUTATION_INT("PERMUTATION_VELOCITY", 4);
	using FPermutationDomain = TShaderPermutationDomain<FVendor, FVelocity>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ResolutionOffset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, NodeVelocity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutVelocityTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVelocityCS, "/Engine/Private/HairStrands/HairStrandsVelocity.usf", "MainCS", SF_Compute);

static void AddHairVelocityPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	FRDGTextureRef& NodeIndex,
	FRDGBufferRef& NodeVis,
	FRDGBufferRef& NodeVelocity,
	FRDGTextureRef& OutVelocityTexture)
{
	const bool bWriteOutVelocity = OutVelocityTexture != nullptr;
	if (!bWriteOutVelocity)
		return;

	check(OutVelocityTexture->Desc.Format == FMaterialPassOutput::VelocityFormat);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FSceneTexturesUniformParameters SceneTextures;
	SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, SceneTextures);

	FHairVelocityCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVelocityCS::FVendor>(GetVendor());
	PermutationVector.Set<FHairVelocityCS::FVelocity>(bWriteOutVelocity ? FMath::Clamp(GHairVelocityType + 1, 0, 3) : 0);

	FHairVelocityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVelocityCS::FParameters>();
	PassParameters->SceneTexturesStruct = CreateUniformBufferImmediate(SceneTextures, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->NodeIndex = NodeIndex;
	PassParameters->NodeVis = GraphBuilder.CreateSRV(NodeVis);
	PassParameters->NodeVelocity = GraphBuilder.CreateSRV(NodeVelocity, FMaterialPassOutput::VelocityFormat);
	PassParameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);

	FIntRect TotalRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);

	// Snap the rect onto thread group boundary
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TotalRect.Min.X = FMath::FloorToInt(float(TotalRect.Min.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Min.Y = FMath::FloorToInt(float(TotalRect.Min.Y) / float(GroupSize.Y)) * GroupSize.Y;
	TotalRect.Max.X = FMath::CeilToInt(float(TotalRect.Max.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Max.Y = FMath::CeilToInt(float(TotalRect.Max.Y) / float(GroupSize.Y)) * GroupSize.Y;

	FIntPoint RectResolution(TotalRect.Width(), TotalRect.Height());
	PassParameters->ResolutionOffset = FIntPoint(TotalRect.Min.X, TotalRect.Min.Y);

	TShaderMapRef<FHairVelocityCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVelocity"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectResolution, GroupSize));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairLightChannelMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairLightChannelMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FHairLightChannelMaskCS, FGlobalShader);

	class FVendor : SHADER_PERMUTATION_INT("PERMUTATION_VENDOR", HairVisibilityVendorCount);
	using FPermutationDomain = TShaderPermutationDomain<FVendor>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, NodeData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeOffsetAndCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutLightChannelMaskTexture)
	END_SHADER_PARAMETER_STRUCT()
		
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairLightChannelMaskCS, "/Engine/Private/HairStrands/HairStrandsLightChannelMask.usf", "MainCS", SF_Compute);

static FRDGTextureRef AddHairLightChannelMaskPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FIntPoint Resolution,
	FRDGBufferRef NodeData,
	FRDGTextureRef NodeOffsetAndCount)
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
	FRDGTextureRef OutLightChannelMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairLightChannelMask"));

	FHairLightChannelMaskCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairLightChannelMaskCS::FVendor>(GetVendor());

	FHairLightChannelMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairLightChannelMaskCS::FParameters>();
	PassParameters->OutputResolution = Resolution;
	PassParameters->NodeData = GraphBuilder.CreateSRV(NodeData);
	PassParameters->NodeOffsetAndCount = NodeOffsetAndCount;
	PassParameters->OutLightChannelMaskTexture = GraphBuilder.CreateUAV(OutLightChannelMaskTexture);
	
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TShaderMapRef<FHairLightChannelMaskCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairLightChannelMask"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(Resolution, GroupSize));

	return OutLightChannelMaskTexture;
}

/////////////////////////////////////////////////////////////////////////////////////////
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassGlobalParameters, )
	SHADER_PARAMETER(uint32, MaxPPLLNodeCount)
	SHADER_PARAMETER_UAV(RWTexture2D<uint>, PPLLCounter)
	SHADER_PARAMETER_UAV(RWTexture2D<uint>, PPLLNodeIndex)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<FPPLLNodeData>, PPLLNodeData)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassGlobalParameters, "HairVisibilityPass");

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityPassParameters, )
	SHADER_PARAMETER(uint32, MaxPPLLNodeCount)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PPLLCounter)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, PPLLNodeIndex)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPPLLNodeData, PPLLNodeData)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static FVisibilityPassGlobalParameters ConvertToGlobalPassParameter(const FVisibilityPassParameters* In)
{
	FVisibilityPassGlobalParameters Out;
	Out.MaxPPLLNodeCount = In->MaxPPLLNodeCount;
	Out.PPLLCounter = In->PPLLCounter->GetRHI();
	Out.PPLLNodeIndex = In->PPLLNodeIndex->GetRHI();
	Out.PPLLNodeData = In->PPLLNodeData->GetRHI();
	return Out;
}

// Example: 28bytes * 8spp = 224bytes per pixel = 442Mb @ 1080p
struct PPLLNodeData
{
	uint32 Depth;
	uint32 PrimitiveId_MacroGroupId;
	uint32 Tangent_Coverage;
	uint32 BaseColor_Roughness;
	uint32 Specular;
	uint32 NextNodeIndex;
	uint32 PackedVelocity;
};

void CreatePassDummyTextures(FRDGBuilder& GraphBuilder, FVisibilityPassParameters* PassParameters)
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

	PassParameters->PPLLCounter		= GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLNodeIndex")));
	PassParameters->PPLLNodeIndex	= GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLNodeIndex")));
	PassParameters->PPLLNodeData	= GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), 1), TEXT("DummyPPLLNodeData")));
}

template<EHairVisibilityRenderMode RenderMode, bool bCullingEnable>
class FHairVisibilityVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FHairVisibilityVS, MeshMaterial);

protected:

	FHairVisibilityVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		ERHIFeatureLevel::Type FeatureLevel = GetMaxSupportedFeatureLevel((EShaderPlatform)Initializer.Target.Platform);
		check(FSceneInterface::GetShadingPath(FeatureLevel) != EShadingPath::Mobile);
		PassUniformBuffer.Bind(Initializer.ParameterMap, FVisibilityPassGlobalParameters::StaticStructMetadata.GetShaderVariableName());
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
		OutEnvironment.SetDefine(TEXT("USE_CULLED_CLUSTER"), bCullingEnable ? 1 : 0);
	}
};

typedef FHairVisibilityVS<HairVisibilityRenderMode_MSAA_Visibility, false >				THairVisiblityVS_MSAAVisibility_NoCulling;
typedef FHairVisibilityVS<HairVisibilityRenderMode_MSAA_Visibility, true >				THairVisiblityVS_MSAAVisibility_Culling;
typedef FHairVisibilityVS<HairVisibilityRenderMode_MSAA, true >							THairVisiblityVS_MSAA;
typedef FHairVisibilityVS<HairVisibilityRenderMode_Transmittance, true >				THairVisiblityVS_Transmittance;
typedef FHairVisibilityVS<HairVisibilityRenderMode_TransmittanceAndHairCount, true >	THairVisiblityVS_TransmittanceAndHairCount;
typedef FHairVisibilityVS<HairVisibilityRenderMode_PPLL, true >							THairVisiblityVS_PPLL;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAAVisibility_NoCulling,	TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAAVisibility_Culling,		TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_MSAA,						TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_Transmittance,				TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_TransmittanceAndHairCount,	TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, THairVisiblityVS_PPLL,						TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairVisibilityShaderElementData(uint32 InHairMacroGroupId, uint32 InHairMaterialId, uint32 InLightChannelMask) : HairMacroGroupId(InHairMacroGroupId), HairMaterialId(InHairMaterialId), LightChannelMask(InLightChannelMask) { }
	uint32 HairMacroGroupId;
	uint32 HairMaterialId;
	uint32 LightChannelMask;
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
		PassUniformBuffer.Bind(Initializer.ParameterMap, FVisibilityPassGlobalParameters::StaticStructMetadata.GetShaderVariableName());
		HairVisibilityPass_HairMacroGroupIndex.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairMacroGroupIndex"));
		HairVisibilityPass_HairMaterialId.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairMaterialId"));
		HairVisibilityPass_LightChannelMask.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_LightChannelMask"));
	}

	FHairVisibilityPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsCompatibleWithHairStrands(Parameters.Platform, Parameters.MaterialParameters) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)	
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const uint32 RenderModeValue = uint32(RenderMode);
		OutEnvironment.SetDefine(TEXT("HAIR_RENDER_MODE"), RenderModeValue);
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
		ShaderBindings.Add(HairVisibilityPass_HairMacroGroupIndex, ShaderElementData.HairMacroGroupId);
		ShaderBindings.Add(HairVisibilityPass_HairMaterialId, ShaderElementData.HairMaterialId);
		ShaderBindings.Add(HairVisibilityPass_LightChannelMask, ShaderElementData.LightChannelMask);
	}

	LAYOUT_FIELD(FShaderParameter, HairVisibilityPass_HairMacroGroupIndex);
	LAYOUT_FIELD(FShaderParameter, HairVisibilityPass_HairMaterialId);
	LAYOUT_FIELD(FShaderParameter, HairVisibilityPass_LightChannelMask);
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_MSAA_Visibility>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_MSAA>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_Transmittance>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_TransmittanceAndHairCount>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityPS<HairVisibilityRenderMode_PPLL>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityPS.usf"), TEXT("MainVisibility"), SF_Pixel);

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
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairMacroGroupId, uint32 HairMaterialId, bool bCullingEnable);

private:
	template<EHairVisibilityRenderMode RenderMode, bool bCullingEnable=true>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const uint32 HairMacroGroupId,
		const uint32 HairMaterialId,
		const uint32 LightChannelMask,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	const EHairVisibilityRenderMode RenderMode;
	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0, false);
}

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairMacroGroupId, uint32 HairMaterialId, bool bCullingEnable)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetHashedName() == CompatibleVF->GetHashedName();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num()>0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());
	const uint32 LightChannelMask = PrimitiveSceneProxy && PrimitiveSceneProxy->GetLightingChannelMask();

	if (bIsCompatible 
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility && bCullingEnable)
			Process<HairVisibilityRenderMode_MSAA_Visibility, true>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility && !bCullingEnable)
			Process<HairVisibilityRenderMode_MSAA_Visibility, false>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_MSAA)
			Process<HairVisibilityRenderMode_MSAA>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_Transmittance)
			Process<HairVisibilityRenderMode_Transmittance>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
			Process<HairVisibilityRenderMode_TransmittanceAndHairCount>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
		else if (RenderMode == HairVisibilityRenderMode_PPLL)
			Process<HairVisibilityRenderMode_PPLL>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairMacroGroupId, HairMaterialId, LightChannelMask, MeshFillMode, MeshCullMode);
	}
}

template<EHairVisibilityRenderMode TRenderMode, bool bCullingEnable>
void FHairVisibilityProcessor::Process(
	const FMeshBatch& MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const uint32 HairMacroGroupId,
	const uint32 HairMaterialId,
	const uint32 LightChannelMask,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FHairVisibilityVS<TRenderMode, bCullingEnable>,
		FMeshMaterialShader,
		FMeshMaterialShader,
		FHairVisibilityPS<TRenderMode>> PassShaders;
	{
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.VertexShader = MaterialResource.GetShader<FHairVisibilityVS<TRenderMode,bCullingEnable>>(VertexFactoryType);
		PassShaders.PixelShader  = MaterialResource.GetShader<FHairVisibilityPS<TRenderMode>>(VertexFactoryType);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	FHairVisibilityShaderElementData ShaderElementData(HairMacroGroupId, HairMaterialId, LightChannelMask);
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
	const FIntRect Viewport = FIntRect(FIntPoint(0, 0), OutTarget->Desc.Extent);// View->ViewRect;
	const FIntPoint Resolution = OutTarget->Desc.Extent;

	ClearUnusedGraphResources(PixelShader, Parameters);

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
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, View->ViewUniformBuffer);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(),Viewport.Height(),
			Viewport.Min.X,  Viewport.Min.Y,
			Viewport.Width(),Viewport.Height(),
			Viewport.Size(),
			Resolution,
			VertexShader,
			EDRF_UseTriangleOptimization);
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
		SHADER_PARAMETER(uint32, ItemCountPerGroup)
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
	const uint32 ItemCountPerGroup,
	FRDGTextureRef CounterTexture)
{
	check(CounterTexture);

	FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("HairVisibilityIndirectArgBuffer"));

	FCopyIndirectBufferCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyIndirectBufferCS::FParameters>();
	Parameters->ThreadGroupSize = ThreadGroupSize;
	Parameters->ItemCountPerGroup = ItemCountPerGroup;
	Parameters->CounterTexture = CounterTexture;
	Parameters->OutArgBuffer = GraphBuilder.CreateUAV(OutBuffer);

	TShaderMapRef<FCopyIndirectBufferCS> ComputeShader(View->ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisbilityCopyIndirectArgs"),
		ComputeShader,
		Parameters,
		FIntVector(1,1,1));

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityPrimitiveIdCompactionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityPrimitiveIdCompactionCS, FGlobalShader);

	class FVendor		: SHADER_PERMUTATION_INT("PERMUTATION_VENDOR", HairVisibilityVendorCount);
	class FVelocity		: SHADER_PERMUTATION_INT("PERMUTATION_VELOCITY", 4);
	class FViewTransmittance : SHADER_PERMUTATION_INT("PERMUTATION_VIEWTRANSMITTANCE", 2);
	class FMaterial 	: SHADER_PERMUTATION_INT("PERMUTATION_MATERIAL_COMPACTION", 2);
	class FPPLL 		: SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_PPLL", 0, 8, 16, 32); // See GetPPLLMaxRenderNodePerPixel
	class FVisibility 	: SHADER_PERMUTATION_INT("PERMUTATION_VISIBILITY", 2);
	class FMSAACount 	: SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_MSAACOUNT", 4, 8);
	using FPermutationDomain = TShaderPermutationDomain<FVendor, FVelocity, FViewTransmittance, FMaterial, FPPLL, FVisibility, FMSAACount>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, ResolutionOffset)
		SHADER_PARAMETER(uint32, MaxNodeCount)
		SHADER_PARAMETER(uint32, bSortSampleByDepth)
		SHADER_PARAMETER(float, DepthTheshold)
		SHADER_PARAMETER(float, CosTangentThreshold)
		SHADER_PARAMETER(float, CoverageThreshold)

		// Available for the MSAA path
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_IDTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_MaterialTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_AttributeTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MSAA_VelocityTexture)
		// Available for the PPLL path
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PPLLCounter)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PPLLNodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, PPLLNodeData)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ViewTransmittanceTexture)

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

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FPPLL>() > 0)
		{
			PermutationVector.Set<FViewTransmittance>(0);
			PermutationVector.Set<FVisibility>(0);
			PermutationVector.Set<FMSAACount>(4);
		}
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FPPLL>() > 0 && PermutationVector.Get<FViewTransmittance>() > 0)
		{
			return false;
		}
		if (PermutationVector.Get<FPPLL>() > 0 && PermutationVector.Get<FVisibility>() > 0)
		{
			return false;
		}
		if (PermutationVector.Get<FPPLL>() > 0 && PermutationVector.Get<FMSAACount>() == 8)
		{
			return false;
		}
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCompaction.usf", "MainCS", SF_Compute);

static void AddHairVisibilityPrimitiveIdCompactionPass(
	const bool bUsePPLL,
	const bool bUseVisibility,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const uint32 NodeGroupSize,
	FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters,
	FRDGTextureRef& OutCompactCounter,
	FRDGTextureRef& OutCompactNodeIndex,
	FRDGBufferRef& OutCompactNodeData,
	FRDGBufferRef& OutCompactNodeCoord,
	FRDGTextureRef& OutCategorizationTexture,
	FRDGTextureRef& OutVelocityTexture,
	FRDGBufferRef& OutIndirectArgsBuffer,
	uint32& OutMaxRenderNodeCount)
{
	FIntPoint Resolution;
	if (bUsePPLL)
	{
		check(PassParameters->PPLLCounter);
		check(PassParameters->PPLLNodeIndex);
		check(PassParameters->PPLLNodeData);
		Resolution = PassParameters->PPLLNodeIndex->Desc.Extent;
	}
	else
	{
		check(PassParameters->MSAA_DepthTexture->Desc.NumSamples == GetMSAASampleCount());

		if (bUseVisibility)
		{
			check(PassParameters->MSAA_DepthTexture);
			check(PassParameters->MSAA_IDTexture);
			Resolution = PassParameters->MSAA_DepthTexture->Desc.Extent;
		}
		else
		{
			check(PassParameters->MSAA_DepthTexture);
			check(PassParameters->MSAA_IDTexture);
			check(PassParameters->MSAA_MaterialTexture);
			check(PassParameters->MSAA_AttributeTexture);
			Resolution = PassParameters->MSAA_DepthTexture->Desc.Extent;
		}
	}

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
		OutCompactCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityCompactCounter"));
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

	const uint32 ClearValues[4] = { 0u,0u,0u,0u };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactCounter), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCompactNodeIndex), ClearValues);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutCategorizationTexture), ClearValues);

	// Select render node count according to current mode
	const uint32 MSAASampleCount = GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? GetMSAASampleCount() : 1;
	const uint32 PPLLMaxRenderNodePerPixel = GetPPLLMaxRenderNodePerPixel();
	const uint32 MaxRenderNodeCount = Resolution.X * Resolution.Y * (GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? MSAASampleCount : PPLLMaxRenderNodePerPixel);
	OutCompactNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(bUseVisibility ? sizeof(HairStrandsVisibilityInternal::NodeVis) : sizeof(HairStrandsVisibilityInternal::NodeData), MaxRenderNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeData"));

	{
		// Pixel coord of the node. Stored as 2*uint16, packed into a single uint32
		OutCompactNodeCoord = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxRenderNodeCount), TEXT("HairVisibilityPrimitiveIdCompactNodeCoord"));
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FSceneTexturesUniformParameters SceneTextures;
	SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, SceneTextures);

	const bool bWriteOutVelocity = OutVelocityTexture != nullptr;
	const uint32 VelocityPermutation = bWriteOutVelocity ? FMath::Clamp(GHairVelocityType + 1, 0, 3) : 0;
	FHairVisibilityPrimitiveIdCompactionCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVendor>(GetVendor());
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVelocity>(VelocityPermutation);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FViewTransmittance>(PassParameters->ViewTransmittanceTexture ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FMaterial>(GHairStrandsMaterialCompactionEnable ? 1 : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FPPLL>(bUsePPLL ? PPLLMaxRenderNodePerPixel : 0);
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FVisibility>(bUseVisibility ? 1 : 0);	
	PermutationVector.Set<FHairVisibilityPrimitiveIdCompactionCS::FMSAACount>(MSAASampleCount == 4 ? 4 : 8);
	PermutationVector = FHairVisibilityPrimitiveIdCompactionCS::RemapPermutation(PermutationVector);

	PassParameters->OutputResolution = Resolution;
	PassParameters->MaxNodeCount = MaxRenderNodeCount;
	PassParameters->bSortSampleByDepth = GHairStrandsSortHairSampleByDepth > 0 ? 1 : 0;
	PassParameters->CoverageThreshold = FMath::Clamp(GHairStrandsFullCoverageThreshold, 0.1f, 1.f);
	PassParameters->DepthTheshold = FMath::Clamp(GHairStrandsMaterialCompactionDepthThreshold, 0.f, 100.f);
	PassParameters->CosTangentThreshold = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(GHairStrandsMaterialCompactionTangentThreshold, 0.f, 90.f)));
	PassParameters->SceneTexturesStruct = CreateUniformBufferImmediate(SceneTextures, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->OutCompactNodeCounter = GraphBuilder.CreateUAV(OutCompactCounter);
	PassParameters->OutCompactNodeIndex = GraphBuilder.CreateUAV(OutCompactNodeIndex);
	PassParameters->OutCompactNodeData = GraphBuilder.CreateUAV(OutCompactNodeData);
	PassParameters->OutCompactNodeCoord = GraphBuilder.CreateUAV(OutCompactNodeCoord);
	PassParameters->OutCategorizationTexture = GraphBuilder.CreateUAV(OutCategorizationTexture);

	if (bWriteOutVelocity)
	{
		PassParameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);
	}
 
	FIntRect TotalRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);

	// Snap the rect onto thread group boundary
	const FIntPoint GroupSize = GetVendorOptimalGroupSize2D();
	TotalRect.Min.X = FMath::FloorToInt(float(TotalRect.Min.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Min.Y = FMath::FloorToInt(float(TotalRect.Min.Y) / float(GroupSize.Y)) * GroupSize.Y;
	TotalRect.Max.X = FMath::CeilToInt(float(TotalRect.Max.X) / float(GroupSize.X)) * GroupSize.X;
	TotalRect.Max.Y = FMath::CeilToInt(float(TotalRect.Max.Y) / float(GroupSize.Y)) * GroupSize.Y;

	FIntPoint RectResolution(TotalRect.Width(), TotalRect.Height());
	PassParameters->ResolutionOffset = FIntPoint(TotalRect.Min.X, TotalRect.Min.Y);

	TShaderMapRef<FHairVisibilityPrimitiveIdCompactionCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsVisibilityCompaction"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectResolution, GroupSize));

	OutIndirectArgsBuffer = AddCopyIndirectArgPass(GraphBuilder, &View, NodeGroupSize, 1, OutCompactCounter);
	OutMaxRenderNodeCount = MaxRenderNodeCount;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairGenerateTileCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairGenerateTileCS);
	SHADER_USE_PARAMETER_STRUCT(FHairGenerateTileCS, FGlobalShader);

	class FTileSize : SHADER_PERMUTATION_INT("PERMUTATION_TILESIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FTileSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER(FIntPoint, TileResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutTileCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutTileIndexTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutTileBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairGenerateTileCS, "/Engine/Private/HairStrands/HairStrandsVisibilityTile.usf", "MainCS", SF_Compute);

static void AddGenerateTilePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ThreadGroupSize,
	const uint32 TileSize,
	const FRDGTextureRef& CategorizationTexture,
	FRDGTextureRef& OutTileIndexTexture,
	FRDGBufferRef& OutTileBuffer,
	FRDGBufferRef& OutTileIndirectArgs)
{
	check(TileSize == 8); // only size supported for now
	const FIntPoint Resolution = CategorizationTexture->Desc.Extent;
	const FIntPoint TileResolution = FIntPoint(FMath::CeilToInt(Resolution.X / float(TileSize)), FMath::CeilToInt(Resolution.Y / float(TileSize)));
	const uint32 TileCount = TileResolution.X * TileResolution.Y;

	FRDGTextureRef TileCounter;
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
		TileCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairTileCounter"));
	}

	{
		FRDGTextureDesc Desc;
		Desc.Extent = TileResolution;
		Desc.Depth = 0;
		Desc.Format = PF_R32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		OutTileIndexTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairTileIndexTexture"));
	}

	OutTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileCount), TEXT("HairTileBuffer"));

	uint32 ClearValues[4] = { 0,0,0,0 };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileCounter), ClearValues);

	FHairGenerateTileCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairGenerateTileCS::FTileSize>(0);

	FHairGenerateTileCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairGenerateTileCS::FParameters>();
	PassParameters->Resolution = Resolution;
	PassParameters->TileResolution = TileResolution;
	PassParameters->CategorizationTexture = CategorizationTexture;
	PassParameters->OutTileCounter = GraphBuilder.CreateUAV(TileCounter);
	PassParameters->OutTileIndexTexture = GraphBuilder.CreateUAV(OutTileIndexTexture);
	PassParameters->OutTileBuffer = GraphBuilder.CreateUAV(OutTileBuffer, PF_R16G16_UINT);

	TShaderMapRef<FHairGenerateTileCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairGenerateTile"),
		ComputeShader,
		PassParameters,
		FIntVector(TileResolution.X, TileResolution.Y, 1));

	OutTileIndirectArgs = AddCopyIndirectArgPass(GraphBuilder, &View, ThreadGroupSize, TileSize*TileSize, TileCounter);
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
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FRDGTextureRef& SceneDepthTexture)
{
	FRDGTextureRef OutVisibilityDepthTexture;
	{
		check(GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA);

		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_DepthStencil;
		Desc.NumMips = 1;
		Desc.NumSamples = GetMSAASampleCount();
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
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	TArray<FIntRect> MacroGroupRects;
	if (IsHairStrandsViewRectOptimEnable())
	{
		for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
		{
			MacroGroupRects.Add(MacroGroupData.ScreenRect);
		}
	}
	else
	{
		MacroGroupRects.Add(Viewport);
	}

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityFillOpaqueDepth"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, MacroGroupRects, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

			for (const FIntRect& ViewRect : MacroGroupRects)
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
					VertexShader,
					EDRF_UseTriangleOptimization);
			}
		});
	}


	return OutVisibilityDepthTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void AddHairVisibilityCommonPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const EHairVisibilityRenderMode RenderMode,
	FVisibilityPassParameters* PassParameters)
{
	auto GetPassName = [RenderMode]()
	{
		switch (RenderMode)
		{
		case HairVisibilityRenderMode_PPLL:						return RDG_EVENT_NAME("HairStrandsVisibilityPPLLPass");
		case HairVisibilityRenderMode_MSAA:						return RDG_EVENT_NAME("HairStrandsVisibilityMSAAPass");
		case HairVisibilityRenderMode_MSAA_Visibility:			return RDG_EVENT_NAME("HairStrandsVisibilityMSAAVisPass");
		case HairVisibilityRenderMode_Transmittance:			return RDG_EVENT_NAME("HairStrandsTransmittancePass");
		case HairVisibilityRenderMode_TransmittanceAndHairCount:return RDG_EVENT_NAME("HairStrandsTransmittanceAndHairCountPass");
		default:												return RDG_EVENT_NAME("Noname");
		}
	};

	GraphBuilder.AddPass(
		GetPassName(),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Scene = Scene, ViewInfo, &MacroGroupDatas, RenderMode](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FVisibilityPassGlobalParameters GlobalPassParameters = ConvertToGlobalPassParameter(PassParameters);
		TUniformBufferRef<FVisibilityPassGlobalParameters> GlobalPassParametersBuffer = TUniformBufferRef<FVisibilityPassGlobalParameters>::CreateUniformBufferImmediate(GlobalPassParameters, UniformBuffer_SingleFrame);

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, GlobalPassParametersBuffer);

		// Note: this reference needs to persistent until SubmitMeshDrawCommands() is called, as DrawRenderState does not ref count 
		// the view uniform buffer (raw pointer). It is only within the MeshProcessor that the uniform buffer get reference
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
		if(RenderMode == HairVisibilityRenderMode_Transmittance || RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount || RenderMode == HairVisibilityRenderMode_PPLL)
		{
			const bool bEnableMSAA = false;
			SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfoBits);
			// Create and set the uniform buffer
			ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
			DrawRenderState.SetViewUniformBuffer(ViewUniformShaderParameters);
		}

		{
			RHICmdList.SetViewport(0, 0, 0.0f, ViewInfo->ViewRect.Width(), ViewInfo->ViewRect.Height(), 1.0f);
			if (RenderMode == HairVisibilityRenderMode_MSAA)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
					CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_Transmittance)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RED, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<
					CW_RED, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_Zero,
					CW_RG, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_Zero>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}
			else if (RenderMode == HairVisibilityRenderMode_PPLL)
			{
				DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
				DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}

			FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
			FMeshCommandOneFrameArray VisibleMeshDrawCommands;
			FGraphicsMinimalPipelineStateSet PipelineStateSet;
			bool NeedsShaderInitialization;
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet, NeedsShaderInitialization);
			FHairVisibilityProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, RenderMode, &ShadowContext);
			
			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
			{
				for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : MacroGroupData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, MacroGroupData.MacroGroupId, PrimitiveInfo.MaterialId, PrimitiveInfo.IsCullingEnable());
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
	const bool bUseVisibility,
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef& OutVisibilityIdTexture,
	FRDGTextureRef& OutVisibilityMaterialTexture,
	FRDGTextureRef& OutVisibilityAttributeTexture,
	FRDGTextureRef& OutVisibilityVelocityTexture,
	FRDGTextureRef& OutVisibilityDepthTexture)
{
	const uint32 MSAASampleCount = GetMSAASampleCount();

	if (bUseVisibility)
	{
		{
			FRDGTextureDesc Desc;
			Desc.Extent.X = Resolution.X;
			Desc.Extent.Y = Resolution.Y;
			Desc.Depth = 0;
			Desc.Format = PF_R32_UINT;
			Desc.NumMips = 1;
			Desc.NumSamples = MSAASampleCount;
			Desc.Flags = TexCreate_None;
			Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
			Desc.bForceSharedTargetAndShaderResource = true;
			OutVisibilityIdTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityIDTexture"));
		}
		OutVisibilityMaterialTexture = nullptr;
		OutVisibilityAttributeTexture = nullptr;
		OutVisibilityVelocityTexture = nullptr;

		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAIdTexture"), ViewInfo, 0xFFFFFFFF, OutVisibilityIdTexture);

		FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
		CreatePassDummyTextures(GraphBuilder, PassParameters);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_MSAA_Visibility, PassParameters);
	}
	else
	{
		{
			FRDGTextureDesc Desc;
			Desc.Extent.X = Resolution.X;
			Desc.Extent.Y = Resolution.Y;
			Desc.Depth = 0;
			Desc.Format = PF_R32G32_UINT;
			Desc.NumMips = 1;
			Desc.NumSamples = MSAASampleCount;
			Desc.Flags = TexCreate_None;
			Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
			Desc.bForceSharedTargetAndShaderResource = true;
			OutVisibilityIdTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityIDTexture"));
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
			OutVisibilityMaterialTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityMaterialTexture"));
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
			OutVisibilityAttributeTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityAttributeTexture"));
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
			OutVisibilityVelocityTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityVelocityTexture"));
		}
		AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAIdTexture"), ViewInfo, 0xFFFFFFFF, OutVisibilityIdTexture);

		// Manually clear RTs as using the Clear action on the RT, issue a global clean on all targets, while still need a special clear 
		// for the PrimitiveId buffer
		// const ERenderTargetLoadAction LoadAction = GHairClearVisibilityBuffer ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ENoAction;
		ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::ENoAction;
		if (GHairClearVisibilityBuffer)
		{
			LoadAction = ERenderTargetLoadAction::ELoad;
			AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAMaterial"), ViewInfo, 0, OutVisibilityMaterialTexture);
			AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAAttribute"), ViewInfo, 0, OutVisibilityAttributeTexture);
			AddClearGraphicPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsClearVisibilityMSAAVelocity"), ViewInfo, 0, OutVisibilityVelocityTexture);
		}

		FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
		CreatePassDummyTextures(GraphBuilder, PassParameters);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(OutVisibilityMaterialTexture, LoadAction, 0);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(OutVisibilityAttributeTexture, LoadAction, 0);
		PassParameters->RenderTargets[3] = FRenderTargetBinding(OutVisibilityVelocityTexture, LoadAction, 0);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_MSAA, PassParameters);
	}
}

static void AddHairVisibilityPPLLPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef& InViewZDepthTexture,
	FRDGTextureRef& OutVisibilityPPLLNodeCounter,
	FRDGTextureRef& OutVisibilityPPLLNodeIndex,
	FRDGBufferRef&  OutVisibilityPPLLNodeData)
{
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
		OutVisibilityPPLLNodeCounter = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLCounter"));
	}

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Resolution.X;
		Desc.Extent.Y = Resolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		OutVisibilityPPLLNodeIndex = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityPPLLNodeIndex"));
	}

	const uint32 PPLLMaxTotalListElementCount = GetPPLLMaxTotalListElementCount(Resolution);
	{
		OutVisibilityPPLLNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), PPLLMaxTotalListElementCount), TEXT("HairVisibilityPPLLNodeData"));
	}
	const uint32 ClearValue0[4] = { 0,0,0,0 };
	const uint32 ClearValueInvalid[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutVisibilityPPLLNodeCounter), ClearValue0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutVisibilityPPLLNodeIndex), ClearValueInvalid);

	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
	PassParameters->PPLLCounter = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeCounter, 0));
	PassParameters->PPLLNodeIndex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeIndex, 0));
	PassParameters->PPLLNodeData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutVisibilityPPLLNodeData));
	PassParameters->MaxPPLLNodeCount = PPLLMaxTotalListElementCount;
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(InViewZDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, HairVisibilityRenderMode_PPLL, PassParameters);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairPrimaryTransmittance
{
	FRDGTextureRef TransmittanceTexture = nullptr;
	FRDGTextureRef HairCountTexture = nullptr;

	FRDGTextureRef HairCountTextureUint = nullptr;
	FRDGTextureRef DepthTextureUint = nullptr;
};

static FHairPrimaryTransmittance AddHairViewTransmittancePass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& Resolution,
	const bool bOutputHairCount,
	FRDGTextureRef SceneDepthTexture)
{
	check(SceneDepthTexture->Desc.Extent == Resolution);
	const EHairVisibilityRenderMode RenderMode = bOutputHairCount ? HairVisibilityRenderMode_TransmittanceAndHairCount : HairVisibilityRenderMode_Transmittance;

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
	Desc.ClearValue = FClearValueBinding(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)); // Clear to transmittance 1

	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
	CreatePassDummyTextures(GraphBuilder, PassParameters);
	FHairPrimaryTransmittance Out;

	Out.TransmittanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewTransmittanceTexture"));
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Out.TransmittanceTexture, ERenderTargetLoadAction::EClear, 0);

	if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
	{
		Desc.Format = PF_G32R32F;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Out.HairCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewHairCountTexture"));
		PassParameters->RenderTargets[1] = FRenderTargetBinding(Out.HairCountTexture, ERenderTargetLoadAction::EClear, 0);
	}

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		SceneDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, MacroGroupDatas, RenderMode, PassParameters);

	return Out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Inject depth information into the view hair count texture, to block opaque occluder
class FHairViewTransmittanceDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairViewTransmittanceDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairViewTransmittanceDepthPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, DistanceThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairViewTransmittanceDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityTransmittanceDepthPS.usf", "MainPS", SF_Pixel);

static void AddHairViewTransmittanceDepthPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& CategorizationTexture,
	const FRDGTextureRef& SceneDepthTexture,
	FRDGTextureRef& HairCountTexture)
{
	FHairViewTransmittanceDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairViewTransmittanceDepthPS::FParameters>();
	Parameters->DistanceThreshold = FMath::Max(1.f, GHairStrandsViewHairCountDepthDistanceThreshold);
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->SceneDepthTexture = SceneDepthTexture;
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(HairCountTexture, ERenderTargetLoadAction::ELoad);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairViewTransmittanceDepthPS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = HairCountTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;
	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsViewTransmittanceDepth"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(), Viewport.Height(),
			Viewport.Min.X, Viewport.Min.Y,
			Viewport.Width(), Viewport.Height(),
			Viewport.Size(),
			Resolution,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityDepthPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityDepthPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorisationTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityDepthPS, "/Engine/Private/HairStrands/HairStrandsVisibilityDepthPS.usf", "MainPS", SF_Pixel);

static void AddHairVisibilityColorAndDepthPatchPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& CategorisationTexture,
	FRDGTextureRef& OutGBufferBTexture,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutDepthTexture)
{
	if (!OutGBufferBTexture || !OutColorTexture || !OutDepthTexture)
	{
		return;
	}

	FHairVisibilityDepthPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityDepthPS::FParameters>();
	Parameters->CategorisationTexture = CategorisationTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutGBufferBTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[1] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	FHairVisibilityDepthPS::FPermutationDomain PermutationVector;
	PermutationVector = FHairVisibilityDepthPS::RemapPermutation(PermutationVector);
	TShaderMapRef<FHairVisibilityDepthPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

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
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairCountToCoverageCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairCountToCoverageCS);
	SHADER_USE_PARAMETER_STRUCT(FHairCountToCoverageCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(float, LUT_HairCount)
		SHADER_PARAMETER(float, LUT_HairRadiusCount)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCoverageLUT)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairCountToCoverageCS, "/Engine/Private/HairStrands/HairStrandsCoverage.usf", "MainCS", SF_Compute);

static FRDGTextureRef AddHairHairCountToTransmittancePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	const FHairLUT& HairLUT,
	const FRDGTextureRef HairCountTexture)
{
	const FIntPoint OutputResolution = HairCountTexture->Desc.Extent;

	FRDGTextureDesc Desc;
	Desc.Extent = OutputResolution;
	Desc.Depth = 0;
	Desc.Format = PF_R32_FLOAT;
	Desc.NumMips = 1;
	Desc.NumSamples = 1;
	Desc.Flags = TexCreate_None;
	Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable;
	Desc.bForceSharedTargetAndShaderResource = true;
	Desc.ClearValue = FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityTexture"));
	FRDGTextureRef HairCoverageLUT = GraphBuilder.RegisterExternalTexture(HairLUT.Textures[HairLUTType_Coverage], TEXT("HairCoverageLUT"));

	FHairCountToCoverageCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairCountToCoverageCS::FParameters>();
	PassParameters->LUT_HairCount = HairCoverageLUT->Desc.Extent.X;
	PassParameters->LUT_HairRadiusCount = HairCoverageLUT->Desc.Extent.Y;
	PassParameters->OutputResolution = OutputResolution;
	PassParameters->HairCoverageLUT = HairCoverageLUT;
	PassParameters->HairCountTexture = HairCountTexture;
	PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

	TShaderMapRef<FHairCountToCoverageCS> ComputeShader(ViewInfo.ShaderMap);
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVisibilityComputeRaster"), ComputeShader, PassParameters, FComputeShaderUtils::GetGroupCount(OutputResolution, FIntPoint(8,8)));

	return OutputTexture;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FVisiblityRasterComputeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisiblityRasterComputeCS);
	SHADER_USE_PARAMETER_STRUCT(FVisiblityRasterComputeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, MaxRasterCount)
		SHADER_PARAMETER(uint32, FrameIdMod8)
		SHADER_PARAMETER(uint32, HairMaterialId)
		SHADER_PARAMETER(uint32, ResolutionMultiplier)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(float, HairStrandsVF_Density)
		SHADER_PARAMETER(float, HairStrandsVF_Radius)
		SHADER_PARAMETER(float, HairStrandsVF_Length)
		SHADER_PARAMETER(uint32, HairStrandsVF_bUseStableRasterization)
		SHADER_PARAMETER(FVector, HairStrandsVF_PositionOffset)
		SHADER_PARAMETER(uint32, HairStrandsVF_VertexCount)
		SHADER_PARAMETER(FMatrix, HairStrandsVF_LocalToWorldPrimitiveTransform)
		SHADER_PARAMETER_SRV(Buffer, HairStrandsVF_PositionBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutHairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutVisibilityTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		// TODO:
		//if (!FDataDrivenShaderPlatformInfo::GetInfo(Parameters.Platform).bSupportsUInt64ImageAtomics))
		//	return false;

		return IsHairStrandsSupported(Parameters.Platform); 
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RASTERCOMPUTE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisiblityRasterComputeCS, "/Engine/Private/HairStrands/HairStrandsVisibilityRasterCompute.usf", "MainCS", SF_Compute);


static bool DoesSupportRasterCompute()
{
#if PLATFORM_WINDOWS
	return IsRHIDeviceNVIDIA() && GRHISupportsAtomicUInt64;
#else
	return false;
#endif
}

struct FRasterComputeOutput
{
	FIntPoint BaseResolution;
	FIntPoint SuperResolution;
	uint32 ResolutionMultiplier = 1;

	FRDGTextureRef HairCountTexture = nullptr;
	FRDGTextureRef DepthTexture = nullptr;
	FRDGTextureRef VisibilityTexture = nullptr;
};

static FRasterComputeOutput AddVisibilityComputeRasterPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FIntPoint& InResolution,
	const FRDGTextureRef SceneDepthTexture)
{
	check(DoesSupportRasterCompute());

	FRasterComputeOutput Out;

	Out.ResolutionMultiplier = FMath::Clamp(GHairStrandsVisibilityComputeRasterSampleCount, 1, 4);
	Out.BaseResolution		 = InResolution;
	Out.SuperResolution		 = InResolution * Out.ResolutionMultiplier;

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Out.SuperResolution.X;
		Desc.Extent.Y = Out.SuperResolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable;
		Desc.bForceSharedTargetAndShaderResource = true;
		Desc.ClearValue = FClearValueBinding(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)); // Clear to transmittance 1
		Out.HairCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewTransmittanceTexture"));
	}
	FRDGTextureUAVRef HairCountTextureUAV = GraphBuilder.CreateUAV(Out.HairCountTexture);

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Out.SuperResolution.X;
		Desc.Extent.Y = Out.SuperResolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable;
		Desc.bForceSharedTargetAndShaderResource = true;
		Desc.ClearValue = FClearValueBinding(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)); // Clear to transmittance 1
		Out.DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairDepthTexture"));
	}
	FRDGTextureUAVRef DepthTextureUAV = GraphBuilder.CreateUAV(Out.DepthTexture);

	{
		FRDGTextureDesc Desc;
		Desc.Extent.X = Out.SuperResolution.X;
		Desc.Extent.Y = Out.SuperResolution.Y;
		Desc.Depth = 0;
		Desc.Format = PF_R32G32_UINT;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource | TexCreate_RenderTargetable;
		Desc.bForceSharedTargetAndShaderResource = true;
		Desc.ClearValue = FClearValueBinding(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)); // Clear to transmittance 1
		Out.VisibilityTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairVisibilityTexture"));
	}
	FRDGTextureUAVRef VisibilityTextureUAV = GraphBuilder.CreateUAV(Out.VisibilityTexture);

	uint32 ClearValues[4] = { 0,0,0,0 };
	AddClearUAVPass(GraphBuilder, HairCountTextureUAV, ClearValues);
	AddClearUAVPass(GraphBuilder, DepthTextureUAV, ClearValues);
	AddClearUAVPass(GraphBuilder, VisibilityTextureUAV, ClearValues);

	// Create and set the uniform buffer
	const bool bEnableMSAA = false;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
	SetUpViewHairRenderInfo(ViewInfo, bEnableMSAA, ViewInfo.CachedViewUniformShaderParameters->HairRenderInfo, ViewInfo.CachedViewUniformShaderParameters->HairRenderInfoBits);
	ViewUniformShaderParameters = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*ViewInfo.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

	const uint32 FrameIdMode8 = ViewInfo.ViewState ? (ViewInfo.ViewState->GetFrameIndex() % 8) : 0;
	const uint32 GroupSize = 32;
	const uint32 DispatchCountX = 64;
	TShaderMapRef<FVisiblityRasterComputeCS> ComputeShader(ViewInfo.ShaderMap);

	for (const FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas.Datas)
	{
		const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos = MacroGroup.PrimitivesInfos;

		for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : PrimitiveSceneInfos)
		{
			FVisiblityRasterComputeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisiblityRasterComputeCS::FParameters>();
			PassParameters->OutputResolution = Out.SuperResolution;
			PassParameters->ResolutionMultiplier = Out.ResolutionMultiplier;
			PassParameters->MacroGroupId = MacroGroup.MacroGroupId;
			PassParameters->DispatchCountX = DispatchCountX;
			PassParameters->MaxRasterCount = FMath::Clamp(GHairStrandsVisibilityComputeRasterMaxPixelCount, 1, 256);
			PassParameters->FrameIdMod8 = FrameIdMode8;
			PassParameters->HairMaterialId = PrimitiveInfo.MaterialId;
			PassParameters->ViewUniformBuffer = ViewUniformShaderParameters;
			PassParameters->SceneDepthTexture = SceneDepthTexture;
			PassParameters->OutHairCountTexture = HairCountTextureUAV;
			PassParameters->OutDepthTexture = DepthTextureUAV;
			PassParameters->OutVisibilityTexture = VisibilityTextureUAV;

			check(PrimitiveInfo.MeshBatchAndRelevance.Mesh && PrimitiveInfo.MeshBatchAndRelevance.Mesh->Elements.Num() > 0);
			const FHairGroupPublicData* HairGroupPublicData = reinterpret_cast<const FHairGroupPublicData*>(PrimitiveInfo.MeshBatchAndRelevance.Mesh->Elements[0].VertexFactoryUserData);
			const FHairGroupPublicData::VertexFactoryInput& VFInput = HairGroupPublicData->VFInput;
			PassParameters->HairStrandsVF_PositionBuffer = VFInput.HairPositionBuffer;
			PassParameters->HairStrandsVF_PositionOffset = VFInput.HairPositionOffset;
			PassParameters->HairStrandsVF_VertexCount = VFInput.VertexCount;
			PassParameters->HairStrandsVF_Radius = VFInput.HairRadius;
			PassParameters->HairStrandsVF_Length = VFInput.HairLength;
			PassParameters->HairStrandsVF_bUseStableRasterization = VFInput.bUseStableRasterization ? 1 : 0;
			PassParameters->HairStrandsVF_Density = VFInput.HairDensity;
			PassParameters->HairStrandsVF_LocalToWorldPrimitiveTransform = VFInput.LocalToWorldTransform.ToMatrixWithScale();

			const uint32 DispatchCountY = FMath::CeilToInt(PassParameters->HairStrandsVF_VertexCount / float(GroupSize * DispatchCountX));
			const FIntVector DispatchCount(DispatchCountX, DispatchCountY, 1);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVisibilityComputeRaster"), ComputeShader, PassParameters, DispatchCount);
		}
	}

	return Out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool GetHairStrandsSkyLightingEnable();

FHairStrandsVisibilityViews RenderHairStrandsVisibilityBuffer(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const TArray<FViewInfo>& Views,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferBTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneColorTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneDepthTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneVelocityTexture,
	const FHairStrandsMacroGroupViews& MacroGroupViews)
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
			FHairLUT HairLUT = GetHairLUT(RHICmdList, View);

			FHairStrandsVisibilityData& VisibilityData = Output.HairDatas.AddDefaulted_GetRef();
			VisibilityData.NodeGroupSize = GetVendorOptimalGroupSize1D();
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = MacroGroupViews.Views[ViewIndex];

			if (MacroGroupDatas.Datas.Num() == 0)
				continue;

			// Use the scene color for computing target resolution as the View.ViewRect, 
			// doesn't include the actual resolution padding which make buffer size 
			// mismatch, and create artifact (e.g. velocity computation)
			check(InSceneDepthTexture);
			const FIntPoint Resolution = InSceneDepthTexture->GetDesc().Extent;


			FRDGBuilder GraphBuilder(RHICmdList);
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
			FRDGTextureRef SceneGBufferBTexture = GraphBuilder.TryRegisterExternalTexture(InSceneGBufferBTexture, TEXT("SceneGBufferBTexture"));
			FRDGTextureRef SceneColorTexture = GraphBuilder.TryRegisterExternalTexture(InSceneColorTexture, TEXT("SceneColorTexture"));
			FRDGTextureRef SceneDepthTexture = GraphBuilder.RegisterExternalTexture(InSceneDepthTexture, TEXT("SceneDepthTexture"));
			FRDGTextureRef SceneVelocityTexture = GraphBuilder.TryRegisterExternalTexture(InSceneVelocityTexture, TEXT("SceneVelocityTexture"));

			const bool bRunColorAndDepthPatching = SceneGBufferBTexture && SceneColorTexture;
			const EHairVisibilityRenderMode RenderMode = GetHairVisibilityRenderMode();
			check(RenderMode == HairVisibilityRenderMode_MSAA || RenderMode == HairVisibilityRenderMode_PPLL);

			// Run the view transmittance pass if needed (not in PPLL mode that is already a high quality render path)
			FHairPrimaryTransmittance ViewTransmittance;
			if (GHairStrandsViewTransmittancePassEnable > 0 && RenderMode != HairVisibilityRenderMode_PPLL)
			{
				// Note: Hair count is required for the sky lighting at the moment as it is used for the TT term
				const bool bOutputHairCount = GetHairStrandsSkyLightingEnable();
				ViewTransmittance = AddHairViewTransmittancePass(
					GraphBuilder,
					Scene,
					&View,
					MacroGroupDatas,
					Resolution,
					bOutputHairCount,
					SceneDepthTexture);

				const bool bHairCountToTransmittance = GHairStrandsHairCountToTransmittance > 0;
				if (bHairCountToTransmittance)
				{
					ViewTransmittance.TransmittanceTexture = AddHairHairCountToTransmittancePass(
						GraphBuilder,
						View,
						HairLUT,
						ViewTransmittance.HairCountTexture);
				}

				const bool bUseRasterCompute = GHairStrandsVisibilityComputeRaster > 0 && DoesSupportRasterCompute();
				if (bUseRasterCompute)
				{
					FRasterComputeOutput RasterOutput = AddVisibilityComputeRasterPass(
						GraphBuilder,
						View,
						MacroGroupDatas,
						Resolution,
						SceneDepthTexture);

					ViewTransmittance.HairCountTextureUint = RasterOutput.HairCountTexture;
					ViewTransmittance.DepthTextureUint = RasterOutput.DepthTexture;
				}

			}

			FRDGTextureRef CategorizationTexture = nullptr;
			FRDGTextureRef CompactNodeIndex = nullptr;
			FRDGBufferRef  CompactNodeData = nullptr;
			FRDGTextureRef NodeCounter = nullptr;
			if (RenderMode == HairVisibilityRenderMode_MSAA)
			{
				const bool bIsVisiblityEnable = GHairStrandsVisibilityMaterialPass > 0;

				struct FRDGMsaaVisibilityResources
				{
					FRDGTextureRef DepthTexture;
					FRDGTextureRef IdTexture;
					FRDGTextureRef MaterialTexture;
					FRDGTextureRef AttributeTexture;
					FRDGTextureRef VelocityTexture;
				} MsaaVisibilityResources;

				MsaaVisibilityResources.DepthTexture = AddHairVisibilityFillOpaqueDepth(
					GraphBuilder,
					View,
					Resolution,
					MacroGroupDatas,
					SceneDepthTexture);

				AddHairVisibilityMSAAPass(
					bIsVisiblityEnable,
					GraphBuilder,
					Scene,
					&View,
					MacroGroupDatas,
					Resolution,
					MsaaVisibilityResources.IdTexture,
					MsaaVisibilityResources.MaterialTexture,
					MsaaVisibilityResources.AttributeTexture,
					MsaaVisibilityResources.VelocityTexture,
					MsaaVisibilityResources.DepthTexture);

				// This is used when compaction is not enabled.
				VisibilityData.MaxSampleCount = MsaaVisibilityResources.IdTexture->Desc.NumSamples;
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.IdTexture, &VisibilityData.IDTexture);
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.DepthTexture, &VisibilityData.DepthTexture);
				if (!bIsVisiblityEnable)
				{
					GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.MaterialTexture, &VisibilityData.MaterialTexture);
					GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.AttributeTexture, &VisibilityData.AttributeTexture);
					GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.VelocityTexture, &VisibilityData.VelocityTexture);
				}

				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->MSAA_DepthTexture = MsaaVisibilityResources.DepthTexture;
					PassParameters->MSAA_IDTexture = MsaaVisibilityResources.IdTexture;
					PassParameters->MSAA_MaterialTexture = MsaaVisibilityResources.MaterialTexture;
					PassParameters->MSAA_AttributeTexture = MsaaVisibilityResources.AttributeTexture;
					PassParameters->MSAA_VelocityTexture = MsaaVisibilityResources.VelocityTexture;
					PassParameters->ViewTransmittanceTexture = ViewTransmittance.TransmittanceTexture;

					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					AddHairVisibilityPrimitiveIdCompactionPass(
						false, // bUsePPLL
						bIsVisiblityEnable,
						GraphBuilder,
						View,
						MacroGroupDatas,
						VisibilityData.NodeGroupSize,
						PassParameters,
						NodeCounter,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer,
						VisibilityData.MaxNodeCount);

					if (bIsVisiblityEnable)
					{
						const bool bUpdateSampleCoverage = GHairStrandsSortHairSampleByDepth > 0;

						// Evaluate material based on the visiblity pass result
						// Output both complete sample data + per-sample velocity
						FMaterialPassOutput PassOutput = AddHairMaterialPass(
							GraphBuilder,
							Scene,
							&View,
							bUpdateSampleCoverage,
							MacroGroupDatas,
							VisibilityData.NodeGroupSize,
							CompactNodeIndex,
							CompactNodeData,
							CompactNodeCoord,
							IndirectArgsBuffer);

						// Merge per-sample velocity into the scene velocity buffer
						AddHairVelocityPass(
							GraphBuilder,
							View,
							MacroGroupDatas,
							CompactNodeIndex,
							CompactNodeData,
							PassOutput.NodeVelocity,
							SceneVelocityTexture);

						if (bUpdateSampleCoverage)
						{
							PassOutput.NodeData = AddUpdateSampleCoveragePass(
								GraphBuilder,
								&View,
								CompactNodeIndex,
								PassOutput.NodeData);
						}

						CompactNodeData = PassOutput.NodeData;
					}

					// Allocate buffer for storing all the light samples
					FRDGTextureRef SampleLightingBuffer = AddClearLightSamplePass(GraphBuilder, &View, VisibilityData.MaxNodeCount, NodeCounter);
					VisibilityData.SampleLightingViewportResolution = SampleLightingBuffer->Desc.Extent;

					GraphBuilder.QueueTextureExtraction(SampleLightingBuffer, 	&VisibilityData.SampleLightingBuffer);
					GraphBuilder.QueueTextureExtraction(CompactNodeIndex,		&VisibilityData.NodeIndex);
					GraphBuilder.QueueTextureExtraction(CategorizationTexture,	&VisibilityData.CategorizationTexture);
					GraphBuilder.QueueBufferExtraction(CompactNodeData,			&VisibilityData.NodeData,					FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(CompactNodeCoord,		&VisibilityData.NodeCoord,					FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer,		&VisibilityData.NodeIndirectArg,			FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
					GraphBuilder.QueueTextureExtraction(NodeCounter, 			&VisibilityData.NodeCount);
				}

				// View transmittance depth test needs to happen before the scene depth is patched with the hair depth (for fully-covered-by-hair pixels)
				if (ViewTransmittance.HairCountTexture)
				{
					AddHairViewTransmittanceDepthPass(
						GraphBuilder,
						View,
						CategorizationTexture,
						SceneDepthTexture,
						ViewTransmittance.HairCountTexture);
					GraphBuilder.QueueTextureExtraction(ViewTransmittance.HairCountTexture, &VisibilityData.ViewHairCountTexture);
				}

				if (ViewTransmittance.HairCountTextureUint)
				{
					GraphBuilder.QueueTextureExtraction(ViewTransmittance.HairCountTextureUint, &VisibilityData.ViewHairCountUintTexture);
				}

				if (ViewTransmittance.DepthTextureUint)
				{
					GraphBuilder.QueueTextureExtraction(ViewTransmittance.DepthTextureUint, &VisibilityData.DepthTextureUint);
				}

				// For fully covered pixels, write: 
				// * black color into the scene color
				// * closest depth
				// * unlit shading model ID 
				if (bRunColorAndDepthPatching)
				{
					AddHairVisibilityColorAndDepthPatchPass(
						GraphBuilder,
						View,
						CategorizationTexture,
						SceneGBufferBTexture,
						SceneColorTexture,
						SceneDepthTexture);
				}
			}
			else if (RenderMode == HairVisibilityRenderMode_PPLL)
			{
				// In this pas we reuse the scene depth buffer to cull hair pixels out.
				// Pixel data is accumulated in buffer containing data organized in a linked list with node scattered in memory according to pixel shader execution. 
				// This with up to width * height * GHairVisibilityPPLLGlobalMaxPixelNodeCount node total maximum.
				// After we have that a node sorting pass happening and we finally output all the data once into the common compaction node list.

				FRDGTextureRef PPLLNodeCounterTexture;
				FRDGTextureRef PPLLNodeIndexTexture;
				FRDGBufferRef PPLLNodeDataBuffer;
				FRDGTextureRef ViewZDepthTexture = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ);

				// Linked list generation pass
				AddHairVisibilityPPLLPass(GraphBuilder, Scene, &View, MacroGroupDatas, Resolution, ViewZDepthTexture, PPLLNodeCounterTexture, PPLLNodeIndexTexture, PPLLNodeDataBuffer);

				// Linked list sorting pass and compaction into common representation
				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->PPLLCounter  = PPLLNodeCounterTexture;
					PassParameters->PPLLNodeIndex= PPLLNodeIndexTexture;
					PassParameters->PPLLNodeData = GraphBuilder.CreateSRV(PPLLNodeDataBuffer);
					PassParameters->ViewTransmittanceTexture = ViewTransmittance.TransmittanceTexture;

					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					AddHairVisibilityPrimitiveIdCompactionPass(
						true, // bUsePPLL
						false,
						GraphBuilder,
						View,
						MacroGroupDatas,
						VisibilityData.NodeGroupSize,
						PassParameters,
						NodeCounter,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer,
						VisibilityData.MaxNodeCount);

					VisibilityData.MaxSampleCount = GetPPLLMaxRenderNodePerPixel();
					GraphBuilder.QueueTextureExtraction(CompactNodeIndex, &VisibilityData.NodeIndex);
					GraphBuilder.QueueTextureExtraction(CategorizationTexture, &VisibilityData.CategorizationTexture);
					GraphBuilder.QueueBufferExtraction(CompactNodeData, &VisibilityData.NodeData, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(CompactNodeCoord, &VisibilityData.NodeCoord, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer, &VisibilityData.NodeIndirectArg, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
					GraphBuilder.QueueTextureExtraction(NodeCounter, &VisibilityData.NodeCount);
				}

				if (bRunColorAndDepthPatching)
				{
					AddHairVisibilityColorAndDepthPatchPass(
						GraphBuilder,
						View,
						CategorizationTexture,
						SceneGBufferBTexture,
						SceneColorTexture,
						SceneDepthTexture);
				}

				// Allocate buffer for storing all the light samples
				FRDGTextureRef SampleLightingBuffer = AddClearLightSamplePass(GraphBuilder, &View, VisibilityData.MaxNodeCount, NodeCounter);
				VisibilityData.SampleLightingViewportResolution = SampleLightingBuffer->Desc.Extent;
				GraphBuilder.QueueTextureExtraction(SampleLightingBuffer, &VisibilityData.SampleLightingBuffer);

#if WITH_EDITOR
				// Extract texture for debug visualization
				GraphBuilder.QueueTextureExtraction(PPLLNodeCounterTexture, &VisibilityData.PPLLNodeCounterTexture);
				GraphBuilder.QueueTextureExtraction(PPLLNodeIndexTexture, &VisibilityData.PPLLNodeIndexTexture);
				GraphBuilder.QueueBufferExtraction(PPLLNodeDataBuffer, &VisibilityData.PPLLNodeDataBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
#endif
			}

		#if RHI_RAYTRACING
			if (IsRayTracingEnabled())
			{
				FRDGTextureRef LightingChannelMaskTexture = AddHairLightChannelMaskPass(
					GraphBuilder,
					View,
					Resolution,
					CompactNodeData,
					CompactNodeIndex);
				GraphBuilder.QueueTextureExtraction(LightingChannelMaskTexture, &VisibilityData.LightChannelMaskTexture);
			}
		#endif

			// Generate Tile data
			{
				FRDGTextureRef TileIndexTexture = nullptr;
				FRDGBufferRef TileBuffer = nullptr;
				FRDGBufferRef TileIndirectArgs = nullptr;
				AddGenerateTilePass(GraphBuilder, View, VisibilityData.TileThreadGroupSize, VisibilityData.TileSize, CategorizationTexture, TileIndexTexture, TileBuffer, TileIndirectArgs);

				GraphBuilder.QueueTextureExtraction(TileIndexTexture, &VisibilityData.TileIndexTexture);
				GraphBuilder.QueueBufferExtraction(TileBuffer, &VisibilityData.TileBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
				GraphBuilder.QueueBufferExtraction(TileIndirectArgs, &VisibilityData.TileIndirectArgs, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
			}

			GraphBuilder.Execute();

			// #hair_todo: is there a better way to get SRV view of a RDG buffer? should work as long as there is not reuse between the pass
			if (VisibilityData.NodeData)
			{
				VisibilityData.NodeDataSRV = RHICreateShaderResourceView(VisibilityData.NodeData->StructuredBuffer);
			}

			if (VisibilityData.NodeCoord)
			{
				VisibilityData.NodeCoordSRV = RHICreateShaderResourceView(VisibilityData.NodeCoord->StructuredBuffer);
			}
		}
	}

	return Output;
}
