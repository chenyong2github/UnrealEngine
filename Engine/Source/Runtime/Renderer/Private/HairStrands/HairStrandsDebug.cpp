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
#include "GpuDebugRendering.h"

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

static int32 GHairStrandsClusterDebug = 0;
static FAutoConsoleVariableRef CVarHairStrandsDebugClusterAABB(TEXT("r.HairStrands.Cluster.Debug"), GHairStrandsClusterDebug, TEXT("Draw debug the world bounding box of hair clusters used for culling optimisation."));

static int32 GHairDebugMeshProjection_SkinCacheMesh = 0;
static int32 GHairDebugMeshProjection_SkinCacheMeshInUVsSpace = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Sim_HairDeformedFrames = 0;

static int32 GHairDebugMeshProjection_Render_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_Render_HairDeformedFrames = 0;


static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMeshInUVsSpace(			TEXT("r.HairStrands.MeshProjection.DebugInUVsSpace"),			GHairDebugMeshProjection_SkinCacheMeshInUVsSpace, TEXT("Render debug mes projection in UVs space"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMesh(					TEXT("r.HairStrands.MeshProjection.DebugSkinCache"),			GHairDebugMeshProjection_SkinCacheMesh, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestTriangles(		TEXT("r.HairStrands.MeshProjection.Render.Rest.Triangles"),		GHairDebugMeshProjection_Render_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairRestFrames(			TEXT("r.HairStrands.MeshProjection.Render.Rest.Frames"),		GHairDebugMeshProjection_Render_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedTriangles(	TEXT("r.HairStrands.MeshProjection.Render.Deformed.Triangles"),	GHairDebugMeshProjection_Render_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Render_HairDeformedFrames(		TEXT("r.HairStrands.MeshProjection.Render.Deformed.Frames"),	GHairDebugMeshProjection_Render_HairDeformedFrames, TEXT("Render debug mes projection"));

static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestTriangles(			TEXT("r.HairStrands.MeshProjection.Sim.Rest.Triangles"),		GHairDebugMeshProjection_Sim_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairRestFrames(				TEXT("r.HairStrands.MeshProjection.Sim.Rest.Frames"),			GHairDebugMeshProjection_Sim_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedTriangles(		TEXT("r.HairStrands.MeshProjection.Sim.Deformed.Triangles"),	GHairDebugMeshProjection_Sim_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_Sim_HairDeformedFrames(			TEXT("r.HairStrands.MeshProjection.Sim.Deformed.Frames"),		GHairDebugMeshProjection_Sim_HairDeformedFrames, TEXT("Render debug mes projection"));
static int32 GHairStrandsDebugPPLL = 0;
static FAutoConsoleVariableRef CVarHairStrandsDebugPPLL(									TEXT("r.HairStrands.DebugPPLL"),									GHairStrandsDebugPPLL, TEXT("Draw debug per pixel light list rendering."));
static int32 GHairVirtualVoxel_DrawDebugPage = 0;
static FAutoConsoleVariableRef CVarHairVirtualVoxel_DrawDebugPage(TEXT("r.HairStrands.Voxelization.Virtual.DrawDebugPage"), GHairVirtualVoxel_DrawDebugPage, TEXT("When voxel debug rendering is enable, render the page bounds, instead of the voxel"));
static int32 GHairVirtualVoxel_DebugTraversalType = 0;
static FAutoConsoleVariableRef CVarHairVirtualVoxel_DebugTraversalType(TEXT("r.HairStrands.Voxelization.Virtual.DebugTraversalType"), GHairVirtualVoxel_DebugTraversalType, TEXT("Traversal mode (0:linear, 1:mip) for debug voxel visualization."));

// Helper functions for accessing interpolation data for debug purpose.
// Definitions is in HairStrandsInterface.cpp
void GetGroomInterpolationData(const EWorldType::Type WorldType, const EHairStrandsProjectionMeshType MeshtType, const FGPUSkinCache* SkinCache, FHairStrandsProjectionMeshData::LOD& OutGeometries);
void GetGroomInterpolationData(const EWorldType::Type WorldType, const EHairStrandsInterpolationType StrandType, const FGPUSkinCache* SkinCache, FHairStrandsProjectionHairData& OutHairData, TArray<int32>& OutLODIndices);

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

FHairStrandsDebugData::Data FHairStrandsDebugData::CreateData(FRDGBuilder& GraphBuilder)
{
	FHairStrandsDebugData::Data Out;
	Out.ShadingPointBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(ShadingInfo), MaxShadingPointCount), TEXT("HairDebugShadingPoint"));
	Out.ShadingPointCounter = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("HairDebugShadingPointCounter"));
	Out.SampleBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(Sample), MaxSampleCount), TEXT("HairDebugSample"));
	Out.SampleCounter = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("HairDebugSampleCounter"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.ShadingPointCounter, PF_R32_UINT), 0u);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.SampleCounter, PF_R32_UINT), 0u);
	return Out;
}

FHairStrandsDebugData::Data FHairStrandsDebugData::ImportData(FRDGBuilder& GraphBuilder, const FHairStrandsDebugData& In)
{
	FHairStrandsDebugData::Data Out;
	Out.ShadingPointBuffer = GraphBuilder.RegisterExternalBuffer(In.ShadingPointBuffer, TEXT("HairDebugShadingPoint"));
	Out.ShadingPointCounter = GraphBuilder.RegisterExternalBuffer(In.ShadingPointCounter, TEXT("HairDebugShadingPointCounter"));
	Out.SampleBuffer = GraphBuilder.RegisterExternalBuffer(In.SampleBuffer, TEXT("HairDebugSample"));
	Out.SampleCounter = GraphBuilder.RegisterExternalBuffer(In.SampleCounter, TEXT("HairDebugSampleCounter"));
	return Out;
}

void FHairStrandsDebugData::ExtractData(FRDGBuilder& GraphBuilder, FHairStrandsDebugData::Data& In, FHairStrandsDebugData& Out)
{
	GraphBuilder.QueueBufferExtraction(In.ShadingPointBuffer, &Out.ShadingPointBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	GraphBuilder.QueueBufferExtraction(In.ShadingPointCounter, &Out.ShadingPointCounter, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	GraphBuilder.QueueBufferExtraction(In.SampleBuffer, &Out.SampleBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	GraphBuilder.QueueBufferExtraction(In.SampleCounter, &Out.SampleCounter, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
}

void FHairStrandsDebugData::SetParameters(FRDGBuilder& GraphBuilder, FHairStrandsDebugData::Data& In, FHairStrandsDebugData::FWriteParameters& Out)
{
	Out.Debug_MaxSampleCount = FHairStrandsDebugData::MaxSampleCount;
	Out.Debug_MaxShadingPointCount = FHairStrandsDebugData::MaxShadingPointCount;
	Out.Debug_ShadingPointBuffer = GraphBuilder.CreateUAV(In.ShadingPointBuffer);
	Out.Debug_ShadingPointCounter = GraphBuilder.CreateUAV(In.ShadingPointCounter, PF_R32_UINT);
	Out.Debug_SampleBuffer = GraphBuilder.CreateUAV(In.SampleBuffer);
	Out.Debug_SampleCounter = GraphBuilder.CreateUAV(In.SampleCounter, PF_R32_UINT);
}

void FHairStrandsDebugData::SetParameters(FRDGBuilder& GraphBuilder, FHairStrandsDebugData::Data& In, FHairStrandsDebugData::FReadParameters& Out)
{
	Out.Debug_MaxSampleCount = FHairStrandsDebugData::MaxSampleCount;
	Out.Debug_MaxShadingPointCount = FHairStrandsDebugData::MaxShadingPointCount;
	Out.Debug_ShadingPointBuffer = GraphBuilder.CreateSRV(In.ShadingPointBuffer);
	Out.Debug_ShadingPointCounter = GraphBuilder.CreateSRV(In.ShadingPointCounter, PF_R32_UINT);
	Out.Debug_SampleBuffer = GraphBuilder.CreateSRV(In.SampleBuffer);
	Out.Debug_SampleCounter = GraphBuilder.CreateSRV(In.SampleCounter, PF_R32_UINT);
}

enum class EHairDebugMode : uint8
{
	None,
	MacroGroups,
	LightBounds,
	DeepOpacityMaps,
	MacroGroupScreenRect,
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
	MaterialTangent,
	Tile
};

static EHairDebugMode GetHairDebugMode()
{
	switch (GHairStrandsDebugMode)
	{
	case 0:  return EHairDebugMode::None;
	case 1:  return EHairDebugMode::MacroGroups;
	case 2:  return EHairDebugMode::LightBounds;
	case 3:  return EHairDebugMode::MacroGroupScreenRect;
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
	case 19: return EHairDebugMode::Tile;
	default: return EHairDebugMode::None;
	};
}

static const TCHAR* ToString(EHairDebugMode DebugMode)
{
	switch (DebugMode)
	{
	case EHairDebugMode::None: return TEXT("None");
	case EHairDebugMode::MacroGroups: return TEXT("Macro groups info");
	case EHairDebugMode::LightBounds: return TEXT("All DOMs light bounds");
	case EHairDebugMode::MacroGroupScreenRect: return TEXT("Screen projected macro groups");
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
	case EHairDebugMode::Tile: return TEXT("Hair tile cotegorization");
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
	case 11:  return EHairStrandsDebugMode::RenderVisCluster;
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
	case EHairStrandsDebugMode::RenderVisCluster			: return TEXT("Hair visility clusters");
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
		SHADER_PARAMETER(uint32, HairMacroGroupCount)
		SHADER_PARAMETER(uint32, MaxSampleCount)
		SHADER_PARAMETER(uint32, HairVisibilityNodeGroupSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountUintTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, HairVisibilityIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairMacroGroupAABBBuffer)
		SHADER_PARAMETER_SRV(Texture2D, DepthStencilTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawUniformBuffer)
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
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FShaderResourceViewRHIRef& InDepthStencilTexture)
{
	const TRefCountPtr<IPooledRenderTarget>& InViewHairCountTexture = VisibilityData.ViewHairCountTexture;
	const TRefCountPtr<IPooledRenderTarget>& InCategorizationTexture = VisibilityData.CategorizationTexture;
	const TRefCountPtr<IPooledRenderTarget>& InNodeIndex = VisibilityData.NodeIndex;
	const TRefCountPtr<FPooledRDGBuffer>& InNodeData = VisibilityData.NodeData;
	
	if (!InCategorizationTexture || !InNodeIndex || !InNodeData || !InDepthStencilTexture) return;

	FRDGTextureRef ViewHairCountTexture = InViewHairCountTexture ? GraphBuilder.RegisterExternalTexture(InViewHairCountTexture, TEXT("ViewHairCountTexture")) : GSystemTextures.GetBlackDummy(GraphBuilder);
	FRDGTextureRef ViewHairCountUintTexture = VisibilityData.ViewHairCountUintTexture ? GraphBuilder.RegisterExternalTexture(VisibilityData.ViewHairCountUintTexture, TEXT("ViewHairCountUintTexture")) : GSystemTextures.GetBlackDummy(GraphBuilder);
	FRDGTextureRef CategorizationTexture = InCategorizationTexture ? GraphBuilder.RegisterExternalTexture(InCategorizationTexture, TEXT("CategorizationTexture")) : nullptr;
	FRDGTextureRef NodeIndex = InNodeIndex ? GraphBuilder.RegisterExternalTexture(InNodeIndex, TEXT("NodeIndex")) : nullptr;
	FRDGBufferRef  NodeData  = InNodeData ? GraphBuilder.RegisterExternalBuffer(InNodeData, TEXT("NodeData")) : nullptr;
	FRDGBufferRef  HairMacroGroupAABBBuffer = GraphBuilder.RegisterExternalBuffer(MacroGroupDatas.MacroGroupResources.MacroGroupAABBsBuffer, TEXT("MacroGroupData"));

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairDebugPrintCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPrintCS::FParameters>();
	Parameters->GroupSize = GetVendorOptimalGroupSize2D();
	Parameters->MaxResolution = CategorizationTexture ? CategorizationTexture->Desc.Extent : FIntPoint(0,0);
	Parameters->PixelCoord = View->CursorPos;
	Parameters->FastResolveMask = STENCIL_TEMPORAL_RESPONSIVE_AA_MASK;
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->HairCountTexture = ViewHairCountTexture;
	Parameters->HairCountUintTexture = ViewHairCountUintTexture;
	Parameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(NodeData);
	Parameters->HairVisibilityNodeOffsetAndCount = NodeIndex;
	Parameters->HairVisibilityIndirectArgsBuffer = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeIndirectArg), PF_R32_UINT);
	Parameters->HairVisibilityNodeGroupSize = VisibilityData.NodeGroupSize;
	Parameters->DepthStencilTexture = InDepthStencilTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->HairMacroGroupCount = MacroGroupDatas.Datas.Num();
	Parameters->MaxSampleCount = VisibilityData.MaxSampleCount;
	Parameters->HairMacroGroupAABBBuffer = GraphBuilder.CreateSRV(HairMacroGroupAABBBuffer, PF_R32_SINT);
	ShaderPrint::SetParameters(*View, Parameters->ShaderPrintUniformBuffer);
	ShaderDrawDebug::SetParameters(GraphBuilder, View->ShaderDrawData, Parameters->ShaderDrawUniformBuffer);
	TShaderMapRef<FHairDebugPrintCS> ComputeShader(View->ShaderMap);

	ClearUnusedGraphResources(ComputeShader, Parameters);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsDebugPrint"),
		ComputeShader,
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
		SHADER_PARAMETER(uint32, TileSize)
		SHADER_PARAMETER(uint32, MaxSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCountUintTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NodeIndex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TileIndexTexture)
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
	const FHairStrandsVisibilityData& VisibilityData,
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
		InDebugMode == EHairDebugMode::MaterialTangent ||
		InDebugMode == EHairDebugMode::Tile);

	const TRefCountPtr<IPooledRenderTarget>& InCategorizationTexture = VisibilityData.CategorizationTexture;
	const TRefCountPtr<IPooledRenderTarget>& InNodeIndex = VisibilityData.NodeIndex;
	const TRefCountPtr<FPooledRDGBuffer>& InNodeData = VisibilityData.NodeData;
	const TRefCountPtr<IPooledRenderTarget>& InTileIndexTexture = VisibilityData.TileIndexTexture;

	if (!InCategorizationTexture || !InNodeIndex || !InNodeData || !InTileIndexTexture) return;
	if (InDebugMode == EHairDebugMode::TAAResolveType && !InDepthStencilTexture) return;

	FRDGTextureRef CategorizationTexture = InCategorizationTexture ? GraphBuilder.RegisterExternalTexture(InCategorizationTexture, TEXT("CategorizationTexture")) : nullptr;
	FRDGTextureRef NodeIndex = InNodeIndex ? GraphBuilder.RegisterExternalTexture(InNodeIndex, TEXT("NodeIndex")) : nullptr;
	FRDGBufferRef  NodeData = InNodeData ? GraphBuilder.RegisterExternalBuffer(InNodeData, TEXT("NodeData")) : nullptr;
	FRDGTextureRef TileIndexTexture = InTileIndexTexture ? GraphBuilder.RegisterExternalTexture(InTileIndexTexture, TEXT("TileIndexTexture")) : nullptr;

	FRDGTextureRef HairCountTexture = VisibilityData.ViewHairCountTexture ? GraphBuilder.RegisterExternalTexture(VisibilityData.ViewHairCountTexture, TEXT("HairCount")) : GSystemTextures.GetBlackDummy(GraphBuilder);
	FRDGTextureRef HairCountUintTexture = VisibilityData.ViewHairCountUintTexture ? GraphBuilder.RegisterExternalTexture(VisibilityData.ViewHairCountUintTexture, TEXT("HairCountUint")) : GSystemTextures.GetBlackDummy(GraphBuilder); 
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
		case EHairDebugMode::Tile:				InternalDebugMode = 9; break;
	};

	FHairDebugPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPS::FParameters>();
	Parameters->ViewUniformBuffer = View->ViewUniformBuffer;
	Parameters->OutputResolution = Resolution;
	Parameters->FastResolveMask = STENCIL_TEMPORAL_RESPONSIVE_AA_MASK;
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->HairCountTexture = HairCountTexture;
	Parameters->HairCountUintTexture = HairCountUintTexture;
	Parameters->NodeIndex = NodeIndex;
	Parameters->NodeData = GraphBuilder.CreateSRV(NodeData);
	Parameters->TileIndexTexture = TileIndexTexture;
	Parameters->TileSize = VisibilityData.TileSize;
	Parameters->DepthStencilTexture = InDepthStencilTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->DebugMode = InternalDebugMode;
	Parameters->SampleIndex = GHairStrandsDebugSampleIndex;
	Parameters->MaxSampleCount = VisibilityData.MaxSampleCount;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTarget, ERenderTargetLoadAction::ELoad, 0);
	TShaderMapRef<FPostProcessVS> VertexShader(View->ShaderMap);

	TShaderMapRef<FHairDebugPS> PixelShader(View->ShaderMap);

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsDebug"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, View](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_Zero>::GetRHI();
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
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_VISUALIZEDOM"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDeepShadowVisualizePS, "/Engine/Private/HairStrands/HairStrandsDeepShadowDebug.usf", "VisualizeDomPS", SF_Pixel);

static void AddDebugDeepShadowTexturePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	const FIntRect& HairViewRect,
	const FHairStrandsDeepShadowData* ShadowData,
	const FDeepShadowResources* Resources,
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
		DeepShadowDepthTexture = GraphBuilder.RegisterExternalTexture(Resources->DepthAtlasTexture, TEXT("DOMDepthTexture"));
		DeepShadowLayerTexture = GraphBuilder.RegisterExternalTexture(Resources->LayersAtlasTexture, TEXT("DOMLayerTexture"));

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

	ClearUnusedGraphResources(PixelShader, Parameters);

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
class FDeepShadowInfoCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeepShadowInfoCS);
	SHADER_USE_PARAMETER_STRUCT(FDeepShadowInfoCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(FVector2D, OutputResolution)
		SHADER_PARAMETER(uint32, AllocatedSlotCount)
		SHADER_PARAMETER(uint32, MacroGroupCount)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MacroGroupAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ShadowWorldToLightTransformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_DOMINFO"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDeepShadowInfoCS, "/Engine/Private/HairStrands/HairStrandsDeepShadowDebug.usf", "MainCS", SF_Compute);

static void AddDeepShadowInfoPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	FRDGTextureRef& OutputTexture)
{
	if (MacroGroupDatas.DeepShadowResources.TotalAtlasSlotCount == 0)
	{
		return;
	}

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	FRDGBufferRef MacroGroupAABBsBuffer = GraphBuilder.RegisterExternalBuffer(MacroGroupDatas.MacroGroupResources.MacroGroupAABBsBuffer);
	FRDGBufferRef DeepShadowWorldToLightTransforms = GraphBuilder.RegisterExternalBuffer(MacroGroupDatas.DeepShadowResources.DeepShadowWorldToLightTransforms);

	const FIntPoint Resolution(OutputTexture->Desc.Extent);
	FDeepShadowInfoCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeepShadowInfoCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->OutputResolution = Resolution;
	Parameters->AllocatedSlotCount = MacroGroupDatas.DeepShadowResources.TotalAtlasSlotCount;
	Parameters->MacroGroupCount = MacroGroupDatas.MacroGroupResources.MacroGroupCount;
	Parameters->SceneTextures = SceneTextures;
	Parameters->MacroGroupAABBBuffer = GraphBuilder.CreateSRV(MacroGroupAABBsBuffer, PF_R32_SINT);
	Parameters->ShadowWorldToLightTransformBuffer = GraphBuilder.CreateSRV(DeepShadowWorldToLightTransforms);
	ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	ShaderPrint::SetParameters(View, Parameters->ShaderPrintParameters);
	Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

	TShaderMapRef<FDeepShadowInfoCS> ComputeShader(View.ShaderMap);

	const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y, 1), FIntVector(8, 8, 1));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsDeepShadowDebugInfo"), ComputeShader, Parameters, DispatchCount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FVoxelVirtualRaymarchingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelVirtualRaymarchingCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelVirtualRaymarchingCS, FGlobalShader);

	class FTraversalType : SHADER_PERMUTATION_INT("PERMUTATION_TRAVERSAL", 2);
	using FPermutationDomain = TShaderPermutationDomain<FTraversalType>;
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		SHADER_PARAMETER(FVector2D, OutputResolution)
		SHADER_PARAMETER(uint32, bDrawPage)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, MacroGroupCount)
		SHADER_PARAMETER(uint32, MaxTotalPageIndexCount)
		SHADER_PARAMETER_STRUCT_REF(FVirtualVoxelParameters, VirtualVoxel)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TotalValidPageCounter)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FVoxelVirtualRaymarchingCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageRayMarching.usf", "MainCS", SF_Compute);

static void AddVoxelPageRaymarchingPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	FRDGTextureRef& OutputTexture)
{
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const FVirtualVoxelResources& VoxelResources = MacroGroupDatas.VirtualVoxelResources;
	const FIntPoint Resolution(OutputTexture->Desc.Extent);
	for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
	{
		const FRDGBufferRef PageIndexBuffer	= GraphBuilder.RegisterExternalBuffer(VoxelResources.PageIndexBuffer, TEXT("HairVoxelPageIndexBuffer"));
		const FRDGTextureRef PageTexture = GraphBuilder.RegisterExternalTexture(VoxelResources.PageTexture, TEXT("HairVoxelPageTexture"));
		const FRDGBufferRef PageIndexGlobalCounter = GraphBuilder.RegisterExternalBuffer(VoxelResources.PageIndexGlobalCounter, TEXT("HairVoxelValidPageCountBuffer"));

		FVoxelVirtualRaymarchingCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelVirtualRaymarchingCS::FParameters>();
		Parameters->ViewUniformBuffer		= View.ViewUniformBuffer;
		Parameters->OutputResolution		= Resolution;
		Parameters->SceneTextures			= SceneTextures;
		Parameters->bDrawPage				= GHairVirtualVoxel_DrawDebugPage ? 1 : 0;
		Parameters->MacroGroupId			= MacroGroupData.MacroGroupId;
		Parameters->MacroGroupCount			= MacroGroupDatas.Datas.Num();
		Parameters->MaxTotalPageIndexCount  = VoxelResources.Parameters.Common.PageIndexCount;
		Parameters->VirtualVoxel			= VoxelResources.UniformBuffer;
		Parameters->TotalValidPageCounter	= GraphBuilder.CreateSRV(PageIndexGlobalCounter, PF_R32_UINT);
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
		ShaderPrint::SetParameters(View, Parameters->ShaderPrintParameters);
		Parameters->OutputTexture			= GraphBuilder.CreateUAV(OutputTexture);

		FVoxelVirtualRaymarchingCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVoxelVirtualRaymarchingCS::FTraversalType>(GHairVirtualVoxel_DebugTraversalType > 0 ? 1 : 0);
		TShaderMapRef<FVoxelVirtualRaymarchingCS> ComputeShader(View.ShaderMap, PermutationVector);

		const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y, 1), FIntVector(8, 8, 1));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVoxelVirtualRaymarching"), ComputeShader, Parameters, DispatchCount);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairStrandsPlotBSDFPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsPlotBSDFPS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsPlotBSDFPS, FGlobalShader);

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
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PLOTBSDF"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsPlotBSDFPS, "/Engine/Private/HairStrands/HairStrandsBsdfPlot.usf", "MainPS", SF_Pixel);

