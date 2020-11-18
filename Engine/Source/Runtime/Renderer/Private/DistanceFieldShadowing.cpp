// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldShadowing.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightSceneInfo.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "ShadowRendering.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "DistanceFieldLightingShared.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"

int32 GDistanceFieldShadowing = 1;
FAutoConsoleVariableRef CVarDistanceFieldShadowing(
	TEXT("r.DistanceFieldShadowing"),
	GDistanceFieldShadowing,
	TEXT("Whether the distance field shadowing feature is allowed."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GDFShadowQuality = 3;
FAutoConsoleVariableRef CVarDFShadowQuality(
	TEXT("r.DFShadowQuality"),
	GDFShadowQuality,
	TEXT("Defines the distance field shadow method which allows to adjust for quality or performance.\n")
	TEXT(" 0:off, 1:low (20 steps, no SSS), 2:medium (32 steps, no SSS), 3:high (64 steps, SSS, default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GFullResolutionDFShadowing = 0;
FAutoConsoleVariableRef CVarFullResolutionDFShadowing(
	TEXT("r.DFFullResolution"),
	GFullResolutionDFShadowing,
	TEXT("1 = full resolution distance field shadowing, 0 = half resolution with bilateral upsample."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GShadowScatterTileCulling = 1;
FAutoConsoleVariableRef CVarShadowScatterTileCulling(
	TEXT("r.DFShadowScatterTileCulling"),
	GShadowScatterTileCulling,
	TEXT("Whether to use the rasterizer to scatter objects onto the tile grid for culling."),
	ECVF_RenderThreadSafe
	);

float GShadowCullTileWorldSize = 200.0f;
FAutoConsoleVariableRef CVarShadowCullTileWorldSize(
	TEXT("r.DFShadowCullTileWorldSize"),
	GShadowCullTileWorldSize,
	TEXT("World space size of a tile used for culling for directional lights."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GTwoSidedMeshDistanceBias = 4;
FAutoConsoleVariableRef CVarTwoSidedMeshDistanceBias(
	TEXT("r.DFTwoSidedMeshDistanceBias"),
	GTwoSidedMeshDistanceBias,
	TEXT("World space amount to expand distance field representations of two sided meshes.  This is useful to get tree shadows to match up with standard shadow mapping."),
	ECVF_RenderThreadSafe
	);

int32 GAverageObjectsPerShadowCullTile = 128;
FAutoConsoleVariableRef CVarAverageObjectsPerShadowCullTile(
	TEXT("r.DFShadowAverageObjectsPerCullTile"),
	GAverageObjectsPerShadowCullTile,
	TEXT("Determines how much memory should be allocated in distance field object culling data structures.  Too much = memory waste, too little = flickering due to buffer overflow."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
	);

static int32 GHeightFieldShadowing = 0;
FAutoConsoleVariableRef CVarHeightFieldShadowing(
	TEXT("r.HeightFieldShadowing"),
	GHeightFieldShadowing,
	TEXT("Whether the height field shadowing feature is allowed."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

int32 GHFShadowQuality = 2;
FAutoConsoleVariableRef CVarHFShadowQuality(
	TEXT("r.HFShadowQuality"),
	GHFShadowQuality,
	TEXT("Defines the height field shadow method which allows to adjust for quality or performance.\n")
	TEXT(" 0:off, 1:low (8 steps), 2:medium (16 steps, default), 3:high (32 steps, hole aware)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static float GMinDirectionalLightAngleForRTHF = 27.f;
static FAutoConsoleVariableRef CVarMinDirectionalLightAngleForRTHF(
	TEXT("r.Shadow.MinDirectionalLightAngleForRTHF"),
	GMinDirectionalLightAngleForRTHF,
	TEXT(""),
	ECVF_RenderThreadSafe);

int32 GAverageHeightFieldObjectsPerShadowCullTile = 16;
FAutoConsoleVariableRef CVarAverageHeightFieldObjectsPerShadowCullTile(
	TEXT("r.HFShadowAverageObjectsPerCullTile"),
	GAverageHeightFieldObjectsPerShadowCullTile,
	TEXT("Determines how much memory should be allocated in height field object culling data structures.  Too much = memory waste, too little = flickering due to buffer overflow."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

int32 const GDistanceFieldShadowTileSizeX = 8;
int32 const GDistanceFieldShadowTileSizeY = 8;

int32 GetDFShadowDownsampleFactor()
{
	return GFullResolutionDFShadowing ? 1 : GAODownsampleFactor;
}

FIntPoint GetBufferSizeForDFShadows()
{
	return FIntPoint::DivideAndRoundDown(FSceneRenderTargets::Get().GetBufferSizeXY(), GetDFShadowDownsampleFactor());
}

TGlobalResource<FDistanceFieldObjectBufferResource> GShadowCulledObjectBuffers;
TGlobalResource<FHeightFieldObjectBufferResource> GShadowCulledHeightFieldObjectBuffers;

class FCullObjectsForShadowCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullObjectsForShadowCS);
	SHADER_USE_PARAMETER_STRUCT(FCullObjectsForShadowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, ObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, CulledObjectBufferParameters)
		SHADER_PARAMETER(uint32, ObjectBoundingGeometryIndexCount)
		SHADER_PARAMETER(FMatrix, WorldToShadow)
		SHADER_PARAMETER(uint32, NumShadowHullPlanes)
		SHADER_PARAMETER(FVector4, ShadowBoundingSphere)
		SHADER_PARAMETER_ARRAY(FVector4,ShadowConvexHull,[12])
	END_SHADER_PARAMETER_STRUCT()

	class FPrimitiveType : SHADER_PERMUTATION_INT("DISTANCEFIELD_PRIMITIVE_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FPrimitiveType>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullObjectsForShadowCS, "/Engine/Private/DistanceFieldShadowing.usf", "CullObjectsForShadowCS", SF_Compute);

/**  */
class FShadowObjectCullVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FShadowObjectCullVS);
	SHADER_USE_PARAMETER_STRUCT(FShadowObjectCullVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, ObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, CulledObjectBufferParameters)
		SHADER_PARAMETER(FMatrix, WorldToShadow)
		SHADER_PARAMETER(float, MinExpandRadius)
	END_SHADER_PARAMETER_STRUCT()

	class FPrimitiveType : SHADER_PERMUTATION_INT("DISTANCEFIELD_PRIMITIVE_TYPE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FPrimitiveType>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform); 
	}
};

IMPLEMENT_GLOBAL_SHADER(FShadowObjectCullVS, "/Engine/Private/DistanceFieldShadowing.usf", "ShadowObjectCullVS", SF_Vertex);

class FShadowObjectCullPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FShadowObjectCullPS);
	SHADER_USE_PARAMETER_STRUCT(FShadowObjectCullPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, ObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, CulledObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightTileIntersectionParameters, LightTileIntersectionParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FPrimitiveType : SHADER_PERMUTATION_INT("DISTANCEFIELD_PRIMITIVE_TYPE", 2);
	class FCountingPass : SHADER_PERMUTATION_BOOL("SCATTER_CULLING_COUNT_PASS");
	using FPermutationDomain = TShaderPermutationDomain<FPrimitiveType, FCountingPass>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform) && RHISupportsPixelShaderUAVs(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FShadowMeshSDFObjectCull, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FShadowObjectCullVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FShadowObjectCullPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, MeshSDFIndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER(FShadowObjectCullPS, "/Engine/Private/DistanceFieldShadowing.usf", "ShadowObjectCullPS", SF_Pixel);

enum EDistanceFieldShadowingType
{
	DFS_DirectionalLightScatterTileCulling,
	DFS_DirectionalLightTiledCulling,
	DFS_PointLightTiledCulling
};

//template<EDistanceFieldShadowingType ShadowingType, uint32 DFShadowQuality, EDistanceFieldPrimitiveType PrimitiveType, bool bHasPrevOutput>
class FDistanceFieldShadowingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDistanceFieldShadowingCS);
	SHADER_USE_PARAMETER_STRUCT(FDistanceFieldShadowingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, RWShadowFactors)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER(FVector2D, NumGroups)
		SHADER_PARAMETER(FVector, LightDirection)
		SHADER_PARAMETER(FVector4, LightPositionAndInvRadius)
		SHADER_PARAMETER(float, LightSourceRadius)
		SHADER_PARAMETER(float, RayStartOffsetDepthScale)
		SHADER_PARAMETER(FVector, TanLightAngleAndNormalThreshold)
		SHADER_PARAMETER(FIntRect, ScissorRectMinAndSize)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, ObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldCulledObjectBufferParameters, CulledObjectBufferParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightTileIntersectionParameters, LightTileIntersectionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlasParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FHeightFieldAtlasParameters, HeightFieldAtlasParameters)
		SHADER_PARAMETER(FMatrix, WorldToShadow)
		SHADER_PARAMETER(float, TwoSidedMeshDistanceBias)
		SHADER_PARAMETER(float, MinDepth)
		SHADER_PARAMETER(float, MaxDepth)
		SHADER_PARAMETER(uint32, DownsampleFactor)
		SHADER_PARAMETER(FVector2D, InvOutputBufferSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowFactorsTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowFactorsSampler)
	END_SHADER_PARAMETER_STRUCT()

	class FCullingType : SHADER_PERMUTATION_INT("CULLING_TYPE", 3);
	class FShadowQuality : SHADER_PERMUTATION_INT("DF_SHADOW_QUALITY", 3);
	class FPrimitiveType : SHADER_PERMUTATION_INT("DISTANCEFIELD_PRIMITIVE_TYPE", 2);
	class FHasPreviousOutput : SHADER_PERMUTATION_BOOL("HAS_PREVIOUS_OUTPUT");
	using FPermutationDomain = TShaderPermutationDomain<FCullingType, FShadowQuality, FPrimitiveType, FHasPreviousOutput>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldShadowTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldShadowTileSizeY);
		OutEnvironment.SetDefine(TEXT("PLATFORM_SUPPORTS_TYPED_UAV_LOAD"), (int32)RHISupports4ComponentUAVReadWrite(Parameters.Platform));
	}
};

IMPLEMENT_GLOBAL_SHADER(FDistanceFieldShadowingCS, "/Engine/Private/DistanceFieldShadowing.usf", "DistanceFieldShadowingCS", SF_Compute);

class FDistanceFieldShadowingUpsamplePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDistanceFieldShadowingUpsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FDistanceFieldShadowingUpsamplePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowFactorsTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowFactorsSampler)
		SHADER_PARAMETER(FIntRect, ScissorRectMinAndSize)
		SHADER_PARAMETER(float, FadePlaneOffset)
		SHADER_PARAMETER(float, InvFadePlaneLength)
		SHADER_PARAMETER(float, NearFadePlaneOffset)
		SHADER_PARAMETER(float, InvNearFadePlaneLength)
	END_SHADER_PARAMETER_STRUCT()

	class FUpsample : SHADER_PERMUTATION_BOOL("UPSAMPLE_REQUIRED");
	using FPermutationDomain = TShaderPermutationDomain<FUpsample>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDistanceFieldShadowingUpsamplePS, "/Engine/Private/DistanceFieldShadowing.usf", "DistanceFieldShadowingUpsamplePS", SF_Pixel);

const uint32 ComputeCulledObjectStartOffsetGroupSize = 8;

/**  */
class FComputeCulledObjectStartOffsetCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeCulledObjectStartOffsetCS);
	SHADER_USE_PARAMETER_STRUCT(FComputeCulledObjectStartOffsetCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightTileIntersectionParameters, LightTileIntersectionParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_START_OFFSET_GROUP_SIZE"), ComputeCulledObjectStartOffsetGroupSize);
	}
};

IMPLEMENT_SHADER_TYPE(,FComputeCulledObjectStartOffsetCS,TEXT("/Engine/Private/DistanceFieldShadowing.usf"),TEXT("ComputeCulledTilesStartOffsetCS"),SF_Compute);

void ScatterObjectsToShadowTiles(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const FMatrix& WorldToShadowValue, 
	float ShadowBoundingRadius,
	bool bCountingPass, 
	EDistanceFieldPrimitiveType PrimitiveType,
	FIntPoint LightTileDimensions, 
	FRDGBufferRef ObjectIndirectArguments,
	const FDistanceFieldObjectBufferParameters& ObjectBufferParameters,
	const FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	const FLightTileIntersectionParameters& LightTileIntersectionParameters)
{
	{
		FShadowMeshSDFObjectCull* PassParameters = GraphBuilder.AllocParameters<FShadowMeshSDFObjectCull>();

		if (GRHIRequiresRenderTargetForPixelShaderUAVs)
		{
			FRDGTextureDesc DummyDesc = FRDGTextureDesc::Create2D(LightTileDimensions, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable);
			PassParameters->RenderTargets[0] = FRenderTargetBinding(GraphBuilder.CreateTexture(DummyDesc, TEXT("Dummy")), ERenderTargetLoadAction::ENoAction);
		}

		const float MinExpandRadiusValue = (PrimitiveType == DFPT_HeightField ? 0.87f : 1.414f) * ShadowBoundingRadius / FMath::Min(LightTileDimensions.X, LightTileDimensions.Y);

		PassParameters->VS.ObjectBufferParameters = ObjectBufferParameters;
		PassParameters->VS.CulledObjectBufferParameters = CulledObjectBufferParameters;
		PassParameters->VS.WorldToShadow = WorldToShadowValue;
		PassParameters->VS.MinExpandRadius = MinExpandRadiusValue;
		PassParameters->PS.ObjectBufferParameters = ObjectBufferParameters;
		PassParameters->PS.CulledObjectBufferParameters = CulledObjectBufferParameters;
		PassParameters->PS.LightTileIntersectionParameters = LightTileIntersectionParameters;

		PassParameters->MeshSDFIndirectArgs = ObjectIndirectArguments;

		FShadowObjectCullVS::FPermutationDomain VSPermutationVector;
		VSPermutationVector.Set< FShadowObjectCullVS::FPrimitiveType >(PrimitiveType);
		auto VertexShader = View.ShaderMap->GetShader< FShadowObjectCullVS >(VSPermutationVector);

		FShadowObjectCullPS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FShadowObjectCullPS::FPrimitiveType >(PrimitiveType);
		PermutationVector.Set< FShadowObjectCullPS::FCountingPass >(bCountingPass);
		auto PixelShader = View.ShaderMap->GetShader< FShadowObjectCullPS >(PermutationVector);

		const bool bReverseCulling = View.bReverseCulling;

		ClearUnusedGraphResources(VertexShader, &PassParameters->VS);
		ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ScatterMeshSDFsToLightGrid %ux%u", LightTileDimensions.X, LightTileDimensions.Y),
			PassParameters,
			ERDGPassFlags::Raster,
			[LightTileDimensions, bReverseCulling, VertexShader, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			RHICmdList.SetViewport(0, 0, 0.0f, LightTileDimensions.X, LightTileDimensions.Y, 1.0f);

			// Render backfaces since camera may intersect
			GraphicsPSOInit.RasterizerState = bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

			RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

			RHICmdList.DrawIndexedPrimitiveIndirect(
				GetUnitCubeIndexBuffer(),
				PassParameters->MeshSDFIndirectArgs->GetIndirectRHICallBuffer(),
				0);
		});
	}
}

void AllocateDistanceFieldCulledObjectBuffers(
	FRDGBuilder& GraphBuilder, 
	bool bWantBoxBounds, 
	uint32 MaxObjects, 
	EDistanceFieldPrimitiveType PrimitiveType,
	FRDGBufferRef& OutObjectIndirectArguments,
	FDistanceFieldCulledObjectBufferParameters& OutParameters)
{
	check(MaxObjects > 0);
	OutObjectIndirectArguments = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndexedIndirectParameters>(), TEXT("FDistanceFieldCulledObjectBuffers_ObjectIndirectArguments"));

	uint32 NumBoundsElementsScale;
	uint32 ObjectDataStride;
	uint32 ObjectBoxBoundsStride;

	if (PrimitiveType == DFPT_SignedDistanceField)
	{
		NumBoundsElementsScale = 1;
		ObjectDataStride = TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField>::ObjectDataStride;
		ObjectBoxBoundsStride = TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField>::ObjectBoxBoundsStride;
	}
	else
	{
		NumBoundsElementsScale = 2;
		ObjectDataStride = TDistanceFieldCulledObjectBuffers<DFPT_HeightField>::ObjectDataStride;
		ObjectBoxBoundsStride = TDistanceFieldCulledObjectBuffers<DFPT_HeightField>::ObjectBoxBoundsStride;
	}

	FRDGBufferRef Bounds = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), MaxObjects * NumBoundsElementsScale), TEXT("FDistanceFieldCulledObjectBuffers_Bounds"));
	FRDGBufferRef Data = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), MaxObjects * ObjectDataStride), TEXT("FDistanceFieldCulledObjectBuffers_Data"));

	OutParameters.RWObjectIndirectArguments = GraphBuilder.CreateUAV(OutObjectIndirectArguments, PF_R32_UINT);
	OutParameters.RWCulledObjectBounds = GraphBuilder.CreateUAV(Bounds);
	OutParameters.RWCulledObjectData = GraphBuilder.CreateUAV(Data);

	OutParameters.ObjectIndirectArguments = GraphBuilder.CreateSRV(OutObjectIndirectArguments, PF_R32_UINT);
	OutParameters.CulledObjectBounds = GraphBuilder.CreateSRV(Bounds);
	OutParameters.CulledObjectData = GraphBuilder.CreateSRV(Data);

	if (bWantBoxBounds)
	{
		FRDGBufferRef BoxBounds = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), MaxObjects * ObjectBoxBoundsStride), TEXT("FDistanceFieldCulledObjectBuffers_BoxBounds"));
		OutParameters.RWCulledObjectBoxBounds = GraphBuilder.CreateUAV(BoxBounds);
		OutParameters.CulledObjectBoxBounds = GraphBuilder.CreateSRV(BoxBounds);
	}
}

