// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

static int32 GDeepShadowDebugIndex = 0;
static float GDeepShadowDebugScale = 20;

static FAutoConsoleVariableRef CVarDeepShadowDebugDomIndex(TEXT("r.HairStrands.DeepShadow.DebugDOMIndex"), GDeepShadowDebugIndex, TEXT("Index of the DOM texture to draw"));
static FAutoConsoleVariableRef CVarDeepShadowDebugDomScale(TEXT("r.HairStrands.DeepShadow.DebugDOMScale"), GDeepShadowDebugScale, TEXT("Scaling value for the DeepOpacityMap when drawing the deep shadow stats"));

static int32 GHairStrandsDebugMode = 0;
static FAutoConsoleVariableRef CVarDeepShadowStats(TEXT("r.HairStrands.DebugMode"), GHairStrandsDebugMode, TEXT("Draw various stats/debug mode about hair rendering"));

static int32 GHairStrandsDebugStrandsMode = 0;
static FAutoConsoleVariableRef CVarDebugPhysicsStrand(TEXT("r.HairStrands.StrandsMode"), GHairStrandsDebugStrandsMode, TEXT("Render debug mode for hair strands. 0:off, 1:simulation strands, 2:render strands with colored simulation strands influence, 3:hair UV, 4:hair root UV, 5: hair seed, 6: dimensions"));

