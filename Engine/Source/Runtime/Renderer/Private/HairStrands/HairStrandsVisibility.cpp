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
static FAutoConsoleVariableRef CVarHairVisibilitySampleCount(TEXT("r.HairStrands.VisibilitySampleCount"), GHairVisibilitySampleCount, TEXT("Hair strands visibility sample count"));

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

static int32 GHairStrandsVisibilityMaterialPass = 0;
static FAutoConsoleVariableRef CVarHairStrandsVisibilityMaterialPass(TEXT("r.HairStrands.Visibility.MaterialPass"), GHairStrandsVisibilityMaterialPass, TEXT("Enable the deferred material pass evaluation after the hair visibility is resolved."));

static int32 GHairStrandsViewHairCount = 0;
static float GHairStrandsViewHairCountDepthDistanceThreshold = 30.f;
static FAutoConsoleVariableRef CVarHairStrandsViewHairCount(TEXT("r.HairStrands.Visibility.HairCount"), GHairStrandsViewHairCount, TEXT("Enable the computation of 'view-hair-count' during the transmission pass."));
static FAutoConsoleVariableRef CVarHairStrandsViewHairCountDepthDistanceThreshold(TEXT("r.HairStrands.Visibility.HairCount.DistanceThreshold"), GHairStrandsViewHairCountDepthDistanceThreshold, TEXT("Distance threshold defining if opaque depth get injected into the 'view-hair-count' buffer."));

/////////////////////////////////////////////////////////////////////////////////////////

namespace HairStrandsVisibilityInternal
{
	struct NodeData
	{
		uint32 Depth;
		uint32 PrimitiveId_ClusterId;
		uint32 Tangent_Coverage;
		uint32 BaseColor_Roughness;
		uint32 Specular;
	};

	// 128 bit alignment
	struct NodeVis
	{
		uint32 Depth;
		uint32 PrimitiveId_ClusterId;
		uint32 Coverage_ClusterID_Pad;
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

EHairVisibilityRenderMode GetHairVisibilityRenderMode()
{
	return GHairVisibilityPPLL > 0 ? HairVisibilityRenderMode_PPLL : HairVisibilityRenderMode_MSAA;
}

uint32 GetPPLLMeanListElementCountPerPixel()
{
	return GHairVisibilityPPLLMeanListElementCountPerPixel;
}
uint32 GetPPLLMaxTotalListElementCount(FIntPoint Resolution)
{
	return Resolution.X * Resolution.Y * GetPPLLMeanListElementCountPerPixel();
}

uint32 GetPPLLMaxRenderNodePerPixel()
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
uint32 GetPPLLMaxTotalRenderNode(FIntPoint Resolution)
{
	return Resolution.X * Resolution.Y * GetPPLLMaxRenderNodePerPixel();
}

uint32 GetHairVisibilitySampleCount()
{
	return GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? FMath::Clamp(GHairVisibilitySampleCount,1,16) : 1;
}

void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, bool bEnableMSAA, FVector4& OutHairRenderInfo)
{
	FVector2D PixelVelocity(1.f / (ViewInfo.ViewRect.Width() * 2), 1.f / (ViewInfo.ViewRect.Height() * 2));
	const float VelocityMagnitudeScale = FMath::Clamp(CVarHairVelocityMagnitudeScale.GetValueOnAnyThread(), 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);

	// In the case we render coverage, we need to override some view uniform shader parameters to account for the change in MSAA sample count.
	const uint32 HairVisibilitySampleCount = bEnableMSAA ? GetHairVisibilitySampleCount() : 1;	// The coverage pass does not use MSAA
	const float RasterizationScaleOverride = 0.0f;	// no override
	FMinHairRadiusAtDepth1 MinHairRadius = ComputeMinStrandRadiusAtDepth1(
		FIntPoint(ViewInfo.UnconstrainedViewRect.Width(), ViewInfo.UnconstrainedViewRect.Height()), ViewInfo.FOV, HairVisibilitySampleCount, RasterizationScaleOverride);

	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
	// Update our view parameters
	OutHairRenderInfo.X = MinHairRadius.Primary;
	OutHairRenderInfo.Y = MinHairRadius.Velocity;
	OutHairRenderInfo.Z = ViewInfo.IsPerspectiveProjection() ? 0.0f : 1.0f;
	OutHairRenderInfo.W = VelocityMagnitudeScale;
}

static bool IsCompatibleWithHairVisibility(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	return IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform);
}