static void AddPlotBSDFPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef& OutputTexture)
{
	
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const FIntPoint Resolution(OutputTexture->Desc.Extent);
	FHairStrandsPlotBSDFPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsPlotBSDFPS::FParameters>();
	Parameters->InputCoord = View.CursorPos;
	Parameters->OutputOffset = FIntPoint(10,100);
	Parameters->OutputResolution = FIntPoint(256, 256);
	Parameters->MaxResolution = OutputTexture->Desc.Extent;
	Parameters->HairComponents = ToBitfield(GetHairComponents());
	Parameters->Roughness = GHairStrandsDebugPlotBsdfRoughness;
	Parameters->BaseColor = GHairStrandsDebugPlotBsdfBaseColor;
	Parameters->Exposure = GHairStrandsDebugPlotBsdfExposure;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

	const FIntPoint OutputResolution = SceneTextures.SceneDepthBuffer->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsPlotBSDFPS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

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
class FHairStrandsPlotSamplePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsPlotSamplePS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsPlotSamplePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FHairStrandsDebugData::FReadParameters, DebugData)
		SHADER_PARAMETER(FIntPoint, OutputOffset)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER(FIntPoint, MaxResolution)
		SHADER_PARAMETER(uint32, HairComponents)
		SHADER_PARAMETER(float, Exposure)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PLOTSAMPLE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsPlotSamplePS, "/Engine/Private/HairStrands/HairStrandsBsdfPlot.usf", "MainPS", SF_Pixel);