void CullDistanceFieldObjectsForLight(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy, 
	EDistanceFieldPrimitiveType PrimitiveType,
	const FMatrix& WorldToShadowValue, 
	int32 NumPlanes, 
	const FPlane* PlaneData, 
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	const FDistanceFieldObjectBufferParameters& ObjectBufferParameters,
	FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	FLightTileIntersectionParameters& LightTileIntersectionParameters)
{
	const bool bIsHeightfield = PrimitiveType == DFPT_HeightField;
	const FScene* Scene = (const FScene*)(View.Family->Scene);
	FRDGBufferRef ObjectIndirectArguments = nullptr;

	RDG_EVENT_SCOPE(GraphBuilder, "CullMeshSDFsForLight");

	const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
	const int32 NumObjectsInBuffer = bIsHeightfield ? SceneData.NumHeightFieldObjectsInBuffer : SceneData.NumObjectsInBuffer;

	AllocateDistanceFieldCulledObjectBuffers(
		GraphBuilder, 
		true, 
		FMath::DivideAndRoundUp(NumObjectsInBuffer, 256) * 256, 
		PrimitiveType,
		ObjectIndirectArguments,
		CulledObjectBufferParameters);

	AddClearUAVPass(GraphBuilder, CulledObjectBufferParameters.RWObjectIndirectArguments, 0);

	{
		FCullObjectsForShadowCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullObjectsForShadowCS::FParameters>();
			
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ObjectBufferParameters = ObjectBufferParameters;
		PassParameters->CulledObjectBufferParameters = CulledObjectBufferParameters;
		PassParameters->ObjectBoundingGeometryIndexCount = UE_ARRAY_COUNT(GCubeIndices);
		PassParameters->WorldToShadow = WorldToShadowValue;
		PassParameters->NumShadowHullPlanes = NumPlanes;
		PassParameters->ShadowBoundingSphere = ShadowBoundingSphereValue;

		check(NumPlanes <= 12);

		for (int32 i = 0; i < NumPlanes; i++)
		{
			PassParameters->ShadowConvexHull[i] = FVector4(PlaneData[i], PlaneData[i].W);
		}

		FCullObjectsForShadowCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FCullObjectsForShadowCS::FPrimitiveType >(PrimitiveType);
		auto ComputeShader = View.ShaderMap->GetShader<FCullObjectsForShadowCS>(PermutationVector);
		const int32 GroupSize = FMath::DivideAndRoundUp<uint32>(NumObjectsInBuffer, UpdateObjectsGroupSize);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CullMeshSDFObjectsToFrustum"),
			ComputeShader,
			PassParameters,
			FIntVector(GroupSize, 1, 1));
	}

	// Allocate tile resolution based on world space size
	const float LightTiles = FMath::Min(ShadowBoundingRadius / GShadowCullTileWorldSize + 1.0f, 256.0f);
	FIntPoint LightTileDimensions(Align(FMath::TruncToInt(LightTiles), 64), Align(FMath::TruncToInt(LightTiles), 64));

	if (LightSceneProxy->GetLightType() == LightType_Directional && GShadowScatterTileCulling)
	{
		const bool b16BitObjectIndices = Scene->DistanceFieldSceneData.CanUse16BitObjectIndices();

		FRDGBufferRef ShadowTileNumCulledObjects = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), LightTileDimensions.X * LightTileDimensions.Y), TEXT("ShadowTileNumCulledObjects"));
		LightTileIntersectionParameters.RWShadowTileNumCulledObjects = GraphBuilder.CreateUAV(ShadowTileNumCulledObjects, PF_R32_UINT);
		LightTileIntersectionParameters.ShadowTileNumCulledObjects = GraphBuilder.CreateSRV(ShadowTileNumCulledObjects, PF_R32_UINT);

		FRDGBufferRef ShadowTileStartOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), LightTileDimensions.X * LightTileDimensions.Y), TEXT("ShadowTileStartOffsets"));
		LightTileIntersectionParameters.RWShadowTileStartOffsets = GraphBuilder.CreateUAV(ShadowTileStartOffsets, PF_R32_UINT);
		LightTileIntersectionParameters.ShadowTileStartOffsets = GraphBuilder.CreateSRV(ShadowTileStartOffsets, PF_R32_UINT);

		FRDGBufferRef NextStartOffset = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("ShadowNextStartOffset"));
		LightTileIntersectionParameters.RWNextStartOffset = GraphBuilder.CreateUAV(NextStartOffset, PF_R32_UINT);
		LightTileIntersectionParameters.NextStartOffset = GraphBuilder.CreateSRV(NextStartOffset, PF_R32_UINT);

		const uint32 MaxNumObjectsPerTile = bIsHeightfield ? GAverageHeightFieldObjectsPerShadowCullTile : GAverageObjectsPerShadowCullTile;
		FRDGBufferRef ShadowTileArrayData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(b16BitObjectIndices ? sizeof(uint16) : sizeof(uint32), MaxNumObjectsPerTile * LightTileDimensions.X * LightTileDimensions.Y), TEXT("ShadowTileArrayData"));
		LightTileIntersectionParameters.RWShadowTileArrayData = GraphBuilder.CreateUAV(ShadowTileArrayData, b16BitObjectIndices ? PF_R16_UINT : PF_R32_UINT);
		LightTileIntersectionParameters.ShadowTileArrayData = GraphBuilder.CreateSRV(ShadowTileArrayData, b16BitObjectIndices ? PF_R16_UINT : PF_R32_UINT);
		LightTileIntersectionParameters.ShadowTileListGroupSize = LightTileDimensions;

		// Start at 0 tiles per object
		AddClearUAVPass(GraphBuilder, LightTileIntersectionParameters.RWShadowTileNumCulledObjects, 0);

		// Rasterize object bounding shapes and intersect with shadow tiles to compute how many objects intersect each tile
		ScatterObjectsToShadowTiles(GraphBuilder, View, WorldToShadowValue, ShadowBoundingRadius, true, PrimitiveType, LightTileDimensions, ObjectIndirectArguments, ObjectBufferParameters, CulledObjectBufferParameters, LightTileIntersectionParameters);

		AddClearUAVPass(GraphBuilder, LightTileIntersectionParameters.RWNextStartOffset, 0);

		// Compute the start offset for each tile's culled object data
		{
			FComputeCulledObjectStartOffsetCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeCulledObjectStartOffsetCS::FParameters>();
			
			PassParameters->LightTileIntersectionParameters = LightTileIntersectionParameters;
			auto ComputeShader = View.ShaderMap->GetShader<FComputeCulledObjectStartOffsetCS>();
			uint32 GroupSizeX = FMath::DivideAndRoundUp<int32>(LightTileDimensions.X, ComputeCulledObjectStartOffsetGroupSize);
			uint32 GroupSizeY = FMath::DivideAndRoundUp<int32>(LightTileDimensions.Y, ComputeCulledObjectStartOffsetGroupSize);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ComputeCulledObjectStartOffset"),
				ComputeShader,
				PassParameters,
				FIntVector(GroupSizeX, GroupSizeY, 1));
		}

		// Start at 0 tiles per object
		AddClearUAVPass(GraphBuilder, LightTileIntersectionParameters.RWShadowTileNumCulledObjects, 0);

		// Rasterize object bounding shapes and intersect with shadow tiles, and write out intersecting tile indices for the cone tracing pass
		ScatterObjectsToShadowTiles(GraphBuilder, View, WorldToShadowValue, ShadowBoundingRadius, false, PrimitiveType, LightTileDimensions, ObjectIndirectArguments, ObjectBufferParameters, CulledObjectBufferParameters, LightTileIntersectionParameters);
	}
}