/////////////////////////////////////////////////////////////////////////////////////////

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMaterialPassParameters, )
	SHADER_PARAMETER(FIntPoint, MaxResolution)
	SHADER_PARAMETER(uint32, MaxSampleCount)
	SHADER_PARAMETER(uint32, NodeGroupSize)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, NodeIndex)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, NodeCoord)
	SHADER_PARAMETER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndirectArgs)
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
	FHairMaterialShaderElementData(int32 ClusterId, int32 MaterialId, int32 PrimitiveId) : MaterialPass_ClusterId(ClusterId), MaterialPass_MaterialId(MaterialId), MaterialPass_PrimitiveId(PrimitiveId) { }
	uint32 MaterialPass_ClusterId;
	uint32 MaterialPass_MaterialId;
	uint32 MaterialPass_PrimitiveId;
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
		MaterialPass_ClusterId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_ClusterId"));
		MaterialPass_MaterialId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_MaterialId"));
		MaterialPass_PrimitiveId.Bind(Initializer.ParameterMap, TEXT("MaterialPass_PrimitiveId"));

		// Fake binding for avoiding error from the shader compiler. These output are actually properly bound by the rendering 
		// pass, and not directly by this material shader
		OutNodeData.Bind(Initializer.ParameterMap, TEXT("OutNodeData"));
		OutNodeVelocity.Bind(Initializer.ParameterMap, TEXT("OutNodeVelocity"));
	}

	FHairMaterialPS() {}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const bool bIsCompatible = IsCompatibleWithHairStrands(Parameters.Material, Parameters.Platform) && Parameters.VertexFactoryType->GetFName() == FName(TEXT("FHairStrandsVertexFactory"));
		return bIsCompatible;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << MaterialPass_ClusterId;
		Ar << MaterialPass_MaterialId;
		Ar << MaterialPass_PrimitiveId;
		Ar << OutNodeData;
		Ar << OutNodeVelocity;
		return bShaderHasOutdatedParameters;
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
		ShaderBindings.Add(MaterialPass_ClusterId, ShaderElementData.MaterialPass_ClusterId);
		ShaderBindings.Add(MaterialPass_MaterialId, ShaderElementData.MaterialPass_MaterialId);
		ShaderBindings.Add(MaterialPass_PrimitiveId, ShaderElementData.MaterialPass_PrimitiveId);
	}

private:
	FShaderParameter MaterialPass_ClusterId;
	FShaderParameter MaterialPass_MaterialId;
	FShaderParameter MaterialPass_PrimitiveId;
	FShaderParameter OutNodeData;
	FShaderParameter OutNodeVelocity;
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
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 HairClusterId, int32 HairMaterialId);

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		const int32 HairClusterId,
		const int32 HairMaterialId,
		const int32 HairPrimitiveId);

	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FHairMaterialProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0);
}

void FHairMaterialProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, int32 HairClusterId, int32 HairMaterialId)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetId() == CompatibleVF->GetId();
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
		}

		int32 PrimitiveId = 0;
		int32 ScenePrimitiveId = 0;
		FPrimitiveSceneInfo* SceneInfo = PrimitiveSceneProxy ? PrimitiveSceneProxy->GetPrimitiveSceneInfo() : nullptr;
		GetDrawCommandPrimitiveId(SceneInfo, MeshBatch.Elements[0], PrimitiveId, ScenePrimitiveId);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		Process(MeshBatchCopy, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, HairMaterialId, PrimitiveId);
	}
}

void FHairMaterialProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	const int32 HairClusterId,
	const int32 HairMaterialId,
	const int32 HairPrimitiveId)
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
	FHairMaterialShaderElementData ShaderElementData(HairClusterId, HairMaterialId, HairPrimitiveId);
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
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,	NodeCoord)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNodeVis>, NodeVis)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNodeData>, OutNodeData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float2>, OutNodeVelocity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

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
	const FHairStrandsClusterDatas& ClusterDatas,
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
		[PassParameters, Scene = Scene, ViewInfo, &ClusterDatas, MaxNodeCount, Resolution, NodeGroupSize](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FMaterialPassParameters MaterialPassParameters;
		MaterialPassParameters.MaxResolution	= Resolution;
		MaterialPassParameters.NodeGroupSize	= NodeGroupSize;
		MaterialPassParameters.MaxSampleCount	= MaxNodeCount;
		MaterialPassParameters.NodeIndex		= PassParameters->NodeIndex->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture;
		MaterialPassParameters.NodeCoord		= PassParameters->NodeCoord->GetRHI();
		MaterialPassParameters.NodeVis			= PassParameters->NodeVis->GetRHI();
		MaterialPassParameters.IndirectArgs		= PassParameters->IndirectArgs->GetRHI();
		TUniformBufferRef<FMaterialPassParameters> MaterialPassParametersBuffer = TUniformBufferRef<FMaterialPassParameters>::CreateUniformBufferImmediate(MaterialPassParameters, UniformBuffer_SingleFrame);

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo, MaterialPassParametersBuffer);
		{
			TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
			const bool bEnableMSAA = false;
			SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo);
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
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet);
			FHairMaterialProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, &ShadowContext);

			for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
			{
				for (const FHairStrandsClusterData::PrimitiveInfo& PrimitiveInfo : ClusterData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, ClusterData.ClusterId, PrimitiveInfo.MaterialId);
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
	const FHairStrandsClusterDatas& ClusterDatas,
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

	FIntRect TotalRect = ComputeVisibleHairStrandsClustersRect(View.ViewRect, ClusterDatas);

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
		*ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(RectResolution, GroupSize));
}

/////////////////////////////////////////////////////////////////////////////////////////
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassGlobalParameters, )
	SHADER_PARAMETER(uint32, MaxPPLLNodeCount)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVisibilityPassGlobalParameters, "VisibilityPassGlobalParameters");

BEGIN_SHADER_PARAMETER_STRUCT(FVisibilityPassParameters, )
	SHADER_PARAMETER(uint32, HairVisibilityPass_MaxPPLLNodeCount)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, PPLLCounter)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, PPLLNodeIndex)
	SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, PPLLNodeData)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

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
	}
};
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_MSAA_Visibility>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_MSAA>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_Transmittance>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_TransmittanceAndHairCount>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FHairVisibilityVS<HairVisibilityRenderMode_PPLL>, TEXT("/Engine/Private/HairStrands/HairStrandsVisibilityVS.usf"), TEXT("Main"), SF_Vertex);

/////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FHairVisibilityShaderElementData(uint32 InHairClusterId, uint32 InHairMaterialId, uint32 InMaxPPLLNodeCount) : HairClusterId(InHairClusterId), HairMaterialId(InHairMaterialId), MaxPPLLNodeCount(InMaxPPLLNodeCount) { }
	uint32 HairClusterId;
	uint32 HairMaterialId;
	uint32 MaxPPLLNodeCount;
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
		HairVisibilityPass_HairClusterIndex.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairClusterIndex"));
		HairVisibilityPass_HairMaterialId.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_HairMaterialId"));
		HairVisibilityPass_MaxPPLLNodeCount.Bind(Initializer.ParameterMap, TEXT("HairVisibilityPass_MaxPPLLNodeCount"));
		PPLLCounter.Bind(Initializer.ParameterMap, TEXT("PPLLCounter"));
		PPLLNodeIndex.Bind(Initializer.ParameterMap, TEXT("PPLLNodeIndex"));
		PPLLNodes.Bind(Initializer.ParameterMap, TEXT("PPLLNodes"));
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
		Ar << HairVisibilityPass_HairMaterialId;
		Ar << HairVisibilityPass_MaxPPLLNodeCount;
		Ar << PPLLCounter;
		Ar << PPLLNodeIndex;
		Ar << PPLLNodes;
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
		ShaderBindings.Add(HairVisibilityPass_HairMaterialId, ShaderElementData.HairMaterialId);
		ShaderBindings.Add(HairVisibilityPass_MaxPPLLNodeCount, ShaderElementData.MaxPPLLNodeCount);
	}

	FShaderParameter HairVisibilityPass_HairClusterIndex;
	FShaderParameter HairVisibilityPass_HairMaterialId;
	FShaderParameter HairVisibilityPass_MaxPPLLNodeCount;
	// This is on FVisibilityPassParameters but needed to avoid the compiler to assert with unbound parameters 
	FShaderParameter PPLLCounter;
	FShaderParameter PPLLNodeIndex;
	FShaderParameter PPLLNodes;
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
	void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairClusterId, uint32 HairMaterialId);

	void SetMaxPPLLNodeCount(uint32 NewValue) { MaxPPLLNodeCount = NewValue; }

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
		const uint32 HairMaterialId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	const EHairVisibilityRenderMode RenderMode;
	FMeshPassProcessorRenderState PassDrawRenderState;
	uint32 MaxPPLLNodeCount = 0;
};

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, 0, 0);
}

void FHairVisibilityProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId, uint32 HairClusterId, uint32 HairMaterialId)
{
	static const FVertexFactoryType* CompatibleVF = FVertexFactoryType::GetVFByName(TEXT("FHairStrandsVertexFactory"));

	// Determine the mesh's material and blend mode.
	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const bool bIsCompatible = IsCompatibleWithHairStrands(&Material, FeatureLevel);
	const bool bIsHairStrandsFactory = MeshBatch.VertexFactory->GetType()->GetId() == CompatibleVF->GetId();
	const bool bShouldRender = (!PrimitiveSceneProxy && MeshBatch.Elements.Num()>0) || (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass());

	if (bIsCompatible 
		&& bIsHairStrandsFactory
		&& bShouldRender
		&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material);
		if (RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
			Process<HairVisibilityRenderMode_MSAA_Visibility>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, HairMaterialId, MeshFillMode, MeshCullMode);
		if (RenderMode == HairVisibilityRenderMode_MSAA)
			Process<HairVisibilityRenderMode_MSAA>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, HairMaterialId, MeshFillMode, MeshCullMode);
		if (RenderMode == HairVisibilityRenderMode_Transmittance)
			Process<HairVisibilityRenderMode_Transmittance>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, HairMaterialId, MeshFillMode, MeshCullMode);
		if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
			Process<HairVisibilityRenderMode_TransmittanceAndHairCount>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, HairMaterialId, MeshFillMode, MeshCullMode);
		if (RenderMode == HairVisibilityRenderMode_PPLL)
			Process<HairVisibilityRenderMode_PPLL>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, MaterialRenderProxy, Material, HairClusterId, HairMaterialId, MeshFillMode, MeshCullMode);
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
	const uint32 HairMaterialId,
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
	FHairVisibilityShaderElementData ShaderElementData(HairClusterId, HairMaterialId, MaxPPLLNodeCount);
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

	class FVendor		: SHADER_PERMUTATION_INT("PERMUTATION_VENDOR", HairVisibilityVendorCount);
	class FVelocity		: SHADER_PERMUTATION_INT("PERMUTATION_VELOCITY", 4);
	class FViewTransmittance : SHADER_PERMUTATION_INT("PERMUTATION_VIEWTRANSMITTANCE", 2);
	class FMaterial		: SHADER_PERMUTATION_INT("PERMUTATION_MATERIAL_COMPACTION", 2);
	class FPPLL			: SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_PPLL", 0, 8, 16, 32); // See GetPPLLMaxRenderNodePerPixel
	class FVisibility	: SHADER_PERMUTATION_INT("PERMUTATION_VISIBILITY", 2);
	using FPermutationDomain = TShaderPermutationDomain<FVendor, FVelocity, FViewTransmittance, FMaterial, FPPLL, FVisibility>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(uint32, MaxNodeCount)
		SHADER_PARAMETER(uint32, HairVisibilityMSAASampleCount)
		SHADER_PARAMETER(FIntPoint, ResolutionOffset)
		SHADER_PARAMETER(float, DepthTheshold)
		SHADER_PARAMETER(float, CosTangentThreshold)

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
		return IsHairStrandsSupported(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityPrimitiveIdCompactionCS, "/Engine/Private/HairStrands/HairStrandsVisibilityCompaction.usf", "MainCS", SF_Compute);

