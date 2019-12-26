// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDebug.h"
#include "HairStrandsInterface.h"
#include "HairStrandsCluster.h"
#include "HairStrandsDeepShadow.h"
#include "HairStrandsUtils.h"
#include "HairStrandsVoxelization.h"
#include "HairStrandsRendering.h"
#include "HairStrandsVisibility.h"
#include "HairStrandsInterface.h"
#include "HairStrandsMeshProjection.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "SceneTextureParameters.h"
#include "DynamicPrimitiveDrawing.h"
#include "RenderTargetTemp.h"
#include "CanvasTypes.h"
#include "ShaderPrintParameters.h"
#include "RenderGraphUtils.h"

static int32 GDeepShadowDebugIndex = 0;
static float GDeepShadowDebugScale = 20;

static FAutoConsoleVariableRef CVarDeepShadowDebugDomIndex(TEXT("r.HairStrands.DeepShadow.DebugDOMIndex"), GDeepShadowDebugIndex, TEXT("Index of the DOM texture to draw"));
static FAutoConsoleVariableRef CVarDeepShadowDebugDomScale(TEXT("r.HairStrands.DeepShadow.DebugDOMScale"), GDeepShadowDebugScale, TEXT("Scaling value for the DeepOpacityMap when drawing the deep shadow stats"));

static int32 GHairStrandsDebugMode = 0;
static FAutoConsoleVariableRef CVarDeepShadowStats(TEXT("r.HairStrands.DebugMode"), GHairStrandsDebugMode, TEXT("Draw various stats/debug mode about hair rendering"));

static int32 GHairStrandsDebugStrandsMode = 0;
static FAutoConsoleVariableRef CVarDebugPhysicsStrand(TEXT("r.HairStrands.StrandsMode"), GHairStrandsDebugStrandsMode, TEXT("Render debug mode for hair strands. 0:off, 1:simulation strands, 2:render strands with colored simulation strands influence, 3:hair UV, 4:hair root UV, 5: hair seed, 6: dimensions"));

static int32 GHairStrandsDebugPlotBsdf = 0;
static FAutoConsoleVariableRef CVarHairStrandsDebugBSDF(TEXT("r.HairStrands.PlotBsdf"), GHairStrandsDebugPlotBsdf, TEXT("Debug view for visualizing hair BSDF."));

static float GHairStrandsDebugPlotBsdfRoughness = 0.3f;
static FAutoConsoleVariableRef CVarHairStrandsDebugBSDFRoughness(TEXT("r.HairStrands.PlotBsdf.Roughness"), GHairStrandsDebugPlotBsdfRoughness, TEXT("Change the roughness of the debug BSDF plot."));

static float GHairStrandsDebugPlotBsdfBaseColor = 1;
static FAutoConsoleVariableRef CVarHairStrandsDebugBSDFAbsorption(TEXT("r.HairStrands.PlotBsdf.BaseColor"), GHairStrandsDebugPlotBsdfBaseColor, TEXT("Change the base color / absorption of the debug BSDF plot."));

static float GHairStrandsDebugPlotBsdfExposure = 1.1f;
static FAutoConsoleVariableRef CVarHairStrandsDebugBSDFExposure(TEXT("r.HairStrands.PlotBsdf.Exposure"), GHairStrandsDebugPlotBsdfExposure, TEXT("Change the exposure of the plot."));

static int GHairStrandsDebugSampleIndex = -1;
static FAutoConsoleVariableRef CVarHairStrandsDebugMaterialSampleIndex(TEXT("r.HairStrands.DebugMode.SampleIndex"), GHairStrandsDebugSampleIndex, TEXT("Debug value for a given sample index (default:-1, i.e., average sample information)."));

static int32 GHairDebugMeshProjection_SkinCacheMesh = 0;

static int32 GHairDebugMeshProjection_Sim_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedFrames = 0;

static int32 GHairDebugMeshProjection_Render_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedFrames = 0;