static int32 GHairDebugMeshProjection_SkinCacheMesh = 0;
static int32 GHairDebugMeshProjection_HairRestTriangles = 0;
static int32 GHairDebugMeshProjection_HairRestFrames = 0;
static int32 GHairDebugMeshProjection_HairDeformedTriangles = 0;
static int32 GHairDebugMeshProjection_HairDeformedFrames = 0;
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_SkinCacheMesh(TEXT("r.HairStrands.MeshProjection.DebugSkinCache"),						GHairDebugMeshProjection_SkinCacheMesh, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_HairRestTriangles(TEXT("r.HairStrands.MeshProjection.DebugHairRestTriangles"),			GHairDebugMeshProjection_HairRestTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_HairRestFrames(TEXT("r.HairStrands.MeshProjection.DebugHairRestFrames"),					GHairDebugMeshProjection_HairRestFrames, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_HairDeformedTriangles(TEXT("r.HairStrands.MeshProjection.DebugHairDeformedTriangles"),	GHairDebugMeshProjection_HairDeformedTriangles, TEXT("Render debug mes projection"));
static FAutoConsoleVariableRef CVarHairDebugMeshProjection_HairDeformedFrames(TEXT("r.HairStrands.MeshProjection.DebugHairDeformedFrames"),			GHairDebugMeshProjection_HairDeformedFrames, TEXT("Render debug mes projection"));


// Helper functions for accessing interpolation data for debug purpose.
// Definitions is in HairStrandsInterface.cpp
void GetGroomInterpolationData(const EWorldType::Type WorldType, FHairStrandsProjectionMeshData& OutGeometries);
void GetGroomInterpolationData(const EWorldType::Type WorldType, FHairStrandsProjectionHairData& OutHairData, TArray<int32>& OutLODIndices);

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
	MeshProjection
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
	case 10:  return EHairDebugMode::VoxelsBaseColor;
	case 11: return EHairDebugMode::VoxelsRoughness;
	case 12: return EHairDebugMode::MeshProjection;
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
	case EHairDebugMode::CoverageType: return TEXT("Type of hair coverage (full/partial)");
	case EHairDebugMode::VoxelsDensity: return TEXT("Hair density volume");
	case EHairDebugMode::VoxelsTangent: return TEXT("Hair tangent volume");
	case EHairDebugMode::VoxelsBaseColor: return TEXT("Hair base color volume");
	case EHairDebugMode::VoxelsRoughness: return TEXT("Hair roughness volume");
	case EHairDebugMode::MeshProjection: return TEXT("Hair mesh projection");
	default: return TEXT("None");
	};
}


EHairStrandsDebugMode GetHairStrandsDebugStrandsMode()
{
	switch (GHairStrandsDebugStrandsMode)
	{
	case 0:  return EHairStrandsDebugMode::None;
	case 1:  return EHairStrandsDebugMode::SimHairStrands;
	case 2:  return EHairStrandsDebugMode::RenderHairStrands;
	case 3:  return EHairStrandsDebugMode::RenderHairRootUV;
	case 4:  return EHairStrandsDebugMode::RenderHairUV;
	case 5:  return EHairStrandsDebugMode::RenderHairSeed;
	case 6:  return EHairStrandsDebugMode::RenderHairDimension;
	case 7:  return EHairStrandsDebugMode::RenderHairRadiusVariation;
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
	case EHairStrandsDebugMode::RenderHairUV				: return TEXT("Hair UV");
	case EHairStrandsDebugMode::RenderHairSeed				: return TEXT("Hair seed");
	case EHairStrandsDebugMode::RenderHairDimension			: return TEXT("Hair dimensions");
	case EHairStrandsDebugMode::RenderHairRadiusVariation	: return TEXT("Hair radius variation");
	default													: return TEXT("None");
	};
}


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairDebugPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FHairDebugPS, FGlobalShader);

	class FDebugMode : SHADER_PERMUTATION_INT("PERMUTATION_DEBUG_MODE", 3);
	using FPermutationDomain = TShaderPermutationDomain<FDebugMode>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, OutputResolution)
		SHADER_PARAMETER(uint32, FastResolveMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
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
	const FShaderResourceViewRHIRef& InDepthStencilTexture,
	FRDGTextureRef& OutTarget)
{
	check(OutTarget);
	check(InDebugMode == EHairDebugMode::TAAResolveType || InDebugMode == EHairDebugMode::SamplePerPixel || InDebugMode == EHairDebugMode::CoverageType);

	if (!InCategorizationTexture) return;
	if (InDebugMode == EHairDebugMode::TAAResolveType && !InDepthStencilTexture) return;

	FRDGTextureRef CategorizationTexture = InCategorizationTexture ? GraphBuilder.RegisterExternalTexture(InCategorizationTexture, TEXT("CategorizationTexture")) : nullptr;

	const FIntRect Viewport = View->ViewRect;
	const FIntPoint Resolution(Viewport.Width(), Viewport.Height());

	FHairDebugPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairDebugPS::FParameters>();
	Parameters->OutputResolution = Resolution;
	Parameters->FastResolveMask = STENCIL_TEMPORAL_RESPONSIVE_AA_MASK;
	Parameters->CategorizationTexture = CategorizationTexture;
	Parameters->DepthStencilTexture = InDepthStencilTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTarget, ERenderTargetLoadAction::ELoad, 0);
	TShaderMapRef<FPostProcessVS> VertexShader(View->ShaderMap);

	FHairDebugPS::FPermutationDomain PermutationVector;
	uint32 DebugPermutation = 0;
	switch (InDebugMode)
	{
		case EHairDebugMode::SamplePerPixel:	DebugPermutation = 0; break;
		case EHairDebugMode::CoverageType:		DebugPermutation = 1; break;
		case EHairDebugMode::TAAResolveType:	DebugPermutation = 2; break;
	};
	PermutationVector.Set<FHairDebugPS::FDebugMode>(DebugPermutation);
	TShaderMapRef<FHairDebugPS> PixelShader(View->ShaderMap, PermutationVector);

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
	FRDGTextureRef& ColorTarget,
	FRDGTextureRef& DepthTexture)
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

	Parameters->RestPosition0Buffer = HairData.LODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
	Parameters->RestPosition1Buffer = HairData.LODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
	Parameters->RestPosition2Buffer = HairData.LODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;
		
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