static void AddPlotSamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FHairStrandsDebugData::Data& DebugData,
	FRDGTextureRef& OutputTexture)
{

	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const FIntPoint Resolution(OutputTexture->Desc.Extent);
	FHairStrandsPlotSamplePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsPlotSamplePS::FParameters>();

	FHairStrandsDebugData::SetParameters(GraphBuilder, DebugData, Parameters->DebugData);
	Parameters->OutputOffset = FIntPoint(100, 100);
	Parameters->OutputResolution = FIntPoint(256, 256);
	Parameters->MaxResolution = OutputTexture->Desc.Extent;
	Parameters->HairComponents = ToBitfield(GetHairComponents());
	Parameters->Exposure = GHairStrandsDebugPlotBsdfExposure;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

	const FIntPoint OutputResolution = SceneTextures.SceneDepthBuffer->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsPlotSamplePS> PixelShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsSamplePlot"),
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
BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionMeshDebugParameters, )
	SHADER_PARAMETER(FMatrix, LocalToWorld)
	SHADER_PARAMETER(uint32, VertexOffset)
	SHADER_PARAMETER(uint32, IndexOffset)
	SHADER_PARAMETER(uint32, MaxIndexCount)
	SHADER_PARAMETER(uint32, MaxVertexCount)
	SHADER_PARAMETER(uint32, MeshUVsChannelOffset)
	SHADER_PARAMETER(uint32, MeshUVsChannelCount)
	SHADER_PARAMETER(uint32, bOutputInUVsSpace)
	SHADER_PARAMETER(uint32, MeshType)
	SHADER_PARAMETER(uint32, SectionIndex)
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputIndexBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexPositionBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer, InputVertexUVsBuffer)
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
	const EHairStrandsProjectionMeshType MeshType,
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
	Parameters->MeshType = uint32(MeshType);
	Parameters->bOutputInUVsSpace = GHairDebugMeshProjection_SkinCacheMeshInUVsSpace ? 1 : 0;
	Parameters->VertexOffset = MeshSectionData.VertexBaseIndex;
	Parameters->IndexOffset = MeshSectionData.IndexBaseIndex;
	Parameters->MaxIndexCount = MeshSectionData.TotalIndexCount;
	Parameters->MaxVertexCount = MeshSectionData.TotalVertexCount;
	Parameters->MeshUVsChannelOffset = MeshSectionData.UVsChannelOffset;
	Parameters->MeshUVsChannelCount = MeshSectionData.UVsChannelCount;
	Parameters->InputIndexBuffer  = MeshSectionData.IndexBuffer;
	Parameters->InputVertexPositionBuffer = MeshSectionData.PositionBuffer;
	Parameters->InputVertexUVsBuffer = MeshSectionData.UVsBuffer;
	Parameters->SectionIndex = MeshSectionData.SectionIndex;
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
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PrimitiveType;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

		// Emit an instanced quad draw call on the order of the number of pixels on the screen.	
		RHICmdList.DrawPrimitive(0, PrimitiveCount, 1);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