static void AddHairVisibilityPrimitiveIdCompactionPass(
	const bool bUsePPLL,
	const bool bUseVisibility,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsClusterDatas& ClusterDatas,
	const uint32 NodeGroupSize,
	FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters,
	FRDGTextureRef& OutCompactNodeIndex,
	FRDGBufferRef& OutCompactNodeData,
	FRDGBufferRef& OutCompactNodeCoord,
	FRDGTextureRef& OutCategorizationTexture,
	FRDGTextureRef& OutVelocityTexture,
	FRDGBufferRef& OutIndirectArgsBuffer)
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

	// Select render node count according to current mode
	const uint32 HairVisibilityMSAASampleCount = GetHairVisibilitySampleCount();
	const uint32 SampleCount = FMath::RoundUpToPowerOfTwo(HairVisibilityMSAASampleCount);
	const uint32 PPLLMaxRenderNodePerPixel = GetPPLLMaxRenderNodePerPixel();
	const uint32 MaxRenderNodeCount = Resolution.X * Resolution.Y * (GetHairVisibilityRenderMode() == HairVisibilityRenderMode_MSAA ? SampleCount : PPLLMaxRenderNodePerPixel);
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
	PermutationVector = FHairVisibilityPrimitiveIdCompactionCS::RemapPermutation(PermutationVector);

	PassParameters->OutputResolution = Resolution;
	PassParameters->MaxNodeCount = MaxRenderNodeCount;
	PassParameters->DepthTheshold = FMath::Clamp(GHairStrandsMaterialCompactionDepthThreshold, 0.f, 100.f);
	PassParameters->CosTangentThreshold = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(GHairStrandsMaterialCompactionTangentThreshold, 0.f, 90.f)));
	PassParameters->HairVisibilityMSAASampleCount = HairVisibilityMSAASampleCount;
	PassParameters->SceneTexturesStruct = CreateUniformBufferImmediate(SceneTextures, EUniformBufferUsage::UniformBuffer_SingleDraw);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->OutCompactNodeCounter = GraphBuilder.CreateUAV(CompactCounter);
	PassParameters->OutCompactNodeIndex = GraphBuilder.CreateUAV(OutCompactNodeIndex);
	PassParameters->OutCompactNodeData = GraphBuilder.CreateUAV(OutCompactNodeData);
	PassParameters->OutCompactNodeCoord = GraphBuilder.CreateUAV(OutCompactNodeCoord);
	PassParameters->OutCategorizationTexture = GraphBuilder.CreateUAV(OutCategorizationTexture);

	if (bWriteOutVelocity)
	{
		PassParameters->OutVelocityTexture = GraphBuilder.CreateUAV(OutVelocityTexture);
	}
 
	FIntRect TotalRect = ComputeVisibleHairStrandsClustersRect(View.ViewRect, ClusterDatas);

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
		*ComputeShader,
		PassParameters,
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
	if (IsHairStrandsViewRectOptimEnable())
	{
		for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
		{
			ClusterRects.Add(ClusterData.ScreenRect);
		}
	}
	else
	{
		ClusterRects.Add(Viewport);
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

template <typename TPassParameters>
static void AddHairVisibilityCommonPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterDatas& ClusterDatas,
	const EHairVisibilityRenderMode RenderMode,
	TPassParameters* PassParameters)
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
		[PassParameters, Scene = Scene, ViewInfo, &ClusterDatas, RenderMode](FRHICommandListImmediate& RHICmdList)
	{
		check(RHICmdList.IsInsideRenderPass());
		check(IsInRenderingThread());

		FMeshPassProcessorRenderState DrawRenderState(*ViewInfo);

		if(RenderMode == HairVisibilityRenderMode_Transmittance || RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount || RenderMode == HairVisibilityRenderMode_PPLL)
		{
			TUniformBufferRef<FViewUniformShaderParameters> ViewUniformShaderParameters;
			const bool bEnableMSAA = false;
			SetUpViewHairRenderInfo(*ViewInfo, bEnableMSAA, ViewInfo->CachedViewUniformShaderParameters->HairRenderInfo);
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
			else if(RenderMode == HairVisibilityRenderMode_MSAA_Visibility)
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
					CW_RED, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_Zero>::GetRHI());
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
			FDynamicPassMeshDrawListContext ShadowContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, PipelineStateSet);
			FHairVisibilityProcessor MeshProcessor(Scene, ViewInfo, DrawRenderState, RenderMode, &ShadowContext);
			if (RenderMode == HairVisibilityRenderMode_PPLL)
			{
				// Work around because the value on the PassParamter is not taken into account for the PS global constant buffer.
				// TODO fix that by having FVisibilityPassParameters be a global parameter structure.
				MeshProcessor.SetMaxPPLLNodeCount(PassParameters->HairVisibilityPass_MaxPPLLNodeCount);
			}

			for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
			{
				for (const FHairStrandsClusterData::PrimitiveInfo& PrimitiveInfo : ClusterData.PrimitivesInfos)
				{
					const FMeshBatch& MeshBatch = *PrimitiveInfo.MeshBatchAndRelevance.Mesh;
					const uint64 BatchElementMask = ~0ull;
					MeshProcessor.AddMeshBatch(MeshBatch, BatchElementMask, PrimitiveInfo.MeshBatchAndRelevance.PrimitiveSceneProxy, -1, ClusterData.ClusterId, PrimitiveInfo.MaterialId);
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
	const FHairStrandsClusterDatas& ClusterDatas,
	const FIntPoint& Resolution,
	FRDGTextureRef& OutVisibilityIdTexture,
	FRDGTextureRef& OutVisibilityMaterialTexture,
	FRDGTextureRef& OutVisibilityAttributeTexture,
	FRDGTextureRef& OutVisibilityVelocityTexture,
	FRDGTextureRef& OutVisibilityDepthTexture)
{
	const uint32 MSAASampleCount = FMath::RoundUpToPowerOfTwo(FMath::Clamp(GHairVisibilitySampleCount, 1, 16));

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
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, ClusterDatas, HairVisibilityRenderMode_MSAA_Visibility, PassParameters);
	}
	else
	{
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
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutVisibilityIdTexture, ERenderTargetLoadAction::ELoad, 0);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(OutVisibilityMaterialTexture,  LoadAction, 0);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(OutVisibilityAttributeTexture, LoadAction, 0);
		PassParameters->RenderTargets[3] = FRenderTargetBinding(OutVisibilityVelocityTexture,  LoadAction, 0);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			OutVisibilityDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);
		AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, ClusterDatas, HairVisibilityRenderMode_MSAA, PassParameters);
	}
}

static void AddHairVisibilityPPLLPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterDatas& ClusterDatas,
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
		// Example: 28bytes * 8spp = 224bytes per pixel = 442Mb @ 1080p
		struct PPLLNodeData
		{
			uint32 Depth;
			uint32 PrimitiveId_ClusterId;
			uint32 Tangent_Coverage;
			uint32 BaseColor_Roughness;
			uint32 Specular;
			uint32 NextNodeIndex;
			uint32 PackedVelocity;
		};

		OutVisibilityPPLLNodeData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(PPLLNodeData), PPLLMaxTotalListElementCount), TEXT("HairVisibilityPPLLNodeData"));
	}
	AddClearUAVPass(GraphBuilder, RDG_EVENT_NAME("ClearHairVisibilityPPLLCounter"), OutVisibilityPPLLNodeCounter, 0);
	AddClearUAVPass(GraphBuilder, RDG_EVENT_NAME("ClearHairVisibilityPPLLNodeIndex"), OutVisibilityPPLLNodeIndex, 0xFFFFFFFF);

	FVisibilityPassParameters* PassParameters = GraphBuilder.AllocParameters<FVisibilityPassParameters>();
	PassParameters->PPLLCounter = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeCounter, 0));
	PassParameters->PPLLNodeIndex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutVisibilityPPLLNodeIndex, 0));
	PassParameters->PPLLNodeData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutVisibilityPPLLNodeData));
	PassParameters->HairVisibilityPass_MaxPPLLNodeCount = PPLLMaxTotalListElementCount;
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(InViewZDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, ClusterDatas, HairVisibilityRenderMode_PPLL, PassParameters);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairPrimaryTransmittance
{
	FRDGTextureRef TransmittanceTexture = nullptr;
	FRDGTextureRef HairCountTexture = nullptr;
};