static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMesh(					TEXT("r.HairStrands.MeshProjection.DebugSkinCache"),					GHairDebugMeshProjection_SkinCacheMesh, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestTriangles(		TEXT("r.HairStrands.MeshProjection.Render.DebugHairRestTriangles"),		GHairDebugMeshProjection_Render_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestFrames(			TEXT("r.HairStrands.MeshProjection.Render.DebugHairRestFrames"),		GHairDebugMeshProjection_Render_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedTriangles(	TEXT("r.HairStrands.MeshProjection.Render.DebugHairDeformedTriangles"),	GHairDebugMeshProjection_Render_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedFrames(		TEXT("r.HairStrands.MeshProjection.Render.DebugHairDeformedFrames"),	GHairDebugMeshProjection_Render_HairDeformedFrames, TEXT("Render debug mes projection"));

static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestTriangles(			TEXT("r.HairStrands.MeshProjection.Sim.DebugHairRestTriangles"),		GHairDebugMeshProjection_Sim_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestFrames(				TEXT("r.HairStrands.MeshProjection.Sim.DebugHairRestFrames"),			GHairDebugMeshProjection_Sim_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedTriangles(		TEXT("r.HairStrands.MeshProjection.Sim.DebugHairDeformedTriangles"),	GHairDebugMeshProjection_Sim_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedFrames(			TEXT("r.HairStrands.MeshProjection.Sim.DebugHairDeformedFrames"),		GHairDebugMeshProjection_Sim_HairDeformedFrames, TEXT("Render debug mes projection"));

static int32 GHairStrandsDebugPPLL = 0;
static FAutoConsoleVariableRef CVarHairStrandsDebugPPLL(									TEXT("r.HairStrands.DebugPPLL"),										GHairStrandsDebugPPLL, TEXT("Draw debug per pixel light list rendering."));

// Helper functions for accessing interpolation data for debug purpose.
// Definitions is in HairStrandsInterface.cpp
void GetGroomInterpolationData(const EWorldType::Type WorldType, FHairStrandsProjectionMeshData& OutGeometries);
void GetGroomInterpolationData(const EWorldType::Type WorldType, const bool bRenderData, FHairStrandsProjectionHairData& OutHairData, TArray<int32>& OutLODIndices);

static int32 GHairStrandsCull = 0;
static int32 GHairStrandsCullIndex = -1;
static int32 GHairStrandsUpdateCullIndex = 0;
static float GHairStrandsCullNormalizedIndex = -1;
static FAutoConsoleVariableRef CVarHairStrandsCull			(TEXT("r.HairStrands.Cull"), GHairStrandsCull, TEXT("Cull hair strands (0:disabled, 1: render cull, 2: sim cull)."));
static FAutoConsoleVariableRef CVarHairStrandsCullIndex		(TEXT("r.HairStrands.Cull.Index"), GHairStrandsCullIndex, TEXT("Hair strands index to be kept. Other will be culled."));
static FAutoConsoleVariableRef CVarHairStrandsUpdateCullIndex(TEXT("r.HairStrands.Cull.Update"), GHairStrandsUpdateCullIndex, TEXT("Update the guide index to be kept using mouse position for fast selection."));

FHairCullInfo GetHairStrandsCullInfo()
{
	FHairCullInfo Out;
	Out.CullMode		= GHairStrandsCull == 1 ? EHairCullMode::Render : (GHairStrandsCull == 2 ? EHairCullMode::Sim : EHairCullMode::None);
	Out.ExplicitIndex	= GHairStrandsCullIndex >= 0 ? GHairStrandsCullIndex : -1;
	Out.NormalizedIndex = GHairStrandsCullNormalizedIndex;
	return Out;
}

enum class EHairDebugMode : uint8
{
	None,
	ClusterData,
	LightBounds,
	DeepOpacityMaps,
	ClusterScreenRect,
	SamplePerPixel,
	CoverageType,
	TAAResolveType,
	VoxelsDensity,
	VoxelsTangent,
	VoxelsBaseColor,
	VoxelsRoughness,
	MeshProjection,
	Coverage,
	MaterialDepth,
	MaterialBaseColor,
	MaterialRoughness,
	MaterialSpecular,
	MaterialTangent
};

static EHairDebugMode GetHairDebugMode()
{
	switch (GHairStrandsDebugMode)
	{
	case 0:  return EHairDebugMode::None;
	case 1:  return EHairDebugMode::ClusterData;
	case 2:  return EHairDebugMode::LightBounds;
	case 3:  return EHairDebugMode::ClusterScreenRect;
	case 4:  return EHairDebugMode::DeepOpacityMaps;
	case 5:  return EHairDebugMode::SamplePerPixel;
	case 6:  return EHairDebugMode::TAAResolveType;
	case 7:  return EHairDebugMode::CoverageType;
	case 8:  return EHairDebugMode::VoxelsDensity;
	case 9:  return EHairDebugMode::VoxelsTangent;
	case 10: return EHairDebugMode::VoxelsBaseColor;
	case 11: return EHairDebugMode::VoxelsRoughness;
	case 12: return EHairDebugMode::MeshProjection;
	case 13: return EHairDebugMode::Coverage;
	case 14: return EHairDebugMode::MaterialDepth;
	case 15: return EHairDebugMode::MaterialBaseColor;
	case 16: return EHairDebugMode::MaterialRoughness;
	case 17: return EHairDebugMode::MaterialSpecular;
	case 18: return EHairDebugMode::MaterialTangent;
	default: return EHairDebugMode::None;
	};
}

static const TCHAR* ToString(EHairDebugMode DebugMode)
{
	switch (DebugMode)
	{
	case EHairDebugMode::None: return TEXT("None");
	case EHairDebugMode::ClusterData: return TEXT("Cluster info");
	case EHairDebugMode::LightBounds: return TEXT("All DOMs light bounds");
	case EHairDebugMode::ClusterScreenRect: return TEXT("Screen projected clusters");
	case EHairDebugMode::DeepOpacityMaps: return TEXT("Deep opacity maps");
	case EHairDebugMode::SamplePerPixel: return TEXT("Sub-pixel sample count");
	case EHairDebugMode::TAAResolveType: return TEXT("TAA resolve type (regular/responsive)");
	case EHairDebugMode::CoverageType: return TEXT("Type of hair coverage - Fully covered : Green / Partially covered : Red");
	case EHairDebugMode::VoxelsDensity: return TEXT("Hair density volume");
	case EHairDebugMode::VoxelsTangent: return TEXT("Hair tangent volume");
	case EHairDebugMode::VoxelsBaseColor: return TEXT("Hair base color volume");
	case EHairDebugMode::VoxelsRoughness: return TEXT("Hair roughness volume");
	case EHairDebugMode::MeshProjection: return TEXT("Hair mesh projection");
	case EHairDebugMode::Coverage: return TEXT("Hair coverage");
	case EHairDebugMode::MaterialDepth: return TEXT("Hair material depth");
	case EHairDebugMode::MaterialBaseColor: return TEXT("Hair material base color");
	case EHairDebugMode::MaterialRoughness: return TEXT("Hair material roughness");
	case EHairDebugMode::MaterialSpecular: return TEXT("Hair material specular");
	case EHairDebugMode::MaterialTangent: return TEXT("Hair material tangent");
	default: return TEXT("None");
	};
}


EHairStrandsDebugMode GetHairStrandsDebugStrandsMode()
{
	switch (GHairStrandsDebugStrandsMode)
	{
	case  0:  return EHairStrandsDebugMode::None;
	case  1:  return EHairStrandsDebugMode::SimHairStrands;
	case  2:  return EHairStrandsDebugMode::RenderHairStrands;
	case  3:  return EHairStrandsDebugMode::RenderHairRootUV;
	case  4:  return EHairStrandsDebugMode::RenderHairRootUDIM;
	case  5:  return EHairStrandsDebugMode::RenderHairUV;
	case  6:  return EHairStrandsDebugMode::RenderHairSeed;
	case  7:  return EHairStrandsDebugMode::RenderHairDimension;
	case  8:  return EHairStrandsDebugMode::RenderHairRadiusVariation;
	case  9:  return EHairStrandsDebugMode::RenderHairBaseColor;
	case 10:  return EHairStrandsDebugMode::RenderHairRoughness;
	default: return EHairStrandsDebugMode::None;
	};
}

static const TCHAR* ToString(EHairStrandsDebugMode DebugMode)
{
	switch (DebugMode)
	{
	case EHairStrandsDebugMode::None						: return TEXT("None");
	case EHairStrandsDebugMode::SimHairStrands				: return TEXT("Simulation strands");
	case EHairStrandsDebugMode::RenderHairStrands			: return TEXT("Rendering strands influences");
	case EHairStrandsDebugMode::RenderHairRootUV			: return TEXT("Roots UV");
	case EHairStrandsDebugMode::RenderHairRootUDIM			: return TEXT("Roots UV UDIM texture index");
	case EHairStrandsDebugMode::RenderHairUV				: return TEXT("Hair UV");
	case EHairStrandsDebugMode::RenderHairSeed				: return TEXT("Hair seed");
	case EHairStrandsDebugMode::RenderHairDimension			: return TEXT("Hair dimensions");
	case EHairStrandsDebugMode::RenderHairRadiusVariation	: return TEXT("Hair radius variation");
	case EHairStrandsDebugMode::RenderHairBaseColor			: return TEXT("Hair vertices color");
	case EHairStrandsDebugMode::RenderHairRoughness			: return TEXT("Hair vertices roughness");
	default													: return TEXT("None");
	};
}


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairDebugPrintCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairDebugPrintCS);
	SHADER_USE_PARAMETER_STRUCT(FHairDebugPrintCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, GroupSize)
		SHADER_PARAMETER(FIntPoint, PixelCoord)
		SHADER_PARAMETER(FIntPoint, MaxResolution)
		SHADER_PARAMETER(uint32, FastResolveMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeData)
		SHADER_PARAMETER_SRV(Texture2D, DepthStencilTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairDebugPrintCS, "/Engine/Private/HairStrands/HairStrandsDebugPrint.usf", "MainCS", SF_Compute);

static void AddDebugHairPrintPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const EHairDebugMode InDebugMode,
	const FHairStrandsVisibilityData& VisibilityData,
	const FShaderResourceViewRHIRef& InDepthStencilTexture)
{
	const TRefCountPtr<IPooledRenderTarget>& InCategorizationTexture = VisibilityData.CategorizationTexture;
	const TRefCountPtr<IPooledRenderTarget>& InNodeIndex = VisibilityData.NodeIndex;
	const TRefCountPtr<FPooledRDGBuffer>& InNodeData = VisibilityData.NodeData;
	
	if (!InCategorizationTexture || !InNodeIndex || !InNodeData || !InDepthStencilTexture) return;

	FRDGTextureRef CategorizationTexture = InCategorizationTexture ? GraphBuilder.RegisterExternalTexture(InCategorizationTexture, TEXT("CategorizationTexture")) : nullptr;
	FRDGTextureRef NodeIndex = InNodeIndex ? GraphBuilder.RegisterExternalTexture(InNodeIndex, TEXT("NodeIndex")) : nullptr;
	FRDGBufferRef  NodeData  = InNodeData ? GraphBuilder.RegisterExternalBuffer(InNodeData, TEXT("NodeData")) : nullptr;

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairDebugPrintCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPrintCS::FParameters>();
	Parameters->GroupSize = GetVendorOptimalGroupSize2D();
	Parameters->MaxResolution = CategorizationTexture ? CategorizationTexture->Desc.Extent : FIntPoint(0,0);
	Parameters->PixelCoord = View->CursorPos;
	Parameters->FastResolveMask = STENCIL_TEMPORAL_RESPONSIVE_AA_MASK;
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(NodeData);
	Parameters->HairVisibilityNodeOffsetAndCount = NodeIndex;
	Parameters->DepthStencilTexture = InDepthStencilTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	ShaderPrint::SetParameters(*View, Parameters->ShaderPrintUniformBuffer);
	TShaderMapRef<FHairDebugPrintCS> ComputeShader(View->ShaderMap);

	ClearUnusedGraphResources(*ComputeShader, Parameters);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsDebugPrint"),
		*ComputeShader,
		Parameters,
		FIntVector(1, 1, 1));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairDebugPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairDebugPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, OutputResolution)
		SHADER_PARAMETER(uint32, FastResolveMask)
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER(int32, SampleIndex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, NodeData)
		SHADER_PARAMETER_SRV(Texture2D, DepthStencilTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairDebugPS, "/Engine/Private/HairStrands/HairStrandsDebug.usf", "MainPS", SF_Pixel);

static void AddDebugHairPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const EHairDebugMode InDebugMode,
	const TRefCountPtr<IPooledRenderTarget>& InCategorizationTexture,
	const TRefCountPtr<IPooledRenderTarget>& InNodeIndex,
	const TRefCountPtr<FPooledRDGBuffer>& InNodeData,
	const FShaderResourceViewRHIRef& InDepthStencilTexture,
	FRDGTextureRef& OutTarget)
{
	check(OutTarget);
	check(InDebugMode == EHairDebugMode::TAAResolveType || 
		InDebugMode == EHairDebugMode::SamplePerPixel || 
		InDebugMode == EHairDebugMode::CoverageType || 
		InDebugMode == EHairDebugMode::Coverage ||
		InDebugMode == EHairDebugMode::MaterialDepth ||
		InDebugMode == EHairDebugMode::MaterialBaseColor ||
		InDebugMode == EHairDebugMode::MaterialRoughness ||
		InDebugMode == EHairDebugMode::MaterialSpecular ||
		InDebugMode == EHairDebugMode::MaterialTangent);

	if (!InCategorizationTexture || !InNodeIndex || !InNodeData) return;
	if (InDebugMode == EHairDebugMode::TAAResolveType && !InDepthStencilTexture) return;

	FRDGTextureRef CategorizationTexture = InCategorizationTexture ? GraphBuilder.RegisterExternalTexture(InCategorizationTexture, TEXT("CategorizationTexture")) : nullptr;
	FRDGTextureRef NodeIndex = InNodeIndex ? GraphBuilder.RegisterExternalTexture(InNodeIndex, TEXT("NodeIndex")) : nullptr;
	FRDGBufferRef  NodeData = InNodeData ? GraphBuilder.RegisterExternalBuffer(InNodeData, TEXT("NodeData")) : nullptr;

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	uint32 InternalDebugMode = 0;
	switch (InDebugMode)
	{
		case EHairDebugMode::SamplePerPixel:	InternalDebugMode = 0; break;
		case EHairDebugMode::CoverageType:		InternalDebugMode = 1; break;
		case EHairDebugMode::TAAResolveType:	InternalDebugMode = 2; break;
		case EHairDebugMode::Coverage:			InternalDebugMode = 3; break;
		case EHairDebugMode::MaterialDepth:		InternalDebugMode = 4; break;
		case EHairDebugMode::MaterialBaseColor:	InternalDebugMode = 5; break;
		case EHairDebugMode::MaterialRoughness:	InternalDebugMode = 6; break;
		case EHairDebugMode::MaterialSpecular:	InternalDebugMode = 7; break;
		case EHairDebugMode::MaterialTangent:	InternalDebugMode = 8; break;
	};

	FHairDebugPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPS::FParameters>();
	Parameters->ViewUniformBuffer = View->ViewUniformBuffer;
	Parameters->OutputResolution = Resolution;
	Parameters->FastResolveMask = STENCIL_TEMPORAL_RESPONSIVE_AA_MASK;
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->NodeIndex = NodeIndex;
	Parameters->NodeData = GraphBuilder.CreateSRV(NodeData);
	Parameters->DepthStencilTexture = InDepthStencilTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->DebugMode = InternalDebugMode;
	Parameters->SampleIndex = GHairStrandsDebugSampleIndex;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTarget, ERenderTargetLoadAction::ELoad, 0);
	TShaderMapRef<FPostProcessVS> VertexShader(View->ShaderMap);

	TShaderMapRef<FHairDebugPS> PixelShader(View->ShaderMap);

	ClearUnusedGraphResources(*PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsDebug"),
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
class FDeepShadowVisualizePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeepShadowVisualizePS);
	SHADER_USE_PARAMETER_STRUCT(FDeepShadowVisualizePS, FGlobalShader);

	class FOutputType : SHADER_PERMUTATION_INT("PERMUTATION_OUTPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FOutputType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, DomScale)
		SHADER_PARAMETER(FVector2D, DomAtlasOffset)
		SHADER_PARAMETER(FVector2D, DomAtlasScale)
		SHADER_PARAMETER(FVector2D, OutputResolution)
		SHADER_PARAMETER(FVector2D, InvOutputResolution)
		SHADER_PARAMETER(FIntVector4, HairViewRect)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadowDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadowLayerTexture)

		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FDeepShadowVisualizePS, "/Engine/Private/HairStrands/HairStrandsDeepShadowDebug.usf", "VisualizeDomPS", SF_Pixel);

static void AddDebugDeepShadowTexturePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const FIntRect& HairViewRect,
	const FHairStrandsDeepShadowData* ShadowData,
	FRDGTextureRef& OutTarget)
{
	check(OutTarget);

	FRDGTextureRef DeepShadowDepthTexture = nullptr;
	FRDGTextureRef DeepShadowLayerTexture = nullptr;
	FIntPoint AtlasResolution(0, 0);
	FVector2D AltasOffset(0, 0);
	FVector2D AltasScale(0, 0);
	if (ShadowData)
	{
		DeepShadowDepthTexture = GraphBuilder.RegisterExternalTexture(ShadowData->DepthTexture, TEXT("DOMDepthTexture"));
		DeepShadowLayerTexture = GraphBuilder.RegisterExternalTexture(ShadowData->LayersTexture, TEXT("DOMLayerTexture"));

		AtlasResolution = FIntPoint(DeepShadowDepthTexture->Desc.Extent.X, DeepShadowDepthTexture->Desc.Extent.Y);
		AltasOffset = FVector2D(ShadowData->AtlasRect.Min.X / float(AtlasResolution.X), ShadowData->AtlasRect.Min.Y / float(AtlasResolution.Y));
		AltasScale = FVector2D((ShadowData->AtlasRect.Max.X - ShadowData->AtlasRect.Min.X) / float(AtlasResolution.X), (ShadowData->AtlasRect.Max.Y - ShadowData->AtlasRect.Min.Y) / float(AtlasResolution.Y));
	}

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FDeepShadowVisualizePS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeepShadowVisualizePS::FParameters>();
	Parameters->DomScale = GDeepShadowDebugScale;
	Parameters->DomAtlasOffset = AltasOffset;
	Parameters->DomAtlasScale = AltasScale;
	Parameters->OutputResolution = Resolution;
	Parameters->InvOutputResolution = FVector2D(1.f / Resolution.X, 1.f / Resolution.Y);
	Parameters->DeepShadowDepthTexture = DeepShadowDepthTexture;
	Parameters->DeepShadowLayerTexture = DeepShadowLayerTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->HairViewRect = FIntVector4(HairViewRect.Min.X, HairViewRect.Min.Y, HairViewRect.Width(), HairViewRect.Height());
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTarget, ERenderTargetLoadAction::ELoad, 0);
	TShaderMapRef<FPostProcessVS> VertexShader(View->ShaderMap);
	FDeepShadowVisualizePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeepShadowVisualizePS::FOutputType>(ShadowData ? 0 : 1);
	TShaderMapRef<FDeepShadowVisualizePS> PixelShader(View->ShaderMap, PermutationVector);

	ClearUnusedGraphResources(*PixelShader, Parameters);

	GraphBuilder.AddPass(
		ShadowData ? RDG_EVENT_NAME("DebugDeepShadowTexture") : RDG_EVENT_NAME("DebugHairViewRect"),
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
class FVoxelRaymarchingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelRaymarchingPS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelRaymarchingPS, FGlobalShader);

	class FDebugMode : SHADER_PERMUTATION_INT("PERMUTATION_DEBUG_MODE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FDebugMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER(FVector, VoxelMinAABB)
		SHADER_PARAMETER(uint32, VoxelResolution)
		SHADER_PARAMETER(FVector, VoxelMaxAABB)
		SHADER_PARAMETER(float, DensityIsoline)
		SHADER_PARAMETER(float, VoxelDensityScale)
		SHADER_PARAMETER(FVector2D, OutputResolution)

		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, DensityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TangentXTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TangentYTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TangentZTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, MaterialTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FVoxelRaymarchingPS, "/Engine/Private/HairStrands/HairStrandsVoxelRayMarching.usf", "MainPS", SF_Pixel);

static void AddVoxelRaymarchingPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const EHairDebugMode DebugMode,
	const FHairStrandsClusterDatas& ClusterDatas,
	FRDGTextureRef& OutputTexture)
{
	check(DebugMode == EHairDebugMode::VoxelsDensity || DebugMode == EHairDebugMode::VoxelsTangent || DebugMode == EHairDebugMode::VoxelsBaseColor || DebugMode == EHairDebugMode::VoxelsRoughness);

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const FIntPoint Resolution(OutputTexture->Desc.Extent);
	for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
	{
		if (DebugMode == EHairDebugMode::VoxelsDensity && !ClusterData.VoxelResources.DensityTexture)
			return;

		if (DebugMode == EHairDebugMode::VoxelsTangent && (!ClusterData.VoxelResources.TangentXTexture || !ClusterData.VoxelResources.TangentYTexture || !ClusterData.VoxelResources.TangentZTexture))
			return;

		if ((DebugMode == EHairDebugMode::VoxelsBaseColor || DebugMode == EHairDebugMode::VoxelsRoughness) && !ClusterData.VoxelResources.MaterialTexture)
			return;

		const FRDGTextureRef VoxelDensityTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.DensityTexture ? ClusterData.VoxelResources.DensityTexture : GSystemTextures.BlackDummy, TEXT("HairVoxelDensityTexture"));
		const FRDGTextureRef VoxelTangentXTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.TangentXTexture ? ClusterData.VoxelResources.TangentXTexture : GSystemTextures.BlackDummy, TEXT("HairVoxelTangentXTexture"));
		const FRDGTextureRef VoxelTangentYTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.TangentYTexture ? ClusterData.VoxelResources.TangentYTexture : GSystemTextures.BlackDummy, TEXT("HairVoxelTangentYTexture"));
		const FRDGTextureRef VoxelTangentZTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.TangentZTexture ? ClusterData.VoxelResources.TangentZTexture : GSystemTextures.BlackDummy, TEXT("HairVoxelTangentZTexture"));
		const FRDGTextureRef VoxelMaterialTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.MaterialTexture ? ClusterData.VoxelResources.MaterialTexture : GSystemTextures.BlackDummy, TEXT("HairVoxelMaterialTexture"));

		FVoxelRaymarchingPS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelRaymarchingPS::FParameters>();
		Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
		Parameters->OutputResolution = Resolution;
		Parameters->SceneTextures = SceneTextures;
		Parameters->DensityTexture = VoxelDensityTexture;
		Parameters->TangentXTexture = VoxelTangentXTexture;
		Parameters->TangentYTexture = VoxelTangentYTexture;
		Parameters->TangentZTexture = VoxelTangentZTexture;
		Parameters->MaterialTexture = VoxelMaterialTexture;
		Parameters->VoxelMinAABB = ClusterData.GetMinBound();
		Parameters->VoxelMaxAABB = ClusterData.GetMaxBound();
		Parameters->VoxelResolution = ClusterData.GetResolution();
		Parameters->VoxelDensityScale = GetHairStrandsVoxelizationDensityScale();
		Parameters->DensityIsoline = 1;
		Parameters->LinearSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

		const FIntPoint OutputResolution = SceneTextures.SceneDepthBuffer->Desc.Extent;
		TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);

		FVoxelRaymarchingPS::FPermutationDomain PermutationVector;
		uint32 DebugPermutation = 0;
		switch (DebugMode)
		{
		case EHairDebugMode::VoxelsDensity:		DebugPermutation = 0; break;
		case EHairDebugMode::VoxelsTangent:		DebugPermutation = 1; break;
		case EHairDebugMode::VoxelsBaseColor:	DebugPermutation = 2; break;
		case EHairDebugMode::VoxelsRoughness:	DebugPermutation = 3; break;
		};
		PermutationVector.Set<FVoxelRaymarchingPS::FDebugMode>(DebugPermutation);

		TShaderMapRef<FVoxelRaymarchingPS> PixelShader(View.ShaderMap, PermutationVector);
		const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
		const FIntRect Viewport = View.ViewRect;
		const FViewInfo* CapturedView = &View;

		ClearUnusedGraphResources(*PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVoxelRaymarching"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
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
class FHairStrandsBSDFPlotPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsBSDFPlotPS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsBSDFPlotPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, InputCoord)
		SHADER_PARAMETER(FIntPoint, OutputOffset)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, MaxResolution)
		SHADER_PARAMETER(uint32, HairComponents)
		SHADER_PARAMETER(float, Roughness)
		SHADER_PARAMETER(float, BaseColor)
		SHADER_PARAMETER(float, Exposure)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsBSDFPlotPS, "/Engine/Private/HairStrands/HairStrandsBsdfPlot.usf", "MainPS", SF_Pixel);