BEGIN_SHADER_PARAMETER_STRUCT(FHairProjectionHairDebugParameters, )
	SHADER_PARAMETER(FVector2D, OutputResolution)
	SHADER_PARAMETER(uint32, MaxRootCount)
	SHADER_PARAMETER(uint32, DeformedFrameEnable)
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

	if (PrimitiveCount == 0 || LODIndex < 0 || LODIndex >= HairData.RestLODDatas.Num() || LODIndex >= HairData.DeformedLODDatas.Num())
		return;

	if (EDebugProjectionHairType::HairFrame == GeometryType && (!HairData.RootPositionBuffer || !HairData.RootNormalBuffer || !HairData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer))
		return;

	if (!HairData.RestLODDatas[LODIndex].RestRootTrianglePosition0Buffer ||
		!HairData.RestLODDatas[LODIndex].RestRootTrianglePosition1Buffer ||
		!HairData.RestLODDatas[LODIndex].RestRootTrianglePosition2Buffer ||
		!HairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition0Buffer ||
		!HairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition1Buffer ||
		!HairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition2Buffer)
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
		Parameters->RootBarycentricBuffer = HairData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;
	}

	Parameters->RestPosition0Buffer = HairData.RestLODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
	Parameters->RestPosition1Buffer = HairData.RestLODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
	Parameters->RestPosition2Buffer = HairData.RestLODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;
	
	Parameters->DeformedPosition0Buffer = HairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
	Parameters->DeformedPosition1Buffer = HairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
	Parameters->DeformedPosition2Buffer = HairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;

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
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PrimitiveType;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParameters);

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

class FDrawDebugClusterAABBCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDebugClusterAABBCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDebugClusterAABBCS, FGlobalShader);

	class FDebugAABBBuffer : SHADER_PERMUTATION_INT("PERMUTATION_DEBUGAABBBUFFER", 2);
	using FPermutationDomain = TShaderPermutationDomain<FDebugAABBBuffer>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_SRV(Buffer, ClusterAABBBuffer)
		SHADER_PARAMETER_SRV(Buffer, GroupAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, CulledDispatchIndirectParametersClusterCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterDebugAABBBuffer)
		SHADER_PARAMETER_SRV(Buffer, CulledDrawIndirectParameters)
		SHADER_PARAMETER(uint32, ClusterCount)
		SHADER_PARAMETER(uint32, TriangleCount)
		SHADER_PARAMETER(uint32, HairGroupId)
		SHADER_PARAMETER(int32, ClusterDebugMode)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_DRAWDEBUGAABB"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDrawDebugClusterAABBCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainDrawDebugAABBCS", SF_Compute);