int32 GetDFShadowQuality()
{
	return FMath::Clamp(GDFShadowQuality, 0, 3);
}

int32 GetHFShadowQuality()
{
	return FMath::Clamp(GHFShadowQuality, 0, 3);
}

bool SupportsDistanceFieldShadows(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GDistanceFieldShadowing 
		&& GetDFShadowQuality() > 0
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& DoesPlatformSupportDistanceFieldShadowing(ShaderPlatform);
}

bool SupportsHeightFieldShadows(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GHeightFieldShadowing
		&& GetHFShadowQuality() > 0
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& DoesPlatformSupportDistanceFieldShadowing(ShaderPlatform);
}

bool FDeferredShadingSceneRenderer::ShouldPrepareForDistanceFieldShadows() const 
{
	bool bSceneHasRayTracedDFShadows = false;

	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		if (LightSceneInfo->ShouldRenderLightViewIndependent())
		{
			const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

			for (int32 ShadowIndex = 0; ShadowIndex < VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex++)
			{
				const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo.AllProjectedShadows[ShadowIndex];

				if (ProjectedShadowInfo->bRayTracedDistanceField)
				{
					bSceneHasRayTracedDFShadows = true;
					break;
				}
			}
		}
	}

	return ViewFamily.EngineShowFlags.DynamicShadows 
		&& bSceneHasRayTracedDFShadows
		&& SupportsDistanceFieldShadows(Scene->GetFeatureLevel(), Scene->GetShaderPlatform());
}