void RenderHairStrandsDebugInfo(FRHICommandListImmediate& RHICmdList, TArray<FViewInfo>& Views, const FHairStrandsDatas* HairDatas)
{
	const float YStep = 14;
	const float ColumnWidth = 200;

	if (Views.Num() == 0 || !HairDatas)
		return;

	const FHairStrandsDeepShadowViews& InDomViews = HairDatas->DeepShadowViews;
	const FHairStrandsClusterViews& InClusterViews = HairDatas->HairClusterPerViews;

	// Only render debug information for the main view
	const uint32 ViewIndex = 0;
	FViewInfo& View = Views[ViewIndex];
	const FSceneViewFamily& ViewFamily = *(View.Family);
	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);

	// Debug mode name only
	const EHairStrandsDebugMode StrandsDebugMode = GetHairStrandsDebugStrandsMode();
	const EHairDebugMode HairDebugMode = GetHairDebugMode();

	if (HairDebugMode == EHairDebugMode::ClusterData)
	{
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
		float Y = 38;
		FLinearColor InactiveColor(0.5, 0.5, 0.5);
		FLinearColor DebugColor(1, 1, 0);
		FString Line;

		//const FVector4 HairComponent = GetHairComponents();
		//const uint32 bHairR = HairComponent.X > 0 ? 1 : 0;
		//const uint32 bHairTT = HairComponent.Y > 0 ? 1 : 0;
		//const uint32 bHairTRT = HairComponent.Z > 0 ? 1 : 0;
		//const uint32 bHairGlobalScattering = FMath::FloorToInt(HairComponent.W / 10.0f) > 0 ? 1 : 0;
		//const uint32 bHairLocalScattering = FMath::Frac(HairComponent.W / 10.0f)*10.f > 0 ? 1 : 0;

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

	if (HairDebugMode == EHairDebugMode::TAAResolveType || HairDebugMode == EHairDebugMode::SamplePerPixel || HairDebugMode == EHairDebugMode::CoverageType)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
		if (ViewIndex < uint32(HairDatas->HairVisibilityViews.HairDatas.Num()))
		{
			const FHairStrandsVisibilityData& VisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
			AddDebugHairPass(GraphBuilder, &View, HairDebugMode, VisibilityData.CategorizationTexture, SceneTargets.SceneStencilSRV, SceneColorTexture);
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
			FHairStrandsProjectionMeshData MeshProjectionData;
			GetGroomInterpolationData(WorldType, MeshProjectionData);

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
				for (FHairStrandsProjectionMeshData::Section& Section : MeshProjectionData.Sections)
				{
					AddDebugProjectionMeshPass(GraphBuilder, &View, bClearDepth, Section, SceneColorTexture, DepthTexture);
					bClearDepth = false;
				}
			}

			FHairStrandsProjectionHairData HairProjectionDatas;
			TArray<int32> HairLODIndices;
			GetGroomInterpolationData(WorldType, HairProjectionDatas, HairLODIndices);
			check(HairProjectionDatas.HairGroups.Num() == HairLODIndices.Num());
			for (int32 HairIndex=0; HairIndex < HairProjectionDatas.HairGroups.Num(); ++HairIndex)
			{
				const FHairStrandsProjectionHairData::HairGroup& Data = HairProjectionDatas.HairGroups[HairIndex];
				const int32 LODIndex = HairLODIndices[HairIndex];

				if (GHairDebugMeshProjection_HairRestTriangles > 0)
				{
					AddDebugProjectionHairPass(GraphBuilder, &View, bClearDepth, EDebugProjectionHairType::HairTriangle, HairStrandsTriangleType::RestPose, LODIndex, Data, SceneColorTexture, DepthTexture);
					bClearDepth = false;
				}
				if (GHairDebugMeshProjection_HairRestFrames > 0)
				{
					AddDebugProjectionHairPass(GraphBuilder, &View, bClearDepth, EDebugProjectionHairType::HairFrame, HairStrandsTriangleType::RestPose, LODIndex, Data, SceneColorTexture, DepthTexture);
					bClearDepth = false;
				}
				if (GHairDebugMeshProjection_HairDeformedTriangles > 0)
				{
					AddDebugProjectionHairPass(GraphBuilder, &View, bClearDepth, EDebugProjectionHairType::HairTriangle, HairStrandsTriangleType::DeformedPose, LODIndex, Data, SceneColorTexture, DepthTexture);
					bClearDepth = false;
				}
				if (GHairDebugMeshProjection_HairDeformedFrames > 0)
				{
					AddDebugProjectionHairPass(GraphBuilder, &View, bClearDepth, EDebugProjectionHairType::HairFrame, HairStrandsTriangleType::DeformedPose, LODIndex, Data, SceneColorTexture, DepthTexture);
					bClearDepth = false;
				}
			}
		}
		GraphBuilder.Execute();
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

		const FVector4 HairComponent = GetHairComponents();
		const uint32 bHairR = HairComponent.X > 0 ? 1 : 0;
		const uint32 bHairTT = HairComponent.Y > 0 ? 1 : 0;
		const uint32 bHairTRT = HairComponent.Z > 0 ? 1 : 0;
		const uint32 bHairGlobalScattering = FMath::FloorToInt(HairComponent.W / 10.0f) > 0 ? 1 : 0;
		const uint32 bHairLocalScattering  = FMath::Frac(HairComponent.W / 10.0f)*10.f > 0 ? 1 : 0;

		Line = FString::Printf(TEXT("Hair Components : (R=%d, TT=%d, TRT=%d, GS=%d, LS=%d)"), bHairR, bHairTT, bHairTRT, bHairGlobalScattering, bHairLocalScattering);
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