static FHairPrimaryTransmittance AddHairViewTransmittancePass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterDatas& ClusterDatas,
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
	FHairPrimaryTransmittance Out;

	Out.TransmittanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewTransmittanceTexture"));
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Out.TransmittanceTexture, ERenderTargetLoadAction::EClear, 0);

	if (RenderMode == HairVisibilityRenderMode_TransmittanceAndHairCount)
	{
		Desc.ClearValue = FClearValueBinding(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Out.HairCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairViewHairCountTexture"));
		PassParameters->RenderTargets[1] = FRenderTargetBinding(Out.HairCountTexture, ERenderTargetLoadAction::EClear, 0);
	}

	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		SceneDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction,
		FExclusiveDepthStencil::DepthRead_StencilNop);
	AddHairVisibilityCommonPass(GraphBuilder, Scene, ViewInfo, ClusterDatas, RenderMode, PassParameters);

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
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = HairCountTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;
	ClearUnusedGraphResources(*PixelShader, Parameters);

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

			// Run the view transmittance pass if needed (not in PPLL mode that is already a high quality render path)
			FHairPrimaryTransmittance ViewTransmittance;
			if (GHairStrandsViewTransmittancePassEnable > 0 && RenderMode != HairVisibilityRenderMode_PPLL)
			{
				const bool bOutputHairCount = GHairStrandsViewHairCount > 0;
				ViewTransmittance = AddHairViewTransmittancePass(
					GraphBuilder,
					Scene,
					&View,
					ClusterDatas,
					Resolution, 
					bOutputHairCount,
					SceneDepthTexture);
			}

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
					ClusterDatas,
					SceneDepthTexture);

				AddHairVisibilityMSAAPass(
					bIsVisiblityEnable,
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
				GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.DepthTexture, &VisibilityData.DepthTexture);
				if (!bIsVisiblityEnable)
				{
					GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.MaterialTexture, &VisibilityData.MaterialTexture);
					GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.AttributeTexture, &VisibilityData.AttributeTexture);
					GraphBuilder.QueueTextureExtraction(MsaaVisibilityResources.VelocityTexture, &VisibilityData.VelocityTexture);
				}

				FRDGTextureRef CategorizationTexture;
				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->MSAA_DepthTexture = MsaaVisibilityResources.DepthTexture;
					PassParameters->MSAA_IDTexture = MsaaVisibilityResources.IdTexture;
					PassParameters->MSAA_MaterialTexture = MsaaVisibilityResources.MaterialTexture;
					PassParameters->MSAA_AttributeTexture = MsaaVisibilityResources.AttributeTexture;
					PassParameters->MSAA_VelocityTexture = MsaaVisibilityResources.VelocityTexture;
					PassParameters->ViewTransmittanceTexture = ViewTransmittance.TransmittanceTexture;

					FRDGTextureRef CompactNodeIndex;
					FRDGBufferRef CompactNodeData;
					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					AddHairVisibilityPrimitiveIdCompactionPass(
						false, // bUsePPLL
						bIsVisiblityEnable,
						GraphBuilder,
						View,
						ClusterDatas,
						VisibilityData.NodeGroupSize,
						PassParameters,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer);

					if (bIsVisiblityEnable)
					{
						// Evaluate material based on the visiblity pass result
						// Output both complete sample data + per-sample velocity
						FMaterialPassOutput PassOutput = AddHairMaterialPass(
							GraphBuilder,
							Scene,
							&View,
							ClusterDatas,
							VisibilityData.NodeGroupSize,
							CompactNodeIndex,
							CompactNodeData,
							CompactNodeCoord,
							IndirectArgsBuffer);

						// Merge per-sample velocity into the scene velocity buffer
						AddHairVelocityPass(
							GraphBuilder,
							View,
							ClusterDatas,
							CompactNodeIndex,
							CompactNodeData,
							PassOutput.NodeVelocity,
							SceneVelocityTexture);

						CompactNodeData = PassOutput.NodeData;
					}

					GraphBuilder.QueueTextureExtraction(CompactNodeIndex,		&VisibilityData.NodeIndex);
					GraphBuilder.QueueTextureExtraction(CategorizationTexture,	&VisibilityData.CategorizationTexture);
					GraphBuilder.QueueBufferExtraction(CompactNodeData,			&VisibilityData.NodeData,					FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(CompactNodeCoord,		&VisibilityData.NodeCoord,					FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer,		&VisibilityData.NodeIndirectArg,			FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
				}

				// For fully covered pixels, write: 
				// * black color into the scene color
				// * closest depth
				// * unlit shading model ID 
				AddHairVisibilityColorAndDepthPatchPass(
					GraphBuilder,
					View,
					CategorizationTexture,
					SceneGBufferBTexture,
					SceneColorTexture,
					SceneDepthTexture);

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
				AddHairVisibilityPPLLPass(GraphBuilder, Scene, &View, ClusterDatas, Resolution, ViewZDepthTexture, PPLLNodeCounterTexture, PPLLNodeIndexTexture, PPLLNodeDataBuffer);

				// Linked list sorting pass and compaction into common representation
				FRDGTextureRef CategorizationTexture;
				{
					FHairVisibilityPrimitiveIdCompactionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityPrimitiveIdCompactionCS::FParameters>();
					PassParameters->PPLLCounter  = PPLLNodeCounterTexture;
					PassParameters->PPLLNodeIndex= PPLLNodeIndexTexture;
					PassParameters->PPLLNodeData = GraphBuilder.CreateSRV(PPLLNodeDataBuffer);
					PassParameters->ViewTransmittanceTexture = ViewTransmittance.TransmittanceTexture;

					FRDGTextureRef CompactNodeIndex;
					FRDGBufferRef CompactNodeData;
					FRDGBufferRef CompactNodeCoord;
					FRDGBufferRef IndirectArgsBuffer;
					AddHairVisibilityPrimitiveIdCompactionPass(
						true, // bUsePPLL
						false,
						GraphBuilder,
						View,
						ClusterDatas,
						VisibilityData.NodeGroupSize,
						PassParameters,
						CompactNodeIndex,
						CompactNodeData,
						CompactNodeCoord,
						CategorizationTexture,
						SceneVelocityTexture,
						IndirectArgsBuffer);
					GraphBuilder.QueueTextureExtraction(CompactNodeIndex, &VisibilityData.NodeIndex);
					GraphBuilder.QueueTextureExtraction(CategorizationTexture, &VisibilityData.CategorizationTexture);
					GraphBuilder.QueueBufferExtraction(CompactNodeData, &VisibilityData.NodeData, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(CompactNodeCoord, &VisibilityData.NodeCoord, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
					GraphBuilder.QueueBufferExtraction(IndirectArgsBuffer, &VisibilityData.NodeIndirectArg, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
				}

				AddHairVisibilityColorAndDepthPatchPass(
					GraphBuilder,
					View,
					CategorizationTexture,
					SceneGBufferBTexture,
					SceneColorTexture,
					SceneDepthTexture);

#if WITH_EDITOR
				// Extract texture for debug visualization
				GraphBuilder.QueueTextureExtraction(PPLLNodeCounterTexture, &VisibilityData.PPLLNodeCounterTexture);
				GraphBuilder.QueueTextureExtraction(PPLLNodeIndexTexture, &VisibilityData.PPLLNodeIndexTexture);
				GraphBuilder.QueueBufferExtraction(PPLLNodeDataBuffer, &VisibilityData.PPLLNodeDataBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
#endif
			}

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