static void AddPlotBSDFPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef& OutputTexture)
{
	
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const FIntPoint Resolution(OutputTexture->Desc.Extent);
	FHairStrandsBSDFPlotPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsBSDFPlotPS::FParameters>();
	Parameters->InputCoord = View.CursorPos;
	Parameters->OutputOffset = FIntPoint(100,100);
	Parameters->OutputResolution = FIntPoint(256, 256);
	Parameters->MaxResolution = OutputTexture->Desc.Extent;
	Parameters->HairComponents = ToBitfield(GetHairComponents());
	Parameters->Roughness = GHairStrandsDebugPlotBsdfRoughness;
	Parameters->BaseColor = GHairStrandsDebugPlotBsdfBaseColor;
	Parameters->Exposure = GHairStrandsDebugPlotBsdfExposure;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

	const FIntPoint OutputResolution = SceneTextures.SceneDepthBuffer->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsBSDFPlotPS> PixelShader(View.ShaderMap);
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(*PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsBsdfPlot"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
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
BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionMeshDebugParameters, )
	SHADER_PARAMETER(FMatrix, LocalToWorld)
	SHADER_PARAMETER(uint32, VertexOffset)
	SHADER_PARAMETER(uint32, IndexOffset)
	SHADER_PARAMETER(uint32, MaxIndexCount)
	SHADER_PARAMETER(uint32, MaxVertexCount)
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputIndexBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionMeshDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}


	FHairProjectionMeshDebug() = default;
	FHairProjectionMeshDebug(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};