bool FDeferredShadingSceneRenderer::ShouldPrepareHeightFieldScene() const
{
	return Scene
		&& ViewFamily.EngineShowFlags.DynamicShadows
		&& SupportsHeightFieldShadows(Scene->GetFeatureLevel(), Scene->GetShaderPlatform());
}

void RayTraceShadows(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef RayTracedShadowsTexture,
	const FViewInfo& View,
	const FProjectedShadowInfo* ProjectedShadowInfo,
	EDistanceFieldPrimitiveType PrimitiveType,
	bool bHasPrevOutput,
	FRDGTextureRef PrevOutputTexture,
	const FDistanceFieldObjectBufferParameters& ObjectBufferParameters,
	const FDistanceFieldCulledObjectBufferParameters& CulledObjectBufferParameters,
	const FLightTileIntersectionParameters& LightTileIntersectionParameters)
{
	FIntRect ScissorRect;
	if (!ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetScissorRect(ScissorRect, View, View.ViewRect))
	{
		ScissorRect = View.ViewRect;
	}

	const int32 DFShadowQuality = (PrimitiveType == DFPT_HeightField ? GetHFShadowQuality() : GetDFShadowQuality()) - 1;
	check(DFShadowQuality >= 0);

	EDistanceFieldShadowingType DistanceFieldShadowingType;

	if (ProjectedShadowInfo->bDirectionalLight && GShadowScatterTileCulling)
	{
		DistanceFieldShadowingType = DFS_DirectionalLightScatterTileCulling;
	}
	else if (ProjectedShadowInfo->bDirectionalLight)
	{
		DistanceFieldShadowingType = DFS_DirectionalLightTiledCulling;
	}
	else
	{
		DistanceFieldShadowingType = DFS_PointLightTiledCulling;
	}

	check(DistanceFieldShadowingType != DFS_PointLightTiledCulling || PrimitiveType != DFPT_HeightField);

	FDistanceFieldAtlasParameters DistanceFieldAtlasParameters;
	DistanceFieldAtlasParameters.DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
	DistanceFieldAtlasParameters.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
	const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
	const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
	const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
	DistanceFieldAtlasParameters.DistanceFieldAtlasTexelSize = FVector(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);

	FHeightFieldAtlasParameters HeightFieldAtlasParameters;
	HeightFieldAtlasParameters.HeightFieldTexture = GHeightFieldTextureAtlas.GetAtlasTexture();
	HeightFieldAtlasParameters.HFVisibilityTexture = GHFVisibilityTextureAtlas.GetAtlasTexture();
	HeightFieldAtlasParameters.HeightFieldAtlasTexelSize = FVector2D(1.0f / GHeightFieldTextureAtlas.GetSizeX(), 1.0f / GHeightFieldTextureAtlas.GetSizeY());

	{
		FDistanceFieldShadowingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDistanceFieldShadowingCS::FParameters>();
			
		PassParameters->RWShadowFactors = GraphBuilder.CreateUAV(RayTracedShadowsTexture);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTexturesUniformBuffer;

		const FLightSceneProxy& LightProxy = *(ProjectedShadowInfo->GetLightSceneInfo().Proxy);
		FLightShaderParameters LightParameters;
		LightProxy.GetLightShaderParameters(LightParameters);

		PassParameters->LightDirection = LightParameters.Direction;
		PassParameters->LightPositionAndInvRadius = FVector4(LightParameters.Position, LightParameters.InvRadius);
		// Default light source radius of 0 gives poor results
		PassParameters->LightSourceRadius = LightParameters.SourceRadius == 0 ? 20 : FMath::Clamp(LightParameters.SourceRadius, .001f, 1.0f / (4 * LightParameters.InvRadius));
		PassParameters->RayStartOffsetDepthScale = LightProxy.GetRayStartOffsetDepthScale();

		const bool bHeightfield = PrimitiveType == DFPT_HeightField;
		const float MaxLightAngle = bHeightfield ? 45.0f : 5.0f;
		const float MinLightAngle = bHeightfield ? FMath::Min(GMinDirectionalLightAngleForRTHF, MaxLightAngle) : 0.001f;
		const float LightSourceAngle = FMath::Clamp(LightProxy.GetLightSourceAngle(), MinLightAngle, MaxLightAngle) * PI / 180.0f;
		PassParameters->TanLightAngleAndNormalThreshold = FVector(FMath::Tan(LightSourceAngle), FMath::Cos(PI / 2 + LightSourceAngle), LightProxy.GetTraceDistance());
		PassParameters->ScissorRectMinAndSize = FIntRect(ScissorRect.Min, ScissorRect.Size());
		PassParameters->ObjectBufferParameters = ObjectBufferParameters;
		PassParameters->CulledObjectBufferParameters = CulledObjectBufferParameters;
		PassParameters->LightTileIntersectionParameters = LightTileIntersectionParameters;
		PassParameters->DistanceFieldAtlasParameters = DistanceFieldAtlasParameters;
		PassParameters->HeightFieldAtlasParameters = HeightFieldAtlasParameters;
		PassParameters->WorldToShadow = FTranslationMatrix(ProjectedShadowInfo->PreShadowTranslation) * ProjectedShadowInfo->TranslatedWorldToClipInnerMatrix;
		PassParameters->TwoSidedMeshDistanceBias = GTwoSidedMeshDistanceBias;

		if (ProjectedShadowInfo->bDirectionalLight)
		{
			PassParameters->MinDepth = ProjectedShadowInfo->CascadeSettings.SplitNear - ProjectedShadowInfo->CascadeSettings.SplitNearFadeRegion;
			PassParameters->MaxDepth = ProjectedShadowInfo->CascadeSettings.SplitFar;
		}
		else
		{
			check(!bHeightfield);
			//@todo - set these up for point lights as well
			PassParameters->MinDepth = 0.0f;
			PassParameters->MaxDepth = HALF_WORLD_MAX;
		}

		PassParameters->DownsampleFactor = GetDFShadowDownsampleFactor();
		const FIntPoint OutputBufferSize = GetBufferSizeForDFShadows();
		PassParameters->InvOutputBufferSize = FVector2D(1.f / OutputBufferSize.X, 1.f / OutputBufferSize.Y);
		PassParameters->ShadowFactorsTexture = PrevOutputTexture;
		PassParameters->ShadowFactorsSampler = TStaticSamplerState<>::GetRHI();
		
		FDistanceFieldShadowingCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FDistanceFieldShadowingCS::FCullingType >((uint32)DistanceFieldShadowingType);
		PermutationVector.Set< FDistanceFieldShadowingCS::FShadowQuality >(DFShadowQuality);
		PermutationVector.Set< FDistanceFieldShadowingCS::FPrimitiveType >(PrimitiveType);
		PermutationVector.Set< FDistanceFieldShadowingCS::FHasPreviousOutput >(bHasPrevOutput);
		auto ComputeShader = View.ShaderMap->GetShader< FDistanceFieldShadowingCS >(PermutationVector);

		uint32 GroupSizeX = FMath::DivideAndRoundUp(ScissorRect.Size().X / GetDFShadowDownsampleFactor(), GDistanceFieldShadowTileSizeX);
		uint32 GroupSizeY = FMath::DivideAndRoundUp(ScissorRect.Size().Y / GetDFShadowDownsampleFactor(), GDistanceFieldShadowTileSizeY);
		PassParameters->NumGroups = FVector2D(GroupSizeX, GroupSizeY);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DistanceFieldShadowing %ux%u", GroupSizeX * GDistanceFieldShadowTileSizeX, GroupSizeY * GDistanceFieldShadowTileSizeY),
			ComputeShader,
			PassParameters,
			FIntVector(GroupSizeX, GroupSizeY, 1));
	}
}