static void AddDrawDebugClusterPass(
	FRHICommandListImmediate& RHICmdList,
	const FHairStrandClusterData& HairClusterData,
	TArray<FViewInfo>& Views)
{
	check(ShaderDrawDebug::IsShaderDrawDebugEnabled());

	static IConsoleVariable* CVarClusterCullingEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Cluster.Culling"));
	if (CVarClusterCullingEnabled && CVarClusterCullingEnabled->GetInt() <= 0)
	{
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	for (int DataIndex = 0; DataIndex < HairClusterData.HairGroups.Num(); ++DataIndex)
	{
		const FHairStrandClusterData::FHairGroup& HairGroupClusters = HairClusterData.HairGroups[DataIndex];

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewInfo& ViewInfo = Views[ViewIndex];
			if (ShaderDrawDebug::IsShaderDrawDebugEnabled(ViewInfo))
			{
				FRDGBuilder GraphBuilder(RHICmdList);

				FRDGBufferRef CulledDispatchIndirectParametersClusterCount = GraphBuilder.RegisterExternalBuffer(HairGroupClusters.CulledDispatchIndirectParametersClusterCount);
				FRWBuffer& DrawIndirectBuffer = HairGroupClusters.HairGroupPublicPtr->GetDrawIndirectBuffer();

				FDrawDebugClusterAABBCS::FPermutationDomain Permutation;
				Permutation.Set<FDrawDebugClusterAABBCS::FDebugAABBBuffer>(GHairStrandsClusterDebug > 1 ? 1 : 0);
				TShaderMapRef<FDrawDebugClusterAABBCS> ComputeShader(ShaderMap, Permutation);

				FDrawDebugClusterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDrawDebugClusterAABBCS::FParameters>();
				Parameters->ViewUniformBuffer = ViewInfo.ViewUniformBuffer;
				Parameters->ClusterCount = HairGroupClusters.ClusterCount;
				Parameters->TriangleCount = HairGroupClusters.VertexCount / 3;
				Parameters->HairGroupId = DataIndex;
				Parameters->ClusterDebugMode = GHairStrandsClusterDebug;
				Parameters->ClusterAABBBuffer = HairGroupClusters.ClusterAABBBuffer->SRV;
				Parameters->CulledDispatchIndirectParametersClusterCountBuffer = GraphBuilder.CreateSRV(CulledDispatchIndirectParametersClusterCount, EPixelFormat::PF_R32_UINT);
				Parameters->CulledDrawIndirectParameters = DrawIndirectBuffer.SRV;
				Parameters->GroupAABBBuffer = HairGroupClusters.GroupAABBBuffer->SRV;

				if (HairGroupClusters.ClusterDebugAABBBuffer && GHairStrandsClusterDebug>1)
				{
					FRDGBufferRef ClusterDebugAABBBuffer = GraphBuilder.RegisterExternalBuffer(HairGroupClusters.ClusterDebugAABBBuffer);
					Parameters->ClusterDebugAABBBuffer = GraphBuilder.CreateSRV(ClusterDebugAABBBuffer, EPixelFormat::PF_R32_SINT);
				}
				ShaderDrawDebug::SetParameters(GraphBuilder, ViewInfo.ShaderDrawData, Parameters->ShaderDrawParameters);
				ShaderPrint::SetParameters(ViewInfo, Parameters->ShaderPrintParameters);

				check(Parameters->ClusterCount/64 <= 65535);
				const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(Parameters->ClusterCount, 1, 1), FIntVector(64, 1, 1));// FIX ME, this could get over 65535
				FComputeShaderUtils::AddPass(
					GraphBuilder, RDG_EVENT_NAME("DrawDebugClusterAABB"),
					ComputeShader, Parameters, DispatchCount);

				GraphBuilder.Execute();
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void RenderHairStrandsDebugInfo(
	FRHICommandListImmediate& RHICmdList, 
	TArray<FViewInfo>& Views, 
	const FHairStrandsDatas* HairDatas, 
	const struct FHairStrandClusterData& HairClusterData)
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

	if (HairDatas && (GHairStrandsDebugPlotBsdf > 0 || HairDatas->DebugData.IsValid()))
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (GHairStrandsDebugPlotBsdf > 0)
		{
			AddPlotBSDFPass(GraphBuilder, View, SceneColorTexture);
		}
		if (HairDatas->DebugData.IsValid())
		{
			FHairStrandsDebugData::Data DebugData = FHairStrandsDebugData::ImportData(GraphBuilder, HairDatas->DebugData);
			AddPlotSamplePass(GraphBuilder, View, DebugData, SceneColorTexture);
		}
		GraphBuilder.Execute();		
	}

	float ClusterY = 38;
	if (HairDebugMode == EHairDebugMode::MacroGroups)
	{
		// Component part of the clusters
		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneTargets.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.FeatureLevel);

		float X = 20;
		float Y = ClusterY;
		const FLinearColor InactiveColor(0.5, 0.5, 0.5);
		const FLinearColor DebugColor(1, 1, 0);
		const FLinearColor DebugGroupColor(0.5f, 0, 0);
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

			Line = FString::Printf(TEXT(" * Id:%d | WorldType:%s | Group count : %d | Asset : %s | Skeletal : %s "), DebugInfo.ComponentId, ToString(DebugInfo.WorldType), DebugInfo.HairGroups.Num(), *DebugInfo.GroomAssetName, *DebugInfo.SkeletalComponentName);
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugColor : InactiveColor);

			for (const FHairStrandsDebugInfo::HairGroup& DebugHairGroup : DebugInfo.HairGroups)
			{
				Line = FString::Printf(TEXT("        |> CurveCount : %d | VertexCount : %d | MaxRadius : %f | MaxLength : %f | Skinned: %s | Binding: %s | Simulation: %s| LOD count : %d"),
					DebugHairGroup.CurveCount,
					DebugHairGroup.VertexCount,
					DebugHairGroup.MaxRadius,
					DebugHairGroup.MaxLength,
					DebugHairGroup.bHasSkinInterpolation ? TEXT("True") : TEXT("False"),
					DebugHairGroup.bHasBinding ? TEXT("True") : TEXT("False"),
					DebugHairGroup.bHasSimulation ? TEXT("True") : TEXT("False"),
					DebugHairGroup.LODCount);
				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), bIsActive ? DebugGroupColor : InactiveColor);
			}
		}

		Canvas.Flush_RenderThread(RHICmdList);

		ClusterY = Y;
	}

	if (!HairDatas)
		return;

	const FHairStrandsMacroGroupViews& InMacroGroupViews = HairDatas->MacroGroupsPerViews;

	if (HairDebugMode == EHairDebugMode::MacroGroups)
	{
		if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
			AddDebugHairPrintPass(GraphBuilder, &View, HairDebugMode, VisibilityData, MacroGroupDatas, SceneTargets.SceneStencilSRV);
			GraphBuilder.Execute();	
		}

		// CPU bound of macro groups
		FViewElementPDI ShadowFrustumPDI(&View, nullptr, nullptr);
		const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
		if (MacroGroupDatas.VirtualVoxelResources.IsValid())
		{
			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
			{
				const FBox Bound(MacroGroupData.VirtualVoxelNodeDesc.WorldMinAABB, MacroGroupData.VirtualVoxelNodeDesc.WorldMaxAABB);
				DrawWireBox(&ShadowFrustumPDI, Bound, FColor::Red, 0);
			}
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

		Line = FString::Printf(TEXT("Macro group count : %d"), MacroGroupDatas.Datas.Num());
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
		{
			Line = FString::Printf(TEXT(" %d - Bound Radus: %f.2m (%dx%d)"), MacroGroupData.MacroGroupId, MacroGroupData.Bounds.GetSphere().W);
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		}

		Canvas.Flush_RenderThread(RHICmdList);
	}

	if (HairDebugMode == EHairDebugMode::DeepOpacityMaps)
	{
		if (ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
			for (const FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas.Datas)
			{
				TRefCountPtr<IPooledRenderTarget> DepthTexture = MacroGroupDatas.DeepShadowResources.DepthAtlasTexture;
				TRefCountPtr<IPooledRenderTarget> LayerTexture = MacroGroupDatas.DeepShadowResources.LayersAtlasTexture;

				if (!DepthTexture || !LayerTexture)
				{
					continue;
				}

				for (const FHairStrandsDeepShadowData& DeepShadowData : MacroGroup.DeepShadowDatas.Datas)
				{
					const uint32 DomIndex = GDeepShadowDebugIndex;
					if (DeepShadowData.AtlasSlotIndex != DomIndex)
						continue;

					FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
					AddDebugDeepShadowTexturePass(GraphBuilder, &View, FIntRect(), &DeepShadowData, &MacroGroupDatas.DeepShadowResources, SceneColorTexture);
				}
			}
			GraphBuilder.Execute();
		}
	}

	// View Rect
	if (IsHairStrandsViewRectOptimEnable() && HairDebugMode == EHairDebugMode::MacroGroupScreenRect)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
			for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
			{
				AddDebugDeepShadowTexturePass(GraphBuilder, &View, MacroGroupData.ScreenRect, nullptr, nullptr, SceneColorTexture);
			}

			const FIntRect TotalRect = ComputeVisibleHairStrandsMacroGroupsRect(View.ViewRect, MacroGroupDatas);
			AddDebugDeepShadowTexturePass(GraphBuilder, &View, TotalRect, nullptr, nullptr, SceneColorTexture);
		}
		GraphBuilder.Execute();
	}
	
	const bool bIsVoxelMode = HairDebugMode == EHairDebugMode::VoxelsDensity || HairDebugMode == EHairDebugMode::VoxelsTangent || HairDebugMode == EHairDebugMode::VoxelsBaseColor || HairDebugMode == EHairDebugMode::VoxelsRoughness;

	// Render Frustum for all lights & macro groups 
	{
		if ((HairDebugMode == EHairDebugMode::LightBounds || HairDebugMode == EHairDebugMode::DeepOpacityMaps) && ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			if (ViewIndex < uint32(InMacroGroupViews.Views.Num()))
			{
				const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
				if (MacroGroupDatas.DeepShadowResources.bIsGPUDriven)
				{
					FRDGBuilder GraphBuilder(RHICmdList);
					FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
					AddDeepShadowInfoPass(GraphBuilder, View, MacroGroupDatas, SceneColorTexture);
					GraphBuilder.Execute();
				}
			}
		}

		FViewElementPDI ShadowFrustumPDI(&View, nullptr, nullptr);

		// All DOMs
		if (HairDebugMode == EHairDebugMode::LightBounds && ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			if (!InMacroGroupViews.Views[ViewIndex].DeepShadowResources.bIsGPUDriven)
			{
				for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupViews.Views[ViewIndex].Datas)
				{
					for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas.Datas)
					{
						DrawFrustumWireframe(&ShadowFrustumPDI, DomData.CPU_WorldToLightTransform.Inverse(), FColor::Emerald, 0);
						DrawWireBox(&ShadowFrustumPDI, DomData.Bounds.GetBox(), FColor::Yellow, 0);
					}
				}
			}
		}

		// Current DOM
		if (HairDebugMode == EHairDebugMode::DeepOpacityMaps && ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			if (!InMacroGroupViews.Views[ViewIndex].DeepShadowResources.bIsGPUDriven)
			{
				const int32 CurrentIndex = FMath::Max(0, GDeepShadowDebugIndex);
				for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupViews.Views[ViewIndex].Datas)
				{
					for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas.Datas)
					{
						if (DomData.AtlasSlotIndex == CurrentIndex)
						{
							DrawFrustumWireframe(&ShadowFrustumPDI, DomData.CPU_WorldToLightTransform.Inverse(), FColor::Emerald, 0);
							DrawWireBox(&ShadowFrustumPDI, DomData.Bounds.GetBox(), FColor::Yellow, 0);
						}
					}
				}
			}
		}

		// Voxelization
		if (bIsVoxelMode && ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
			if (MacroGroupDatas.VirtualVoxelResources.IsValid())
			{
				for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
				{
					const FBox Bound(MacroGroupData.VirtualVoxelNodeDesc.WorldMinAABB, MacroGroupData.VirtualVoxelNodeDesc.WorldMaxAABB);
					DrawWireBox(&ShadowFrustumPDI, Bound, FColor::Red, 0);
					DrawFrustumWireframe(&ShadowFrustumPDI, MacroGroupData.VirtualVoxelNodeDesc.WorldToClip.Inverse(), FColor::Purple, 0);
				}
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
		HairDebugMode == EHairDebugMode::MaterialTangent ||
		HairDebugMode == EHairDebugMode::Tile;
	if (bRunDebugPass)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
		{
			const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
			AddDebugHairPass(GraphBuilder, &View, HairDebugMode, VisibilityData, SceneTargets.SceneStencilSRV, SceneColorTexture);
			AddDebugHairPrintPass(GraphBuilder, &View, HairDebugMode, VisibilityData, MacroGroupDatas, SceneTargets.SceneStencilSRV);
		}

		GraphBuilder.Execute();
	}

	if (bIsVoxelMode)
	{
		if (ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = InMacroGroupViews.Views[ViewIndex];
			if (MacroGroupDatas.VirtualVoxelResources.IsValid())
			{
				FRDGBuilder GraphBuilder(RHICmdList);
				FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
				AddVoxelPageRaymarchingPass(GraphBuilder, View, MacroGroupDatas, SceneColorTexture);
				GraphBuilder.Execute();
			}
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

			const FGPUSkinCache* SkinCache = View.Family->Scene->GetGPUSkinCache();
			if (GHairDebugMeshProjection_SkinCacheMesh > 0)
			{
				FViewInfo* LocalView = &View;
				auto RenderMeshProjection = [&bClearDepth, WorldType, LocalView, SkinCache, &SceneColorTexture, &DepthTexture](FRDGBuilder& LocalGraphBuilder, EHairStrandsProjectionMeshType MeshType)
				{
					FHairStrandsProjectionMeshData::LOD MeshProjectionLODData;
					GetGroomInterpolationData(WorldType, MeshType, SkinCache, MeshProjectionLODData);
					for (FHairStrandsProjectionMeshData::Section& Section : MeshProjectionLODData.Sections)
					{
						AddDebugProjectionMeshPass(LocalGraphBuilder, LocalView, MeshType, bClearDepth, Section, SceneColorTexture, DepthTexture);
						bClearDepth = false;
					}
				};

				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::DeformedMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::RestMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::SourceMesh);
				RenderMeshProjection(GraphBuilder, EHairStrandsProjectionMeshType::TargetMesh);
			}

			FViewInfo* LocalView = &View;
			auto RenderProjectionData = [&GraphBuilder, LocalView, WorldType, SkinCache, &bClearDepth, SceneColorTexture, DepthTexture](EHairStrandsInterpolationType StrandType, bool bRestTriangle, bool bRestFrame, bool bDeformedTriangle, bool bDeformedFrame)
			{
				FHairStrandsProjectionHairData HairProjectionDatas;
				TArray<int32> HairLODIndices;
				GetGroomInterpolationData(WorldType, StrandType, SkinCache, HairProjectionDatas, HairLODIndices);
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
					EHairStrandsInterpolationType::RenderStrands, 
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
					EHairStrandsInterpolationType::SimulationStrands,
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

			const FIntPoint PPLLResolution = VisibilityData.PPLLNodeIndexTexture->GetDesc().Extent;
			FHairVisibilityDebugPPLLCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHairVisibilityDebugPPLLCS::FParameters>();
			PassParameters->PPLLMeanListElementCountPerPixel = float(VisibilityData.MaxSampleCount);
			PassParameters->PPLLMaxTotalListElementCount = float(VisibilityData.MaxSampleCount * PPLLResolution.X * PPLLResolution.Y);
			PassParameters->PPLLCounter = PPLLNodeCounterTexture;
			PassParameters->PPLLNodeIndex = PPLLNodeIndexTexture;
			PassParameters->PPLLNodeData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(PPLLNodeDataBuffer));
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->SceneColorTextureUAV = GraphBuilder.CreateUAV(SceneColorTexture);
			ShaderPrint::SetParameters(View, PassParameters->ShaderPrintParameters);

			FHairVisibilityDebugPPLLCS::FPermutationDomain PermutationVector;
			TShaderMapRef<FHairVisibilityDebugPPLLCS> ComputeShader(View.ShaderMap, PermutationVector);
			FIntVector TextureSize = SceneColorTexture->Desc.GetSize(); TextureSize.Z = 1;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairPPLLDebug"), ComputeShader, PassParameters,
				FIntVector::DivideAndRoundUp(TextureSize, FIntVector(8, 8, 1)));
			GraphBuilder.Execute();
		}
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled() && GHairStrandsClusterDebug > 0)
	{
		AddDrawDebugClusterPass(RHICmdList, HairClusterData, Views);
	}

	// Text
	if (HairDebugMode == EHairDebugMode::LightBounds || HairDebugMode == EHairDebugMode::DeepOpacityMaps)
	{
		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneTargets.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.FeatureLevel);

		const uint32 AtlasTotalSlotCount = FDeepShadowResources::MaxAtlasSlotCount;
		uint32 AtlasAllocatedSlot = 0;
		FIntPoint AtlasSlotResolution = FIntPoint(0, 0);
		FIntPoint AtlasResolution = FIntPoint(0, 0);
		bool bIsGPUDriven = false;
		if (ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			const FDeepShadowResources& Resources = InMacroGroupViews.Views[ViewIndex].DeepShadowResources;
			AtlasResolution = Resources.DepthAtlasTexture ? Resources.DepthAtlasTexture->GetRenderTargetItem().TargetableTexture->GetTexture2D()->GetSizeXY() : FIntPoint(0,0);
			AtlasAllocatedSlot = Resources.TotalAtlasSlotCount;
			bIsGPUDriven = Resources.bIsGPUDriven;
		}

		const uint32 DomTextureIndex = GDeepShadowDebugIndex;

		float X = 20;
		float Y = 38;

		FLinearColor DebugColor(1, 1, 0);
		FString Line;

		const FHairComponent HairComponent = GetHairComponents();
		Line = FString::Printf(TEXT("Hair Components : (R=%d, TT=%d, TRT=%d, GS=%d, LS=%d)"), HairComponent.R, HairComponent.TT, HairComponent.TRT, HairComponent.GlobalScattering, HairComponent.LocalScattering);
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("----------------------------------------------------------------"));						Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("Debug strands mode : %s"), ToString(StrandsDebugMode));									Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("Voxelization : %s"), IsHairStrandsVoxelizationEnable() ? TEXT("On") : TEXT("Off"));		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("View rect optim.: %s"), IsHairStrandsViewRectOptimEnable() ? TEXT("On") : TEXT("Off"));	Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("----------------------------------------------------------------"));						Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM Atlas resolution  : %dx%d"), AtlasResolution.X, AtlasResolution.Y);					Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM Atlas slot        : %d/%d"), AtlasAllocatedSlot, AtlasTotalSlotCount);					Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM Texture Index     : %d/%d"), DomTextureIndex, AtlasAllocatedSlot);						Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
		Line = FString::Printf(TEXT("DOM GPU driven        : %s"), bIsGPUDriven ? TEXT("On") : TEXT("Off"));					Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);

		if (ViewIndex < uint32(InMacroGroupViews.Views.Num()))
		{
			for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupViews.Views[ViewIndex].Datas)
			{
				for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas.Datas)
				{
					Line = FString::Printf(TEXT(" %d - Bound Radus: %f.2m (%dx%d)"), DomData.AtlasSlotIndex, DomData.Bounds.GetSphere().W / 10.f, DomData.ShadowResolution.X, DomData.ShadowResolution.Y);
					Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), DebugColor);
				}
			}
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