class FHairProjectionMeshDebugVS : public FHairProjectionMeshDebug
{
public:
	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugVS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )		
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()
};

class FHairProjectionMeshDebugPS : public FHairProjectionMeshDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionMeshDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionMeshDebugPS, FHairProjectionMeshDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionMeshDebugParameters, Pass)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionMeshDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionMeshDebug.usf", "MainPS", SF_Pixel);

static void AddDebugProjectionMeshPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const bool bClearDepth,
	FHairStrandsProjectionMeshData::Section& MeshSectionData,
	FRDGTextureRef& ColorTexture, 
	FRDGTextureRef& DepthTexture)
{	
	const EPrimitiveType PrimitiveType = PT_TriangleList;
	const bool bHasIndexBuffer = MeshSectionData.IndexBuffer != nullptr;
	const uint32 PrimitiveCount = MeshSectionData.NumPrimitives;

	if (!MeshSectionData.PositionBuffer || PrimitiveCount==0)
		return;

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionMeshDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionMeshDebugParameters>();
	Parameters->LocalToWorld = MeshSectionData.LocalToWorld.ToMatrixWithScale();
	Parameters->OutputResolution = Resolution;
	Parameters->VertexOffset = MeshSectionData.VertexBaseIndex;
	Parameters->IndexOffset = MeshSectionData.IndexBaseIndex;
	Parameters->MaxIndexCount = MeshSectionData.TotalIndexCount;
	Parameters->MaxVertexCount = MeshSectionData.TotalVertexCount;
	Parameters->InputIndexBuffer  = MeshSectionData.IndexBuffer;
	Parameters->InputVertexBuffer = MeshSectionData.PositionBuffer;
	Parameters->ViewUniformBuffer = View->ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(ColorTexture, ERenderTargetLoadAction::ELoad, 0);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, bClearDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairProjectionMeshDebugVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairProjectionMeshDebugVS::FInputType>(bHasIndexBuffer ? 1 : 0);

	TShaderMapRef<FHairProjectionMeshDebugVS> VertexShader(View->ShaderMap, PermutationVector);
	TShaderMapRef<FHairProjectionMeshDebugPS> PixelShader(View->ShaderMap);

	FHairProjectionMeshDebugVS::FParameters VSParameters;
	VSParameters.Pass = *Parameters;
	FHairProjectionMeshDebugPS::FParameters PSParameters;
	PSParameters.Pass = *Parameters;
		
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMeshProjectionMeshDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VSParameters, PSParameters, VertexShader, PixelShader, Viewport, Resolution, View, PrimitiveCount, PrimitiveType](FRHICommandList& RHICmdList)
	{

		RHICmdList.SetViewport(
			Viewport.Min.X,
			Viewport.Min.Y,
			0.0f,
			Viewport.Max.X,
			Viewport.Max.Y,
			1.0f);

		// Apply additive blending pipeline state.
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PrimitiveType;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, *VertexShader, VertexShader->GetVertexShader(), VSParameters);
		SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), PSParameters);

		// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
		RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionHairDebugParameters, )
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER(uint32, MaxRootCount)
	SHADER_PARAMETER(uint32, DeformedFrameEnable)
	SHADER_PARAMETER(FVector, RestPositionOffset)
	SHADER_PARAMETER(FVector, DeformedPositionOffset)
	SHADER_PARAMETER(FMatrix, RootLocalToWorld)

	SHADER_PARAMETER_SRV(StructuredBuffer, RestPosition0Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RestPosition1Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RestPosition2Buffer)

	SHADER_PARAMETER_SRV(StructuredBuffer, DeformedPosition0Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, DeformedPosition1Buffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, DeformedPosition2Buffer)

	// Change for actual frame data (stored or computed only)
	SHADER_PARAMETER_SRV(StructuredBuffer, RootPositionBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RootNormalBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, RootBarycentricBuffer)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FHairProjectionHairDebug : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}

	FHairProjectionHairDebug() = default;
	FHairProjectionHairDebug(const CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}
};