FRDGTextureRef FProjectedShadowInfo::BeginRenderRayTracedDistanceFieldProjection(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FViewInfo& View) const
{
	const bool bDFShadowSupported = SupportsDistanceFieldShadows(View.GetFeatureLevel(), View.GetShaderPlatform());
	const bool bHFShadowSupported = SupportsHeightFieldShadows(View.GetFeatureLevel(), View.GetShaderPlatform());
	const FScene* Scene = (const FScene*)(View.Family->Scene);

	FRDGTextureRef RayTracedShadowsTexture = nullptr;

	if (bDFShadowSupported && View.Family->EngineShowFlags.RayTracedDistanceFieldShadows)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BeginRenderRayTracedDistanceFieldShadows);
		RDG_EVENT_SCOPE(GraphBuilder, "BeginRayTracedDistanceFieldShadow");

		if (GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI && Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0)
		{
			check(!Scene->DistanceFieldSceneData.HasPendingOperations());

			int32 NumPlanes = 0;
			const FPlane* PlaneData = NULL;
			FVector4 ShadowBoundingSphereValue(0, 0, 0, 0);

			if (bDirectionalLight)
			{
				NumPlanes = CascadeSettings.ShadowBoundsAccurate.Planes.Num();
				PlaneData = CascadeSettings.ShadowBoundsAccurate.Planes.GetData();
			}
			else if (bOnePassPointLightShadow)
			{
				ShadowBoundingSphereValue = FVector4(ShadowBounds.Center.X, ShadowBounds.Center.Y, ShadowBounds.Center.Z, ShadowBounds.W);
			}
			else
			{
				NumPlanes = CasterOuterFrustum.Planes.Num();
				PlaneData = CasterOuterFrustum.Planes.GetData();
				ShadowBoundingSphereValue = FVector4(PreShadowTranslation, 0);
			}

			const FMatrix WorldToShadowValue = FTranslationMatrix(PreShadowTranslation) * TranslatedWorldToClipInnerMatrix;

			FDistanceFieldObjectBufferParameters ObjectBufferParameters;
			ObjectBufferParameters.SceneObjectBounds = Scene->DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
			ObjectBufferParameters.SceneObjectData = Scene->DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
			ObjectBufferParameters.NumSceneObjects = Scene->DistanceFieldSceneData.NumObjectsInBuffer;

			FLightTileIntersectionParameters LightTileIntersectionParameters;
			FDistanceFieldCulledObjectBufferParameters CulledObjectBufferParameters;

			CullDistanceFieldObjectsForLight(
				GraphBuilder,
				View,
				LightSceneInfo->Proxy,
				DFPT_SignedDistanceField,
				WorldToShadowValue,
				NumPlanes,
				PlaneData,
				ShadowBoundingSphereValue,
				ShadowBounds.W,
				ObjectBufferParameters,
				CulledObjectBufferParameters,
				LightTileIntersectionParameters
				);

			{
				const FIntPoint BufferSize = GetBufferSizeForDFShadows();
				FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(BufferSize, PF_G16R16F, FClearValueBinding::None, TexCreate_UAV));
				Desc.Flags |= GFastVRamConfig.DistanceFieldShadows;
				RayTracedShadowsTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracedShadows"));
			}

			RayTraceShadows(GraphBuilder, SceneTexturesUniformBuffer, RayTracedShadowsTexture, View, this, DFPT_SignedDistanceField, false, nullptr, ObjectBufferParameters, CulledObjectBufferParameters, LightTileIntersectionParameters);
		}
	}

	if (bDirectionalLight
		&& View.Family->EngineShowFlags.RayTracedDistanceFieldShadows
		&& GHeightFieldTextureAtlas.GetAtlasTexture()
		&& Scene->DistanceFieldSceneData.NumHeightFieldObjectsInBuffer > 0
		&& bHFShadowSupported)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BeginRenderRayTracedHeightFieldShadows);
		RDG_EVENT_SCOPE(GraphBuilder, "BeginRenderRayTracedHeightFieldShadows");

		check(!Scene->DistanceFieldSceneData.HasPendingHeightFieldOperations());

		const int32 NumPlanes = CascadeSettings.ShadowBoundsAccurate.Planes.Num();
		const FPlane* PlaneData = CascadeSettings.ShadowBoundsAccurate.Planes.GetData();
		const FVector4 ShadowBoundingSphereValue(0.f, 0.f, 0.f, 0.f);
		const FMatrix WorldToShadowValue = FTranslationMatrix(PreShadowTranslation) * TranslatedWorldToClipInnerMatrix;

		FDistanceFieldObjectBufferParameters ObjectBufferParameters;
		ObjectBufferParameters.SceneObjectBounds = Scene->DistanceFieldSceneData.GetHeightFieldObjectBuffers()->Bounds.SRV;
		ObjectBufferParameters.SceneObjectData = Scene->DistanceFieldSceneData.GetHeightFieldObjectBuffers()->Data.SRV;
		ObjectBufferParameters.NumSceneObjects = Scene->DistanceFieldSceneData.NumHeightFieldObjectsInBuffer;

		FLightTileIntersectionParameters LightTileIntersectionParameters;
		FDistanceFieldCulledObjectBufferParameters CulledObjectBufferParameters;

		CullDistanceFieldObjectsForLight(
			GraphBuilder,
			View,
			LightSceneInfo->Proxy,
			DFPT_HeightField,
			WorldToShadowValue,
			NumPlanes,
			PlaneData,
			ShadowBoundingSphereValue,
			ShadowBounds.W,
			ObjectBufferParameters,
			CulledObjectBufferParameters,
			LightTileIntersectionParameters
			);

		const bool bHasPrevOutput = !!RayTracedShadowsTexture;

		FRDGTextureRef PrevOutputTexture = nullptr;

		if (!RHISupports4ComponentUAVReadWrite(View.GetShaderPlatform()))
		{
			PrevOutputTexture = RayTracedShadowsTexture;
			RayTracedShadowsTexture = nullptr;
		}

		if (!RayTracedShadowsTexture)
		{
			const FIntPoint BufferSize = GetBufferSizeForDFShadows();
			FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(BufferSize, PF_G16R16F, FClearValueBinding::None, TexCreate_UAV));
			Desc.Flags |= GFastVRamConfig.DistanceFieldShadows;
			RayTracedShadowsTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracedShadows"));
		}

		RayTraceShadows(GraphBuilder, SceneTexturesUniformBuffer, RayTracedShadowsTexture, View, this, DFPT_HeightField, bHasPrevOutput, PrevOutputTexture, ObjectBufferParameters, CulledObjectBufferParameters, LightTileIntersectionParameters);
	}

	return RayTracedShadowsTexture;
}