class FHairProjectionHairDebugVS : public FHairProjectionHairDebug
{
public:
	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FInputType>;

	DECLARE_GLOBAL_SHADER(FHairProjectionHairDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionHairDebugVS, FHairProjectionHairDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionHairDebugParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
};

class FHairProjectionHairDebugPS : public FHairProjectionHairDebug
{
	DECLARE_GLOBAL_SHADER(FHairProjectionHairDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairProjectionHairDebugPS, FHairProjectionHairDebug);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairProjectionHairDebugParameters, Pass)
		END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FHairProjectionHairDebugVS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionHairDebug.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FHairProjectionHairDebugPS, "/Engine/Private/HairStrands/HairStrandsMeshProjectionHairDebug.usf", "MainPS", SF_Pixel);

enum class EDebugProjectionHairType
{
	HairFrame,
	HairTriangle,
};

static void AddDebugProjectionHairPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const bool bClearDepth,
	const EDebugProjectionHairType GeometryType,
	const HairStrandsTriangleType PoseType,
	const int32 LODIndex,
	const FHairStrandsProjectionHairData::HairGroup& HairData,
	FRDGTextureRef ColorTarget,
	FRDGTextureRef DepthTexture)
{
	const EPrimitiveType PrimitiveType = GeometryType == EDebugProjectionHairType::HairFrame ? PT_LineList : PT_TriangleList;
	const uint32 PrimitiveCount = HairData.RootCount;

	if (PrimitiveCount == 0 || LODIndex < 0 || LODIndex >= HairData.LODDatas.Num())
		return;

	if (EDebugProjectionHairType::HairFrame == GeometryType && (!HairData.RootPositionBuffer || !HairData.RootNormalBuffer || !HairData.LODDatas[LODIndex].RootTriangleBarycentricBuffer))
		return;

	if (!HairData.LODDatas[LODIndex].RestRootTrianglePosition0Buffer ||
		!HairData.LODDatas[LODIndex].RestRootTrianglePosition1Buffer ||
		!HairData.LODDatas[LODIndex].RestRootTrianglePosition2Buffer ||
		!HairData.LODDatas[LODIndex].DeformedRootTrianglePosition0Buffer ||
		!HairData.LODDatas[LODIndex].DeformedRootTrianglePosition1Buffer ||
		!HairData.LODDatas[LODIndex].DeformedRootTrianglePosition2Buffer)
		return;

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairProjectionHairDebugParameters* Parameters = GraphBuilder.AllocParameters<FHairProjectionHairDebugParameters>();
	Parameters->OutputResolution = Resolution;
	Parameters->MaxRootCount = HairData.RootCount;
	Parameters->RootLocalToWorld = HairData.LocalToWorld.ToMatrixWithScale();
	Parameters->DeformedFrameEnable = PoseType == HairStrandsTriangleType::DeformedPose;

	if (EDebugProjectionHairType::HairFrame == GeometryType)
	{
		Parameters->RootPositionBuffer = HairData.RootPositionBuffer;
		Parameters->RootNormalBuffer = HairData.RootNormalBuffer;
		Parameters->RootBarycentricBuffer = HairData.LODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;
	}

	Parameters->RestPositionOffset = HairData.LODDatas[LODIndex].RestPositionOffset;
	Parameters->RestPosition0Buffer = HairData.LODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
	Parameters->RestPosition1Buffer = HairData.LODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
	Parameters->RestPosition2Buffer = HairData.LODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;
	
	Parameters->DeformedPositionOffset = HairData.LODDatas[LODIndex].DeformedPositionOffset;
	Parameters->DeformedPosition0Buffer = HairData.LODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
	Parameters->DeformedPosition1Buffer = HairData.LODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
	Parameters->DeformedPosition2Buffer = HairData.LODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;

	Parameters->ViewUniformBuffer = View->ViewUniformBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(ColorTarget, ERenderTargetLoadAction::ELoad, 0);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTexture, bClearDepth ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairProjectionHairDebugVS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairProjectionHairDebugVS::FInputType>(PrimitiveType == PT_LineList ? 0 : 1);

	TShaderMapRef<FHairProjectionHairDebugVS> VertexShader(View->ShaderMap, PermutationVector);
	TShaderMapRef<FHairProjectionHairDebugPS> PixelShader(View->ShaderMap);

	FHairProjectionHairDebugVS::FParameters VSParameters;
	VSParameters.Pass = *Parameters;
	FHairProjectionHairDebugPS::FParameters PSParameters;
	PSParameters.Pass = *Parameters;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsMeshProjectionHairDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VSParameters, PSParameters, VertexShader, PixelShader, Viewport, Resolution, View, PrimitiveCount, PrimitiveType](FRHICommandList& RHICmdList)
	{

		RHICmdList.SetViewport(
			Viewport.Min.X,
			Viewport.Min.Y,
			0.0f,
			Viewport.Max.X,
			Viewport.Max.Y,
			1.0f);

		// Apply additive blending pipeline state.
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PrimitiveType;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, *VertexShader, VertexShader->GetVertexShader(), VSParameters);
		SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), PSParameters);

		// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
		RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* ToString(EWorldType::Type Type)
{
	switch (Type)
	{
		case EWorldType::None			: return TEXT("None");
		case EWorldType::Game			: return TEXT("Game");
		case EWorldType::Editor			: return TEXT("Editor");
		case EWorldType::PIE			: return TEXT("PIE");
		case EWorldType::EditorPreview	: return TEXT("EditorPreview");
		case EWorldType::GamePreview	: return TEXT("GamePreview");
		case EWorldType::GameRPC		: return TEXT("GameRPC");
		case EWorldType::Inactive		: return TEXT("Inactive");
		default							: return TEXT("Unknown");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairVisibilityDebugPPLLCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityDebugPPLLCS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityDebugPPLLCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(float, PPLLMeanListElementCountPerPixel)
		SHADER_PARAMETER(float, PPLLMaxTotalListElementCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PPLLCounter)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PPLLNodeIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, PPLLNodeData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, SceneColorTextureUAV)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
	END_SHADER_PARAMETER_STRUCT()

		static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEBUG_PPLL_PS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FHairVisibilityDebugPPLLCS, "/Engine/Private/HairStrands/HairStrandsVisibilityPPLLDebug.usf", "VisibilityDebugPPLLCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

void RenderHairStrandsDebugInfo(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views, const FHairStrandsDatas* HairDatas)
{
	const float YStep = 14;
	const float ColumnWidth = 200;

	if (Views.Num() == 0)
		return;

	if (GHairStrandsUpdateCullIndex)
	{
		const FViewInfo& View = Views[0];
		const float TotalPixelCount = View.ViewRect.Width() * View.ViewRect.Height();
		const float Index = View.CursorPos.X + View.CursorPos.Y * View.ViewRect.Width();
		GHairStrandsCullNormalizedIndex = Index / TotalPixelCount;
	}

	// Only render debug information for the main view
	const uint32 ViewIndex = 0;
	FViewInfo& View = Views[ViewIndex];
	const FSceneViewFamily& ViewFamily = *(View.Family);
	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);

	// Debug mode name only
	const EHairStrandsDebugMode StrandsDebugMode = GetHairStrandsDebugStrandsMode();
	const EHairDebugMode HairDebugMode = GetHairDebugMode();

	if (GHairStrandsDebugPlotBsdf > 0)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		AddPlotBSDFPass(GraphBuilder, View, SceneColorTexture);
		GraphBuilder.Execute();		
	}

	float ClusterY = 38;
	if (HairDebugMode == EHairDebugMode::ClusterData)
	{
		// Component part of the clusters
		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneTargets.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.FeatureLevel);

		float X = 20;
		float Y = ClusterY;
		FLinearColor InactiveColor(0.5, 0.5, 0.5);
		FLinearColor DebugColor(1, 1, 0);
		FString Line;

		const FHairStrandsDebugInfos DebugInfos = GetHairStandsDebugInfos();

		Line = FString::Printf(TEXT("----------------------------------------------------------------"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		Line = FString::Printf(TEXT("Registered component count : %d"), DebugInfos.Num());
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		for (const FHairStrandsDebugInfo& DebugInfo : DebugInfos)
		{
			check(ViewFamily.Scene && ViewFamily.Scene->GetWorld());
			const bool bIsActive = DebugInfo.WorldType == ViewFamily.Scene->GetWorld()->WorldType;

			Line = FString::Printf(TEXT(" * Id:%d | WorldType:%s | Group count : %d"), DebugInfo.Id, ToString(DebugInfo.WorldType), DebugInfo.HairGroups.Num());
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugColor : InactiveColor);

			for (const FHairStrandsDebugInfo::HairGroup& DebugHairGroup : DebugInfo.HairGroups)
			{
				Line = FString::Printf(TEXT("        |> CurveCount : %d | VertexCount : %d | MaxRadius : %f | MaxLength : %f | Skinned: %s | LOD count : %d"),
					DebugHairGroup.CurveCount,
					DebugHairGroup.VertexCount,
					DebugHairGroup.MaxRadius,
					DebugHairGroup.MaxLength,
					DebugHairGroup.bHasSkinInterpolation ? TEXT("True") : TEXT("False"),
					DebugHairGroup.LODCount);
				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugColor : InactiveColor);
			}
		}

		Canvas.Flush_RenderThread(RHICmdList);

		ClusterY = Y;
	}

	if (!HairDatas)
		return;

	const FHairStrandsDeepShadowViews& InDomViews = HairDatas->DeepShadowViews;
	const FHairStrandsClusterViews& InClusterViews = HairDatas->HairClusterPerViews;

	if (HairDebugMode == EHairDebugMode::ClusterData)
	{
		if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
			AddDebugHairPrintPass(GraphBuilder, &View, HairDebugMode, VisibilityData, SceneTargets.SceneStencilSRV);
			GraphBuilder.Execute();	
		}

		// Clusters part of the clusters
		FViewElementPDI ShadowFrustumPDI(&View, nullptr, nullptr);
		const FHairStrandsClusterDatas& ClusterDatas = InClusterViews.Views[ViewIndex];
		for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
		{
			const FBox ClusterBox(ClusterData.GetMinBound(), ClusterData.GetMaxBound());
			DrawWireBox(&ShadowFrustumPDI, ClusterBox, FColor::Red, 0);
		}

		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneTargets.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.FeatureLevel);

		float X = 20;
		float Y = ClusterY;
		FLinearColor InactiveColor(0.5, 0.5, 0.5);
		FLinearColor DebugColor(1, 1, 0);
		FString Line;

		Line = FString::Printf(TEXT("----------------------------------------------------------------"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		Line = FString::Printf(TEXT("Cluster count : %d"), ClusterDatas.Datas.Num());
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
		{
			Line = FString::Printf(TEXT(" %d - Bound Radus: %f.2m (%dx%d)"), ClusterData.ClusterId, ClusterData.Bounds.GetSphere().W);
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		}

		Canvas.Flush_RenderThread(RHICmdList);
	}

	if (HairDebugMode == EHairDebugMode::DeepOpacityMaps)
	{
		const uint32 DomIndex = GDeepShadowDebugIndex;
		TRefCountPtr<IPooledRenderTarget> DepthTexture;
		TRefCountPtr<IPooledRenderTarget> LayerTexture;
		const FHairStrandsDeepShadowDatas& DeepShadoDatas = InDomViews.Views[ViewIndex];
		const bool bIsValid = DomIndex < uint32(DeepShadoDatas.Datas.Num());
		if (bIsValid)
		{				
			DepthTexture = DeepShadoDatas.Datas[DomIndex].DepthTexture;
			LayerTexture = DeepShadoDatas.Datas[DomIndex].LayersTexture;
		}

		if (DepthTexture && LayerTexture)
		{
			const FHairStrandsDeepShadowData& DeepShadoData = DeepShadoDatas.Datas[DomIndex];
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
			AddDebugDeepShadowTexturePass(GraphBuilder, &View, FIntRect(), &DeepShadoData, SceneColorTexture);
			GraphBuilder.Execute();
		}
	}

	// View Rect
	if (IsHairStrandsViewRectOptimEnable() && HairDebugMode == EHairDebugMode::ClusterScreenRect)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (ViewIndex < uint32(InClusterViews.Views.Num()))
		{
			const FHairStrandsClusterDatas& ClusterDatas = InClusterViews.Views[ViewIndex];
			for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
			{
				AddDebugDeepShadowTexturePass(GraphBuilder, &View, ClusterData.ScreenRect, nullptr, SceneColorTexture);
			}

			const FIntRect TotalRect = ComputeVisibleHairStrandsClustersRect(View.ViewRect, ClusterDatas);
			AddDebugDeepShadowTexturePass(GraphBuilder, &View, TotalRect, nullptr, SceneColorTexture);
		}
		GraphBuilder.Execute();
	}
	
	const bool bIsVoxelMode = HairDebugMode == EHairDebugMode::VoxelsDensity || HairDebugMode == EHairDebugMode::VoxelsTangent || HairDebugMode == EHairDebugMode::VoxelsBaseColor || HairDebugMode == EHairDebugMode::VoxelsRoughness;

	// Render Frustum for all lights & clusters
	{
		FViewElementPDI ShadowFrustumPDI(&View, nullptr, nullptr);

		// All DOMs
		if (HairDebugMode == EHairDebugMode::LightBounds && ViewIndex < uint32(InDomViews.Views.Num()))
		{
			const FHairStrandsDeepShadowDatas& DOMs = InDomViews.Views[ViewIndex];
			for (const FHairStrandsDeepShadowData& DomData : DOMs.Datas)
			{
				DrawFrustumWireframe(&ShadowFrustumPDI, DomData.WorldToLightTransform.Inverse(), FColor::Emerald, 0);
				DrawWireBox(&ShadowFrustumPDI, DomData.Bounds.GetBox(), FColor::Yellow, 0);
			}
		}

		// Current DOM
		if (HairDebugMode == EHairDebugMode::DeepOpacityMaps && ViewIndex < uint32(InDomViews.Views.Num()))
		{
			const int32 CurrentIndex = FMath::Max(0, GDeepShadowDebugIndex);
			const FHairStrandsDeepShadowDatas& DOMs = InDomViews.Views[ViewIndex];
			if (CurrentIndex < DOMs.Datas.Num())
			{
				DrawFrustumWireframe(&ShadowFrustumPDI, DOMs.Datas[CurrentIndex].WorldToLightTransform.Inverse(), FColor::Emerald, 0);
				DrawWireBox(&ShadowFrustumPDI, DOMs.Datas[CurrentIndex].Bounds.GetBox(), FColor::Yellow, 0);
			}
		}

		// Voxelization
		if (bIsVoxelMode && ViewIndex < uint32(InClusterViews.Views.Num()))
		{
			const FHairStrandsClusterDatas& ClusterDatas = InClusterViews.Views[ViewIndex];
			for (const FHairStrandsClusterData& ClusterData : ClusterDatas.Datas)
			{
				DrawFrustumWireframe(&ShadowFrustumPDI, ClusterData.VoxelResources.WorldToClip.Inverse(), FColor::Purple, 0);

				const FBox VoxelizationBox(ClusterData.GetMinBound(), ClusterData.GetMaxBound());
				DrawWireBox(&ShadowFrustumPDI, VoxelizationBox, FColor::Red, 0);
			}
		}
	}

	const bool bRunDebugPass =
		HairDebugMode == EHairDebugMode::TAAResolveType ||
		HairDebugMode == EHairDebugMode::SamplePerPixel ||
		HairDebugMode == EHairDebugMode::CoverageType ||
		HairDebugMode == EHairDebugMode::Coverage ||
		HairDebugMode == EHairDebugMode::MaterialDepth ||
		HairDebugMode == EHairDebugMode::MaterialBaseColor ||
		HairDebugMode == EHairDebugMode::MaterialRoughness ||
		HairDebugMode == EHairDebugMode::MaterialSpecular ||
		HairDebugMode == EHairDebugMode::MaterialTangent;
	if (bRunDebugPass)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
		{
			const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
			AddDebugHairPass(GraphBuilder, &View, HairDebugMode, VisibilityData.CategorizationTexture, VisibilityData.NodeIndex, VisibilityData.NodeData, SceneTargets.SceneStencilSRV, SceneColorTexture);
			AddDebugHairPrintPass(GraphBuilder, &View, HairDebugMode, VisibilityData,  SceneTargets.SceneStencilSRV);
		}

		GraphBuilder.Execute();
	}

	if (bIsVoxelMode)
	{
		if (ViewIndex < uint32(InClusterViews.Views.Num()))
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
			const FHairStrandsClusterDatas& ClusterDatas = InClusterViews.Views[ViewIndex];
			AddVoxelRaymarchingPass(GraphBuilder, View, HairDebugMode, ClusterDatas, SceneColorTexture);
			GraphBuilder.Execute();
		}
	}

	if (HairDebugMode == EHairDebugMode::MeshProjection)
	{
		const EWorldType::Type WorldType = View.Family->Scene->GetWorld()->WorldType;

		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
		{

			bool bClearDepth = true;
			FRDGTextureRef DepthTexture;
			{
				FRDGTextureDesc Desc;
				Desc.Extent = SceneColorTexture->Desc.Extent;
				Desc.Depth = 0;
				Desc.Format = PF_DepthStencil;
				Desc.NumMips = 1;
				Desc.NumSamples = 1;
				Desc.Flags = TexCreate_None;
				Desc.TargetableFlags = TexCreate_DepthStencilTargetable;
				Desc.ClearValue = FClearValueBinding::DepthFar;
				Desc.bForceSharedTargetAndShaderResource = true;
				DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairInterpolationDepthTexture"));
			}

			if (GHairDebugMeshProjection_SkinCacheMesh > 0)
			{
				FHairStrandsProjectionMeshData MeshProjectionData;
				GetGroomInterpolationData(WorldType, MeshProjectionData);
				for (FHairStrandsProjectionMeshData::Section& Section : MeshProjectionData.Sections)
				{
					AddDebugProjectionMeshPass(GraphBuilder, &View, bClearDepth, Section, SceneColorTexture, DepthTexture);
					bClearDepth = false;
				}
			}

			FViewInfo* LocalView = &View;
			auto RenderProjectionData = [&GraphBuilder, LocalView, WorldType, &bClearDepth, SceneColorTexture, DepthTexture](bool bRenderData, bool bRestTriangle, bool bRestFrame, bool bDeformedTriangle, bool bDeformedFrame)
			{
				FHairStrandsProjectionHairData HairProjectionDatas;
				TArray<int32> HairLODIndices;
				GetGroomInterpolationData(WorldType, bRenderData, HairProjectionDatas, HairLODIndices);
				check(HairProjectionDatas.HairGroups.Num() == HairLODIndices.Num());
				for (int32 HairIndex=0; HairIndex < HairProjectionDatas.HairGroups.Num(); ++HairIndex)
				{
					const FHairStrandsProjectionHairData::HairGroup& Data = HairProjectionDatas.HairGroups[HairIndex];
					const int32 LODIndex = HairLODIndices[HairIndex];

					if (bRestTriangle)
					{
						AddDebugProjectionHairPass(GraphBuilder, LocalView, bClearDepth, EDebugProjectionHairType::HairTriangle, HairStrandsTriangleType::RestPose, LODIndex, Data, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
					if (bRestFrame)
					{
						AddDebugProjectionHairPass(GraphBuilder, LocalView, bClearDepth, EDebugProjectionHairType::HairFrame, HairStrandsTriangleType::RestPose, LODIndex, Data, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
					if (bDeformedTriangle)
					{
						AddDebugProjectionHairPass(GraphBuilder, LocalView, bClearDepth, EDebugProjectionHairType::HairTriangle, HairStrandsTriangleType::DeformedPose, LODIndex, Data, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
					if (bDeformedFrame)
					{
						AddDebugProjectionHairPass(GraphBuilder, LocalView, bClearDepth, EDebugProjectionHairType::HairFrame, HairStrandsTriangleType::DeformedPose, LODIndex, Data, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
				}
			};

			if (GHairDebugMeshProjection_Render_HairRestTriangles > 0 || 
				GHairDebugMeshProjection_Render_HairRestFrames > 0 || 
				GHairDebugMeshProjection_Render_HairDeformedTriangles > 0 || 
				GHairDebugMeshProjection_Render_HairDeformedFrames > 0)
			{
				RenderProjectionData(
					true, 
					GHairDebugMeshProjection_Render_HairRestTriangles > 0, 
					GHairDebugMeshProjection_Render_HairRestFrames > 0, 
					GHairDebugMeshProjection_Render_HairDeformedTriangles > 0, 
					GHairDebugMeshProjection_Render_HairDeformedFrames > 0);
			}

			if (GHairDebugMeshProjection_Sim_HairRestTriangles > 0 || 
				GHairDebugMeshProjection_Sim_HairRestFrames > 0 || 
				GHairDebugMeshProjection_Sim_HairDeformedTriangles > 0 || 
				GHairDebugMeshProjection_Sim_HairDeformedFrames > 0)
			{
				RenderProjectionData(
					false, 
					GHairDebugMeshProjection_Sim_HairRestTriangles > 0,
					GHairDebugMeshProjection_Sim_HairRestFrames > 0, 
					GHairDebugMeshProjection_Sim_HairDeformedTriangles > 0, 
					GHairDebugMeshProjection_Sim_HairDeformedFrames > 0);
			}
		}
		GraphBuilder.Execute();
	}

	if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
	{
		const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
		if (GHairStrandsDebugPPLL && VisibilityData.PPLLNodeCounterTexture) // Check if PPLL rendering is used and its debug view is enabled.
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
			FRDGTextureRef PPLLNodeCounterTexture = GraphBuilder.RegisterExternalTexture(VisibilityData.PPLLNodeCounterTexture, TEXT("PPLLNodeCounterTexture"));
			FRDGTextureRef PPLLNodeIndexTexture = GraphBuilder.RegisterExternalTexture(VisibilityData.PPLLNodeIndexTexture, TEXT("PPLLNodeIndexTexture"));
			FRDGBufferRef  PPLLNodeDataBuffer = GraphBuilder.RegisterExternalBuffer(VisibilityData.PPLLNodeDataBuffer, TEXT("PPLLNodeDataBuffer"));

			FHairVisibilityDebugPPLLCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityDebugPPLLCS::FParameters>();
			PassParameters->PPLLMeanListElementCountPerPixel = float(GetPPLLMeanListElementCountPerPixel());
			PassParameters->PPLLMaxTotalListElementCount = float(GetPPLLMaxTotalListElementCount(VisibilityData.PPLLNodeIndexTexture->GetDesc().Extent));
			PassParameters->PPLLCounter = PPLLNodeCounterTexture;
			PassParameters->PPLLNodeIndex = PPLLNodeIndexTexture;
			PassParameters->PPLLNodeData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PPLLNodeDataBuffer));
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->SceneColorTextureUAV = GraphBuilder.CreateUAV(SceneColorTexture);
			ShaderPrint::SetParameters(View, PassParameters->ShaderPrintParameters);

			FHairVisibilityDebugPPLLCS::FPermutationDomain PermutationVector;
			TShaderMapRef<FHairVisibilityDebugPPLLCS> ComputeShader(View.ShaderMap, PermutationVector);
			FIntVector TextureSize = SceneColorTexture->Desc.GetSize(); TextureSize.Z = 1;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairPPLLDebug"), *ComputeShader, PassParameters,
				FIntVector::DivideAndRoundUp(TextureSize, FIntVector(8, 8, 1)));
			GraphBuilder.Execute();
		}
	}

	// Text
	if (HairDebugMode == EHairDebugMode::LightBounds || HairDebugMode == EHairDebugMode::DeepOpacityMaps)
	{
		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneTargets.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.FeatureLevel);

		const FHairStrandsDeepShadowDatas& ViewData = InDomViews.Views[ViewIndex];
		const uint32 DomTextureIndex = GDeepShadowDebugIndex;

		const FIntPoint AtlasResolution = ViewData.Datas.Num() > 0 && ViewData.Datas[0].DepthTexture ? ViewData.Datas[0].DepthTexture->GetDesc().Extent : FIntPoint(0, 0);
		float X = 20;
		float Y = 38;

		FLinearColor DebugColor(1, 1, 0);
		FString Line;

		const FHairComponent HairComponent = GetHairComponents();
		Line = FString::Printf(TEXT("Hair Components : (R=%d, TT=%d, TRT=%d, GS=%d, LS=%d)"), HairComponent.R, HairComponent.TT, HairComponent.TRT, HairComponent.GlobalScattering, HairComponent.LocalScattering);
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("----------------------------------------------------------------"));				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("Debug strands mode : %s"), ToString(StrandsDebugMode));							Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("Voxelization : %s"), IsHairStrandsVoxelizationEnable() ? TEXT("On") : TEXT("Off"));		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("View rect optim.: %s"), IsHairStrandsViewRectOptimEnable() ? TEXT("On") : TEXT("Off"));	Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("----------------------------------------------------------------"));				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM Atlas resolution : %d/%d"), AtlasResolution.X, AtlasResolution.Y);				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM Cluster count : %d"), ViewData.Datas.Num());									Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM Texture Index : %d/%d"), DomTextureIndex, ViewData.Datas.Num());				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		uint32 BoundIndex = 0;
		for (const FHairStrandsDeepShadowData& DomData : ViewData.Datas)
		{
			Line = FString::Printf(TEXT(" %d - Bound Radus: %f.2m (%dx%d)"), BoundIndex++, DomData.Bounds.GetSphere().W / 10.f, DomData.ShadowResolution.X, DomData.ShadowResolution.Y);
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		}

		Canvas.Flush_RenderThread(RHICmdList);
	}

	if (StrandsDebugMode != EHairStrandsDebugMode::None || HairDebugMode != EHairDebugMode::None)
	{
		float X = 40;
		float Y = View.ViewRect.Height() - YStep * 3.f;
		FString Line;
		if (StrandsDebugMode != EHairStrandsDebugMode::None)
			Line = FString::Printf(TEXT("Hair Debug mode - %s"), ToString(StrandsDebugMode));
		else if (HairDebugMode != EHairDebugMode::None)
			Line = FString::Printf(TEXT("Hair Debug mode - %s"), ToString(HairDebugMode));

		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneTargets.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.FeatureLevel);
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 0));
		Canvas.Flush_RenderThread(RHICmdList);
	}
}