BEGIN_SHADER_PARAMETER_STRUCT(FDistanceFieldShadowingUpsample, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldShadowingUpsamplePS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FProjectedShadowInfo::RenderRayTracedDistanceFieldProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef ScreenShadowMaskTexture,
	const FViewInfo& View,
	FIntRect ScissorRect,
	bool bProjectingForForwardShading) const
{
	check(ScissorRect.Area() > 0);

	FRDGTextureRef RayTracedShadowsTexture = BeginRenderRayTracedDistanceFieldProjection(GraphBuilder, SceneTextures.UniformBuffer, View);

	if (RayTracedShadowsTexture)
	{
		FDistanceFieldShadowingUpsample* PassParameters = GraphBuilder.AllocParameters<FDistanceFieldShadowingUpsample>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ScreenShadowMaskTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneTextures.Depth.Target, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilRead);
		
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.SceneTextures = SceneTextures.UniformBuffer;
		PassParameters->PS.ShadowFactorsTexture = RayTracedShadowsTexture;
		PassParameters->PS.ShadowFactorsSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		PassParameters->PS.ScissorRectMinAndSize = FIntRect(ScissorRect.Min, ScissorRect.Size());

		if (bDirectionalLight && CascadeSettings.FadePlaneLength > 0)
		{
			PassParameters->PS.FadePlaneOffset = CascadeSettings.FadePlaneOffset;
			PassParameters->PS.InvFadePlaneLength = 1.0f / FMath::Max(CascadeSettings.FadePlaneLength, .00001f);
		}
		else
		{
			PassParameters->PS.FadePlaneOffset = 0.0f;
			PassParameters->PS.InvFadePlaneLength = 0.0f;
		}

		if (bDirectionalLight && CascadeSettings.SplitNearFadeRegion > 0)
		{
			PassParameters->PS.NearFadePlaneOffset = CascadeSettings.SplitNear - CascadeSettings.SplitNearFadeRegion;
			PassParameters->PS.InvNearFadePlaneLength = 1.0f / FMath::Max(CascadeSettings.SplitNearFadeRegion, .00001f);
		}
		else
		{
			PassParameters->PS.NearFadePlaneOffset = -1.0f;
			PassParameters->PS.InvNearFadePlaneLength = 1.0f;
		}

		FDistanceFieldShadowingUpsamplePS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FDistanceFieldShadowingUpsamplePS::FUpsample >(GFullResolutionDFShadowing == 0);
		auto PixelShader = View.ShaderMap->GetShader< FDistanceFieldShadowingUpsamplePS >(PermutationVector);

		const bool bReverseCulling = View.bReverseCulling;

		ClearUnusedGraphResources(PixelShader, &PassParameters->PS);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Upsample"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &View, PixelShader, ScissorRect, bProjectingForForwardShading, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			RHICmdList.SetViewport(ScissorRect.Min.X, ScissorRect.Min.Y, 0.0f, ScissorRect.Max.X, ScissorRect.Max.Y, 1.0f);
			RHICmdList.SetScissorRect(true, ScissorRect.Min.X, ScissorRect.Min.Y, ScissorRect.Max.X, ScissorRect.Max.Y);

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BlendState = GetBlendStateForProjection(bProjectingForForwardShading, false);

			TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.bDepthBounds = bDirectionalLight;
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

			//@todo - depth bounds test for local lights
			if (bDirectionalLight)
			{
				SetDepthBoundsTest(RHICmdList, CascadeSettings.SplitNear - CascadeSettings.SplitNearFadeRegion, CascadeSettings.SplitFar, View.ViewMatrices.GetProjectionMatrix());
			}

			DrawRectangle(
				RHICmdList,
				0, 0,
				ScissorRect.Width(), ScissorRect.Height(),
				ScissorRect.Min.X / GetDFShadowDownsampleFactor(), ScissorRect.Min.Y / GetDFShadowDownsampleFactor(),
				ScissorRect.Width() / GetDFShadowDownsampleFactor(), ScissorRect.Height() / GetDFShadowDownsampleFactor(),
				FIntPoint(ScissorRect.Width(), ScissorRect.Height()),
				GetBufferSizeForDFShadows(),
				VertexShader);

			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		});
	}
}
