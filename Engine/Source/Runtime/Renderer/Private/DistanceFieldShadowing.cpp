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

static int32 GHFShadowQuality = 3;
FAutoConsoleVariableRef CVarHFShadowQuality(
	TEXT("r.HFShadowQuality"),
	GHFShadowQuality,
	TEXT("Defines the height field shadow method which allows to adjust for quality or performance.\n")
	TEXT(" 0:off, 1:low (8 steps), 2:medium (16 steps), 3:high (32 steps, default)"),
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
	return FIntPoint::DivideAndRoundDown(FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(), GetDFShadowDownsampleFactor());
}

TGlobalResource<FDistanceFieldObjectBufferResource> GShadowCulledObjectBuffers;
TGlobalResource<FHeightFieldObjectBufferResource> GShadowCulledHeightFieldObjectBuffers;

template <EDistanceFieldPrimitiveType PrimitiveType>
class TCullObjectsForShadowCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TCullObjectsForShadowCS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UPDATEOBJECTS_THREADGROUP_SIZE"), UpdateObjectsGroupSize);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
	}

	TCullObjectsForShadowCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ObjectBufferParameters.Bind(Initializer.ParameterMap);
		CulledObjectParameters.Bind(Initializer.ParameterMap);
		ObjectBoundingGeometryIndexCount.Bind(Initializer.ParameterMap, TEXT("ObjectBoundingGeometryIndexCount"));
		WorldToShadow.Bind(Initializer.ParameterMap, TEXT("WorldToShadow"));
		NumShadowHullPlanes.Bind(Initializer.ParameterMap, TEXT("NumShadowHullPlanes"));
		ShadowBoundingSphere.Bind(Initializer.ParameterMap, TEXT("ShadowBoundingSphere"));
		ShadowConvexHull.Bind(Initializer.ParameterMap, TEXT("ShadowConvexHull"));
	}

	TCullObjectsForShadowCS()
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FScene* Scene,
		const FSceneView& View,
		const FMatrix& WorldToShadowValue,
		int32 NumPlanes,
		const FPlane* PlaneData,
		const FVector4& ShadowBoundingSphereValue)
	{
		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const auto& SceneObjectBuffers = TSelector<bIsHeightField>()(*SceneData.GetHeightFieldObjectBuffers(), *SceneData.GetCurrentObjectBuffers());
		auto& CulledObjectBuffers = TSelector<bIsHeightField>()(GShadowCulledHeightFieldObjectBuffers, GShadowCulledObjectBuffers);
		
		int32 NumObjectsInBuffer;
		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (bIsHeightField)
		{
			NumObjectsInBuffer = SceneData.NumHeightFieldObjectsInBuffer;
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			NumObjectsInBuffer = SceneData.NumObjectsInBuffer;
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		FRHIComputeShader* ShaderRHI = GetComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		ObjectBufferParameters.Set(RHICmdList, ShaderRHI, SceneObjectBuffers, NumObjectsInBuffer, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ);

		FRHIUnorderedAccessView* OutUAVs[4];
		OutUAVs[0] = CulledObjectBuffers.Buffers.ObjectIndirectArguments.UAV;
		OutUAVs[1] = CulledObjectBuffers.Buffers.Bounds.UAV;
		OutUAVs[2] = CulledObjectBuffers.Buffers.Data.UAV;
		OutUAVs[3] = CulledObjectBuffers.Buffers.BoxBounds.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));

		CulledObjectParameters.Set(RHICmdList, ShaderRHI, CulledObjectBuffers.Buffers, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ);

		SetShaderValue(RHICmdList, ShaderRHI, ObjectBoundingGeometryIndexCount, UE_ARRAY_COUNT(GCubeIndices));
		SetShaderValue(RHICmdList, ShaderRHI, WorldToShadow, WorldToShadowValue);
		SetShaderValue(RHICmdList, ShaderRHI, ShadowBoundingSphere, ShadowBoundingSphereValue);

		if (NumPlanes <= 12)
		{
			SetShaderValue(RHICmdList, ShaderRHI, NumShadowHullPlanes, NumPlanes);
			SetShaderValueArray(RHICmdList, ShaderRHI, ShadowConvexHull, PlaneData, NumPlanes);
		}
		else
		{
			SetShaderValue(RHICmdList, ShaderRHI, NumShadowHullPlanes, 0);
		}
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FScene* Scene)
	{
		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const auto& SceneObjectBuffers = TSelector<bIsHeightField>()(*SceneData.GetHeightFieldObjectBuffers(), *SceneData.GetCurrentObjectBuffers());
		auto& CulledObjectBuffers = TSelector<bIsHeightField>()(GShadowCulledHeightFieldObjectBuffers, GShadowCulledObjectBuffers);

		ObjectBufferParameters.UnsetParameters(RHICmdList, GetComputeShader(), SceneObjectBuffers);
		CulledObjectParameters.UnsetParameters(RHICmdList, GetComputeShader());

		FRHIUnorderedAccessView* OutUAVs[4];
		OutUAVs[0] = CulledObjectBuffers.Buffers.ObjectIndirectArguments.UAV;
		OutUAVs[1] = CulledObjectBuffers.Buffers.Bounds.UAV;
		OutUAVs[2] = CulledObjectBuffers.Buffers.Data.UAV;
		OutUAVs[3] = CulledObjectBuffers.Buffers.BoxBounds.UAV;
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, OutUAVs, UE_ARRAY_COUNT(OutUAVs));
	}

	virtual bool Serialize(FArchive& Ar) override
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ObjectBufferParameters;
		Ar << CulledObjectParameters;
		Ar << ObjectBoundingGeometryIndexCount;
		Ar << WorldToShadow;
		Ar << NumShadowHullPlanes;
		Ar << ShadowBoundingSphere;
		Ar << ShadowConvexHull;
		return bShaderHasOutdatedParameters;
	}

private:

	TDistanceFieldObjectBufferParameters<PrimitiveType> ObjectBufferParameters;
	TDistanceFieldCulledObjectBufferParameters<PrimitiveType> CulledObjectParameters;
	FShaderParameter ObjectBoundingGeometryIndexCount;
	FShaderParameter WorldToShadow;
	FShaderParameter NumShadowHullPlanes;
	FShaderParameter ShadowBoundingSphere;
	FShaderParameter ShadowConvexHull;
};

IMPLEMENT_SHADER_TYPE(template<>, TCullObjectsForShadowCS<DFPT_SignedDistanceField>, TEXT("/Engine/Private/DistanceFieldShadowing.usf"), TEXT("CullObjectsForShadowCS"), SF_Compute);
IMPLEMENT_SHADER_TYPE(template<>, TCullObjectsForShadowCS<DFPT_HeightField>, TEXT("/Engine/Private/DistanceFieldShadowing.usf"), TEXT("CullObjectsForShadowCS"), SF_Compute);

/**  */
template <EDistanceFieldPrimitiveType PrimitiveType>
class TShadowObjectCullVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TShadowObjectCullVS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform); 
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
	}

	TShadowObjectCullVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		ObjectParameters.Bind(Initializer.ParameterMap);
		WorldToShadow.Bind(Initializer.ParameterMap, TEXT("WorldToShadow"));
		MinExpandRadius.Bind(Initializer.ParameterMap, TEXT("MinExpandRadius"));
	}

	TShadowObjectCullVS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, FVector2D NumGroupsValue, const FMatrix& WorldToShadowMatrixValue, float ShadowRadius)
	{
		FRHIVertexShader* ShaderRHI = GetVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		
		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		auto& CulledObjectBuffers = TSelector<bIsHeightField>()(GShadowCulledHeightFieldObjectBuffers, GShadowCulledObjectBuffers);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (bIsHeightField)
		{
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		ObjectParameters.Set(RHICmdList, ShaderRHI, CulledObjectBuffers.Buffers, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ);

		SetShaderValue(RHICmdList, ShaderRHI, WorldToShadow, WorldToShadowMatrixValue);

		const float MinExpandRadiusValue = (bIsHeightField ? 0.87f : 1.414f) * ShadowRadius / FMath::Min(NumGroupsValue.X, NumGroupsValue.Y);
		SetShaderValue(RHICmdList, ShaderRHI, MinExpandRadius, MinExpandRadiusValue);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ObjectParameters;
		Ar << WorldToShadow;
		Ar << MinExpandRadius;
		return bShaderHasOutdatedParameters;
	}

private:
	TDistanceFieldCulledObjectBufferParameters<PrimitiveType> ObjectParameters;
	FShaderParameter WorldToShadow;
	FShaderParameter MinExpandRadius;
};

IMPLEMENT_SHADER_TYPE(template<>, TShadowObjectCullVS<DFPT_SignedDistanceField>, TEXT("/Engine/Private/DistanceFieldShadowing.usf"), TEXT("ShadowObjectCullVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>, TShadowObjectCullVS<DFPT_HeightField>, TEXT("/Engine/Private/DistanceFieldShadowing.usf"), TEXT("ShadowObjectCullVS"), SF_Vertex);

template <bool bCountingPass, EDistanceFieldPrimitiveType PrimitiveType>
class TShadowObjectCullPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TShadowObjectCullPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform) && RHISupportsPixelShaderUAVs(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		FLightTileIntersectionParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SCATTER_CULLING_COUNT_PASS"), bCountingPass ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
	}

	/** Default constructor. */
	TShadowObjectCullPS() {}

	/** Initialization constructor. */
	TShadowObjectCullPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ObjectParameters.Bind(Initializer.ParameterMap);
		LightTileIntersectionParameters.Bind(Initializer.ParameterMap);
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		FLightTileIntersectionResources* TileIntersectionResources)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		auto& CulledObjectBuffers = TSelector<bIsHeightField>()(GShadowCulledHeightFieldObjectBuffers, GShadowCulledObjectBuffers);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (bIsHeightField)
		{
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		ObjectParameters.Set(RHICmdList, ShaderRHI, CulledObjectBuffers.Buffers, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ);

		LightTileIntersectionParameters.Set(RHICmdList, ShaderRHI, *TileIntersectionResources);
	}

	void GetUAVs(const FSceneView& View, FLightTileIntersectionResources* TileIntersectionResources, TArray<FRHIUnorderedAccessView*>& UAVs)
	{
		LightTileIntersectionParameters.GetUAVs(*TileIntersectionResources, UAVs);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ObjectParameters;
		Ar << LightTileIntersectionParameters;
		return bShaderHasOutdatedParameters;
	}

private:
	TDistanceFieldCulledObjectBufferParameters<PrimitiveType> ObjectParameters;
	FLightTileIntersectionParameters LightTileIntersectionParameters;
};

#define VARIATION(PrimitiveType) \
	typedef TShadowObjectCullPS<true, PrimitiveType> FShadowObjectCullPS##1##PrimitiveType; \
	typedef TShadowObjectCullPS<false, PrimitiveType> FShadowObjectCullPS##0##PrimitiveType; \
	IMPLEMENT_SHADER_TYPE(template<>, FShadowObjectCullPS##1##PrimitiveType, TEXT("/Engine/Private/DistanceFieldShadowing.usf"), TEXT("ShadowObjectCullPS"), SF_Pixel); \
	IMPLEMENT_SHADER_TYPE(template<>, FShadowObjectCullPS##0##PrimitiveType, TEXT("/Engine/Private/DistanceFieldShadowing.usf"), TEXT("ShadowObjectCullPS"), SF_Pixel)

VARIATION(DFPT_SignedDistanceField);
VARIATION(DFPT_HeightField);

#undef VARIATION

enum EDistanceFieldShadowingType
{
	DFS_DirectionalLightScatterTileCulling,
	DFS_DirectionalLightTiledCulling,
	DFS_PointLightTiledCulling
};

template<EDistanceFieldShadowingType ShadowingType, uint32 DFShadowQuality, EDistanceFieldPrimitiveType PrimitiveType, bool bHasPrevOutput>
class TDistanceFieldShadowingCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDistanceFieldShadowingCS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLightTileIntersectionParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDistanceFieldShadowTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDistanceFieldShadowTileSizeY);
		OutEnvironment.SetDefine(TEXT("SCATTER_TILE_CULLING"), ShadowingType == DFS_DirectionalLightScatterTileCulling);
		OutEnvironment.SetDefine(TEXT("POINT_LIGHT"), ShadowingType == DFS_PointLightTiledCulling);
		OutEnvironment.SetDefine(TEXT("DF_SHADOW_QUALITY"), DFShadowQuality);
		OutEnvironment.SetDefine(TEXT("DISTANCEFIELD_PRIMITIVE_TYPE"), (int32)PrimitiveType);
		OutEnvironment.SetDefine(TEXT("HAS_PREVIOUS_OUTPUT"), (int32)bHasPrevOutput);
		OutEnvironment.SetDefine(TEXT("PLATFORM_SUPPORTS_TYPED_UAV_LOAD"), (int32)RHISupports4ComponentUAVReadWrite(Parameters.Platform));
	}

	/** Default constructor. */
	TDistanceFieldShadowingCS() {}

	/** Initialization constructor. */
	TDistanceFieldShadowingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ShadowFactors.Bind(Initializer.ParameterMap, TEXT("ShadowFactors"));
		NumGroups.Bind(Initializer.ParameterMap, TEXT("NumGroups"));
		LightDirection.Bind(Initializer.ParameterMap, TEXT("LightDirection"));
		LightSourceRadius.Bind(Initializer.ParameterMap, TEXT("LightSourceRadius"));
		RayStartOffsetDepthScale.Bind(Initializer.ParameterMap, TEXT("RayStartOffsetDepthScale"));
		LightPositionAndInvRadius.Bind(Initializer.ParameterMap, TEXT("LightPositionAndInvRadius"));
		TanLightAngleAndNormalThreshold.Bind(Initializer.ParameterMap, TEXT("TanLightAngleAndNormalThreshold"));
		ScissorRectMinAndSize.Bind(Initializer.ParameterMap, TEXT("ScissorRectMinAndSize"));
		ObjectParameters.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		LightTileIntersectionParameters.Bind(Initializer.ParameterMap);
		WorldToShadow.Bind(Initializer.ParameterMap, TEXT("WorldToShadow"));
		TwoSidedMeshDistanceBias.Bind(Initializer.ParameterMap, TEXT("TwoSidedMeshDistanceBias"));
		MinDepth.Bind(Initializer.ParameterMap, TEXT("MinDepth"));
		MaxDepth.Bind(Initializer.ParameterMap, TEXT("MaxDepth"));
		DownsampleFactor.Bind(Initializer.ParameterMap, TEXT("DownsampleFactor"));
		InvOutputBufferSize.Bind(Initializer.ParameterMap, TEXT("InvOutputBufferSize"));
		ShadowFactorsTexture.Bind(Initializer.ParameterMap, TEXT("ShadowFactorsTexture"));
		ShadowFactorsSampler.Bind(Initializer.ParameterMap, TEXT("ShadowFactorsSampler"));
	}

	template<typename TRHICommandList>
	void SetParameters(
		TRHICommandList& RHICmdList, 
		const FSceneView& View, 
		const FProjectedShadowInfo* ProjectedShadowInfo,
		FSceneRenderTargetItem& ShadowFactorsValue, 
		FVector2D NumGroupsValue,
		const FIntRect& ScissorRect,
		FLightTileIntersectionResources* TileIntersectionResources,
		FRHITexture* PrevOutput)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, ShadowFactorsValue.UAV);
		ShadowFactors.SetTexture(RHICmdList, ShaderRHI, ShadowFactorsValue.ShaderResourceTexture, ShadowFactorsValue.UAV);

		constexpr bool bIsHeightField = PrimitiveType == DFPT_HeightField;
		auto& CulledObjectBuffers = TSelector<bIsHeightField>()(GShadowCulledHeightFieldObjectBuffers, GShadowCulledObjectBuffers);

		FRHITexture* TextureAtlas;
		int32 AtlasSizeX;
		int32 AtlasSizeY;
		int32 AtlasSizeZ;

		if (bIsHeightField)
		{
			TextureAtlas = GHeightFieldTextureAtlas.GetAtlasTexture();
			AtlasSizeX = GHeightFieldTextureAtlas.GetSizeX();
			AtlasSizeY = GHeightFieldTextureAtlas.GetSizeY();
			AtlasSizeZ = 1;
		}
		else
		{
			TextureAtlas = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
			AtlasSizeX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			AtlasSizeY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			AtlasSizeZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		}

		ObjectParameters.Set(RHICmdList, ShaderRHI, CulledObjectBuffers.Buffers, TextureAtlas, AtlasSizeX, AtlasSizeY, AtlasSizeZ);

		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		SetShaderValue(RHICmdList, ShaderRHI, NumGroups, NumGroupsValue);

		const FLightSceneProxy& LightProxy = *(ProjectedShadowInfo->GetLightSceneInfo().Proxy);

		FLightShaderParameters LightParameters;
		LightProxy.GetLightShaderParameters(LightParameters);

		SetShaderValue(RHICmdList, ShaderRHI, LightDirection, LightParameters.Direction);
		FVector4 LightPositionAndInvRadiusValue(LightParameters.Position, LightParameters.InvRadius);
		SetShaderValue(RHICmdList, ShaderRHI, LightPositionAndInvRadius, LightPositionAndInvRadiusValue);
		// Default light source radius of 0 gives poor results
		SetShaderValue(RHICmdList, ShaderRHI, LightSourceRadius, LightParameters.SourceRadius == 0 ? 20 : FMath::Clamp(LightParameters.SourceRadius, .001f, 1.0f / (4 * LightParameters.InvRadius)));

		SetShaderValue(RHICmdList, ShaderRHI, RayStartOffsetDepthScale, LightProxy.GetRayStartOffsetDepthScale());

		constexpr float MaxLightAngle = bIsHeightField ? 45.0f : 5.0f;
		const float MinLightAngle = bIsHeightField ? FMath::Min(GMinDirectionalLightAngleForRTHF, MaxLightAngle) : 0.001f;
		const float LightSourceAngle = FMath::Clamp(LightProxy.GetLightSourceAngle(), MinLightAngle, MaxLightAngle) * PI / 180.0f;
		const FVector TanLightAngleAndNormalThresholdValue(FMath::Tan(LightSourceAngle), FMath::Cos(PI / 2 + LightSourceAngle), LightProxy.GetTraceDistance());
		SetShaderValue(RHICmdList, ShaderRHI, TanLightAngleAndNormalThreshold, TanLightAngleAndNormalThresholdValue);

		SetShaderValue(RHICmdList, ShaderRHI, ScissorRectMinAndSize, FIntRect(ScissorRect.Min, ScissorRect.Size()));

		check(TileIntersectionResources || !LightTileIntersectionParameters.IsBound());

		if (TileIntersectionResources)
		{
			LightTileIntersectionParameters.Set(RHICmdList, ShaderRHI, *TileIntersectionResources);
		}

		FMatrix WorldToShadowMatrixValue = FTranslationMatrix(ProjectedShadowInfo->PreShadowTranslation) * ProjectedShadowInfo->SubjectAndReceiverMatrix;
		SetShaderValue(RHICmdList, ShaderRHI, WorldToShadow, WorldToShadowMatrixValue);

		SetShaderValue(RHICmdList, ShaderRHI, TwoSidedMeshDistanceBias, GTwoSidedMeshDistanceBias);

		if (ProjectedShadowInfo->bDirectionalLight)
		{
			SetShaderValue(RHICmdList, ShaderRHI, MinDepth, ProjectedShadowInfo->CascadeSettings.SplitNear - ProjectedShadowInfo->CascadeSettings.SplitNearFadeRegion);
			SetShaderValue(RHICmdList, ShaderRHI, MaxDepth, ProjectedShadowInfo->CascadeSettings.SplitFar);
		}
		else
		{
			check(!bIsHeightField);
			//@todo - set these up for point lights as well
			SetShaderValue(RHICmdList, ShaderRHI, MinDepth, 0.0f);
			SetShaderValue(RHICmdList, ShaderRHI, MaxDepth, HALF_WORLD_MAX);
		}

		SetShaderValue(RHICmdList, ShaderRHI, DownsampleFactor, GetDFShadowDownsampleFactor());

		if (InvOutputBufferSize.IsBound())
		{
			check(ShadowFactorsTexture.IsBound() && ShadowFactorsSampler.IsBound() && PrevOutput);

			const FIntPoint OutputBufferSize = GetBufferSizeForDFShadows();
			SetShaderValue(RHICmdList, ShaderRHI, InvOutputBufferSize, FVector2D(1.f / OutputBufferSize.X, 1.f / OutputBufferSize.Y));
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowFactorsTexture, ShadowFactorsSampler, TStaticSamplerState<>::GetRHI(), PrevOutput);
		}
	}

	template<typename TRHICommandList>
	void UnsetParameters(TRHICommandList& RHICmdList, FSceneRenderTargetItem& ShadowFactorsValue)
	{
		ShadowFactors.UnsetUAV(RHICmdList, GetComputeShader());
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, ShadowFactorsValue.UAV);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << ShadowFactors;
		Ar << NumGroups;
		Ar << LightDirection;
		Ar << LightPositionAndInvRadius;
		Ar << LightSourceRadius;
		Ar << RayStartOffsetDepthScale;
		Ar << TanLightAngleAndNormalThreshold;
		Ar << ScissorRectMinAndSize;
		Ar << ObjectParameters;
		Ar << SceneTextureParameters;
		Ar << LightTileIntersectionParameters;
		Ar << WorldToShadow;
		Ar << TwoSidedMeshDistanceBias;
		Ar << MinDepth;
		Ar << MaxDepth;
		Ar << DownsampleFactor;
		Ar << InvOutputBufferSize;
		Ar << ShadowFactorsTexture;
		Ar << ShadowFactorsSampler;
		return bShaderHasOutdatedParameters;
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/DistanceFieldShadowing.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("DistanceFieldShadowingCS");
	}

private:

	FRWShaderParameter ShadowFactors;
	FShaderParameter NumGroups;
	FShaderParameter LightDirection;
	FShaderParameter LightPositionAndInvRadius;
	FShaderParameter LightSourceRadius;
	FShaderParameter RayStartOffsetDepthScale;
	FShaderParameter TanLightAngleAndNormalThreshold;
	FShaderParameter ScissorRectMinAndSize;
	TDistanceFieldCulledObjectBufferParameters<PrimitiveType> ObjectParameters;
	FSceneTextureShaderParameters SceneTextureParameters;
	FLightTileIntersectionParameters LightTileIntersectionParameters;
	FShaderParameter WorldToShadow;
	FShaderParameter TwoSidedMeshDistanceBias;
	FShaderParameter MinDepth;
	FShaderParameter MaxDepth;
	FShaderParameter DownsampleFactor;
	FShaderParameter InvOutputBufferSize;
	FShaderResourceParameter ShadowFactorsTexture;
	FShaderResourceParameter ShadowFactorsSampler;
};

// #define avoids a lot of code duplication
#define VARIATION(A, B, C) \
	typedef TDistanceFieldShadowingCS<A, 1, B, C> FDistanceFieldShadowingCS##A##1##B##C; \
	typedef TDistanceFieldShadowingCS<A, 2, B, C> FDistanceFieldShadowingCS##A##2##B##C; \
    typedef TDistanceFieldShadowingCS<A, 3, B, C> FDistanceFieldShadowingCS##A##3##B##C; \
	IMPLEMENT_SHADER_TYPE2(FDistanceFieldShadowingCS##A##1##B##C, SF_Compute); \
	IMPLEMENT_SHADER_TYPE2(FDistanceFieldShadowingCS##A##2##B##C, SF_Compute); \
    IMPLEMENT_SHADER_TYPE2(FDistanceFieldShadowingCS##A##3##B##C, SF_Compute)

VARIATION(DFS_DirectionalLightScatterTileCulling, DFPT_SignedDistanceField, false);
VARIATION(DFS_DirectionalLightScatterTileCulling, DFPT_HeightField, false);
VARIATION(DFS_DirectionalLightScatterTileCulling, DFPT_HeightField, true);
VARIATION(DFS_DirectionalLightTiledCulling, DFPT_SignedDistanceField, false);
VARIATION(DFS_DirectionalLightTiledCulling, DFPT_HeightField, false);
VARIATION(DFS_DirectionalLightTiledCulling, DFPT_HeightField, true);
VARIATION(DFS_PointLightTiledCulling, DFPT_SignedDistanceField, false);

#undef VARIATION

template<bool bUpsampleRequired>
class TDistanceFieldShadowingUpsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDistanceFieldShadowingUpsamplePS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
		OutEnvironment.SetDefine(TEXT("UPSAMPLE_REQUIRED"), bUpsampleRequired);
	}

	/** Default constructor. */
	TDistanceFieldShadowingUpsamplePS() {}

	/** Initialization constructor. */
	TDistanceFieldShadowingUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SceneTextureParameters.Bind(Initializer);
		ShadowFactorsTexture.Bind(Initializer.ParameterMap,TEXT("ShadowFactorsTexture"));
		ShadowFactorsSampler.Bind(Initializer.ParameterMap,TEXT("ShadowFactorsSampler"));
		ScissorRectMinAndSize.Bind(Initializer.ParameterMap,TEXT("ScissorRectMinAndSize"));
		FadePlaneOffset.Bind(Initializer.ParameterMap,TEXT("FadePlaneOffset"));
		InvFadePlaneLength.Bind(Initializer.ParameterMap,TEXT("InvFadePlaneLength"));
		NearFadePlaneOffset.Bind(Initializer.ParameterMap,TEXT("NearFadePlaneOffset"));
		InvNearFadePlaneLength.Bind(Initializer.ParameterMap,TEXT("InvNearFadePlaneLength"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo, const FIntRect& ScissorRect, TRefCountPtr<IPooledRenderTarget>& ShadowFactorsTextureValue)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		SetTextureParameter(RHICmdList, ShaderRHI, ShadowFactorsTexture, ShadowFactorsSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), ShadowFactorsTextureValue->GetRenderTargetItem().ShaderResourceTexture);
	
		SetShaderValue(RHICmdList, ShaderRHI, ScissorRectMinAndSize, FIntRect(ScissorRect.Min, ScissorRect.Size()));

		if (ShadowInfo->bDirectionalLight && ShadowInfo->CascadeSettings.FadePlaneLength > 0)
		{
			SetShaderValue(RHICmdList, ShaderRHI, FadePlaneOffset, ShadowInfo->CascadeSettings.FadePlaneOffset);
			SetShaderValue(RHICmdList, ShaderRHI, InvFadePlaneLength, 1.0f / FMath::Max(ShadowInfo->CascadeSettings.FadePlaneLength, .00001f));
		}
		else
		{
			SetShaderValue(RHICmdList, ShaderRHI, FadePlaneOffset, 0.0f);
			SetShaderValue(RHICmdList, ShaderRHI, InvFadePlaneLength, 0.0f);
		}

		if (ShadowInfo->bDirectionalLight && ShadowInfo->CascadeSettings.SplitNearFadeRegion > 0)
		{
			SetShaderValue(RHICmdList, ShaderRHI, NearFadePlaneOffset, ShadowInfo->CascadeSettings.SplitNear - ShadowInfo->CascadeSettings.SplitNearFadeRegion);
			SetShaderValue(RHICmdList, ShaderRHI, InvNearFadePlaneLength, 1.0f / FMath::Max(ShadowInfo->CascadeSettings.SplitNearFadeRegion, .00001f));
		}
		else
		{
			SetShaderValue(RHICmdList, ShaderRHI, NearFadePlaneOffset, -1.0f);
			SetShaderValue(RHICmdList, ShaderRHI, InvNearFadePlaneLength, 1.0f);
		}
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << SceneTextureParameters;
		Ar << ShadowFactorsTexture;
		Ar << ShadowFactorsSampler;
		Ar << ScissorRectMinAndSize;
		Ar << FadePlaneOffset;
		Ar << InvFadePlaneLength;
		Ar << NearFadePlaneOffset;
		Ar << InvNearFadePlaneLength;
		return bShaderHasOutdatedParameters;
	}

private:

	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderResourceParameter ShadowFactorsTexture;
	FShaderResourceParameter ShadowFactorsSampler;
	FShaderParameter ScissorRectMinAndSize;
	FShaderParameter FadePlaneOffset;
	FShaderParameter InvFadePlaneLength;
	FShaderParameter NearFadePlaneOffset;
	FShaderParameter InvNearFadePlaneLength;
};

IMPLEMENT_SHADER_TYPE(template<>,TDistanceFieldShadowingUpsamplePS<true>,TEXT("/Engine/Private/DistanceFieldShadowing.usf"),TEXT("DistanceFieldShadowingUpsamplePS"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,TDistanceFieldShadowingUpsamplePS<false>,TEXT("/Engine/Private/DistanceFieldShadowing.usf"),TEXT("DistanceFieldShadowingUpsamplePS"),SF_Pixel);

const uint32 ComputeCulledObjectStartOffsetGroupSize = 8;

/**  */
class FComputeCulledObjectStartOffsetCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FComputeCulledObjectStartOffsetCS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldShadowing(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_START_OFFSET_GROUP_SIZE"), ComputeCulledObjectStartOffsetGroupSize);
	}

	FComputeCulledObjectStartOffsetCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TileIntersectionParameters.Bind(Initializer.ParameterMap);
	}

	FComputeCulledObjectStartOffsetCS()
	{
	}
	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, FLightTileIntersectionResources* TileIntersectionResources)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		TArray<FRHIUnorderedAccessView*> UAVs;
		TileIntersectionParameters.GetUAVs(*TileIntersectionResources, UAVs);

		RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAVs.GetData(), UAVs.Num());

		TileIntersectionParameters.Set(RHICmdList, ShaderRHI, *TileIntersectionResources);
	}

	void UnsetParameters(FRHICommandList& RHICmdList, const FSceneView& View, FLightTileIntersectionResources* TileIntersectionResources)
	{
		TileIntersectionParameters.UnsetParameters(RHICmdList, GetComputeShader());

		TArray<FRHIUnorderedAccessView*> UAVs;
		TileIntersectionParameters.GetUAVs(*TileIntersectionResources, UAVs);

		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, UAVs.GetData(), UAVs.Num());
	}

	virtual bool Serialize(FArchive& Ar) override
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TileIntersectionParameters;
		return bShaderHasOutdatedParameters;
	}

private:

	FLightTileIntersectionParameters TileIntersectionParameters;
};

IMPLEMENT_SHADER_TYPE(,FComputeCulledObjectStartOffsetCS,TEXT("/Engine/Private/DistanceFieldShadowing.usf"),TEXT("ComputeCulledTilesStartOffsetCS"),SF_Compute);

template<bool bCountingPass, EDistanceFieldPrimitiveType PrimitiveType>
void ScatterObjectsToShadowTiles(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View, 
	const FMatrix& WorldToShadowValue, 
	float ShadowBoundingRadius,
	FIntPoint LightTileDimensions, 
	FLightTileIntersectionResources* TileIntersectionResources)
{
	TShaderMapRef<TShadowObjectCullVS<PrimitiveType>> VertexShader(View.ShaderMap);
	TShaderMapRef<TShadowObjectCullPS<bCountingPass, PrimitiveType>> PixelShader(View.ShaderMap);

	TArray<FRHIUnorderedAccessView*> UAVs;
	PixelShader->GetUAVs(View, TileIntersectionResources, UAVs);
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, UAVs.GetData(), UAVs.Num());

	FRHIRenderPassInfo RPInfo(UAVs.Num(), UAVs.GetData());
	if (GRHIRequiresRenderTargetForPixelShaderUAVs)
	{
		TRefCountPtr<IPooledRenderTarget> Dummy;
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(LightTileDimensions, PF_B8G8R8A8, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Dummy, TEXT("Dummy"));

		RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::DontLoad_DontStore;
		RPInfo.ColorRenderTargets[0].ArraySlice = -1;
		RPInfo.ColorRenderTargets[0].MipIndex = 0;
		RPInfo.ColorRenderTargets[0].RenderTarget = Dummy->GetRenderTargetItem().TargetableTexture;
	}
	
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ScatterObjectsToShadowTiles"));
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		RHICmdList.SetViewport(0, 0, 0.0f, LightTileDimensions.X, LightTileDimensions.Y, 1.0f);

		// Render backfaces since camera may intersect
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, View, FVector2D(LightTileDimensions.X, LightTileDimensions.Y), WorldToShadowValue, ShadowBoundingRadius);
		PixelShader->SetParameters(RHICmdList, View, TileIntersectionResources);

		RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

		RHICmdList.DrawIndexedPrimitiveIndirect(
			GetUnitCubeIndexBuffer(),
			GShadowCulledObjectBuffers.Buffers.ObjectIndirectArguments.Buffer,
			0);
	}
	RHICmdList.EndRenderPass();

	// #todo-renderpasses Needed for now. Once SetRenderTarget is removed completely this will be redundant.
	UnbindRenderTargets(RHICmdList);
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToCompute, UAVs.GetData(), UAVs.Num());
}

template <EDistanceFieldPrimitiveType PrimitiveType>
void CullDistanceFieldObjectsForLight_Internal(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy, 
	const FMatrix& WorldToShadowValue, 
	int32 NumPlanes, 
	const FPlane* PlaneData, 
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	TUniquePtr<FLightTileIntersectionResources>& TileIntersectionResources)
{
	constexpr bool bIsHeightfield = PrimitiveType == DFPT_HeightField;
	const FScene* Scene = (const FScene*)(View.Family->Scene);

	SCOPED_DRAW_EVENT(RHICmdList, CullObjectsForLight);

	{
		const FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;
		const int32 NumObjectsInBuffer = bIsHeightfield ? SceneData.NumHeightFieldObjectsInBuffer : SceneData.NumObjectsInBuffer;
		auto& CulledObjectBuffers = TSelector<bIsHeightfield>()(GShadowCulledHeightFieldObjectBuffers, GShadowCulledObjectBuffers);

		if (!CulledObjectBuffers.IsInitialized()
			|| CulledObjectBuffers.Buffers.MaxObjects < NumObjectsInBuffer
			|| CulledObjectBuffers.Buffers.MaxObjects > 3 * NumObjectsInBuffer
			|| GFastVRamConfig.bDirty)
		{
			CulledObjectBuffers.Buffers.bWantBoxBounds = true;
			CulledObjectBuffers.Buffers.MaxObjects = NumObjectsInBuffer * 5 / 4;
			CulledObjectBuffers.ReleaseResource();
			CulledObjectBuffers.InitResource();
		}
		CulledObjectBuffers.Buffers.AcquireTransientResource();

		{
			SCOPED_DRAW_EVENTF(RHICmdList, CullObjectsToFrustum, TEXT("CullObjectsToFrustum %sObjects %d"), bIsHeightfield ? TEXT("HeightField") : TEXT("DistanceField"), NumObjectsInBuffer);

			ClearUAV(RHICmdList, CulledObjectBuffers.Buffers.ObjectIndirectArguments, 0);

			TShaderMapRef<TCullObjectsForShadowCS<PrimitiveType>> ComputeShader(GetGlobalShaderMap(Scene->GetFeatureLevel()));
			RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
			ComputeShader->SetParameters(RHICmdList, Scene, View, WorldToShadowValue, NumPlanes, PlaneData, ShadowBoundingSphereValue);

			DispatchComputeShader(RHICmdList, *ComputeShader, FMath::DivideAndRoundUp<uint32>(NumObjectsInBuffer, UpdateObjectsGroupSize), 1, 1);
			ComputeShader->UnsetParameters(RHICmdList, Scene);
		}
	}

	// Allocate tile resolution based on world space size
	const float LightTiles = FMath::Min(ShadowBoundingRadius / GShadowCullTileWorldSize + 1.0f, 256.0f);
	FIntPoint LightTileDimensions(LightTiles, LightTiles);

	if (LightSceneProxy->GetLightType() == LightType_Directional && GShadowScatterTileCulling)
	{
		const bool b16BitObjectIndices = Scene->DistanceFieldSceneData.CanUse16BitObjectIndices();
		bool bLightDimensionsDirty = false;

		if (TileIntersectionResources)
		{
			FIntPoint AlignedLightDimensions = FLightTileIntersectionResources::GetAlignedDimensions(LightTileDimensions);
			bLightDimensionsDirty = AlignedLightDimensions.X > TileIntersectionResources->GetTileAlignedDimensions().X;
		}
		
		if (!TileIntersectionResources || bLightDimensionsDirty || TileIntersectionResources->b16BitIndices != b16BitObjectIndices)
		{
			if (TileIntersectionResources)
			{
				TileIntersectionResources->Release();
			}
			else
			{
				TileIntersectionResources = MakeUnique<FLightTileIntersectionResources>();
			}

			TileIntersectionResources->b16BitIndices = b16BitObjectIndices;
			TileIntersectionResources->TileDimensions = LightTileDimensions;
			TileIntersectionResources->Initialize(bIsHeightfield ? GAverageHeightFieldObjectsPerShadowCullTile : GAverageObjectsPerShadowCullTile);
		}

		check(TileIntersectionResources);
		TileIntersectionResources->TileDimensions = LightTileDimensions;

		{
			SCOPED_DRAW_EVENT(RHICmdList, ComputeTileStartOffsets);

			// Start at 0 tiles per object
			ClearUAV(RHICmdList, TileIntersectionResources->TileNumCulledObjects, 0);

			// Rasterize object bounding shapes and intersect with shadow tiles to compute how many objects intersect each tile
			ScatterObjectsToShadowTiles<true, PrimitiveType>(RHICmdList, View, WorldToShadowValue, ShadowBoundingRadius, LightTileDimensions, TileIntersectionResources.Get());

			ClearUAV(RHICmdList, TileIntersectionResources->NextStartOffset, 0);

			uint32 GroupSizeX = FMath::DivideAndRoundUp<int32>(LightTileDimensions.X, ComputeCulledObjectStartOffsetGroupSize);
			uint32 GroupSizeY = FMath::DivideAndRoundUp<int32>(LightTileDimensions.Y, ComputeCulledObjectStartOffsetGroupSize);

			// Compute the start offset for each tile's culled object data
			TShaderMapRef<FComputeCulledObjectStartOffsetCS> ComputeShader(View.ShaderMap);
			RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
			ComputeShader->SetParameters(RHICmdList, View, TileIntersectionResources.Get());
			DispatchComputeShader(RHICmdList, *ComputeShader, GroupSizeX, GroupSizeY, 1);
			ComputeShader->UnsetParameters(RHICmdList, View, TileIntersectionResources.Get());
		}

		{
			SCOPED_DRAW_EVENTF(RHICmdList, CullObjectsToTiles, TEXT("CullObjectsToTiles %ux%u"), LightTileDimensions.X, LightTileDimensions.Y);

			// Start at 0 tiles per object
			ClearUAV(RHICmdList, TileIntersectionResources->TileNumCulledObjects, 0);

			// Rasterize object bounding shapes and intersect with shadow tiles, and write out intersecting tile indices for the cone tracing pass
			ScatterObjectsToShadowTiles<false, PrimitiveType>(RHICmdList, View, WorldToShadowValue, ShadowBoundingRadius, LightTileDimensions, TileIntersectionResources.Get());
		}
	}
}

void CullDistanceFieldObjectsForLight(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy,
	const FMatrix& WorldToShadowValue,
	int32 NumPlanes,
	const FPlane* PlaneData,
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	TUniquePtr<FLightTileIntersectionResources>& TileIntersectionResources)
{
	CullDistanceFieldObjectsForLight_Internal<DFPT_SignedDistanceField>(
		RHICmdList,
		View,
		LightSceneProxy,
		WorldToShadowValue,
		NumPlanes,
		PlaneData,
		ShadowBoundingSphereValue,
		ShadowBoundingRadius,
		TileIntersectionResources);
}

void CullHeightFieldObjectsForLight(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FLightSceneProxy* LightSceneProxy,
	const FMatrix& WorldToShadowValue,
	int32 NumPlanes,
	const FPlane* PlaneData,
	const FVector4& ShadowBoundingSphereValue,
	float ShadowBoundingRadius,
	TUniquePtr<FLightTileIntersectionResources>& TileIntersectionResources)
{
	CullDistanceFieldObjectsForLight_Internal<DFPT_HeightField>(
		RHICmdList,
		View,
		LightSceneProxy,
		WorldToShadowValue,
		NumPlanes,
		PlaneData,
		ShadowBoundingSphereValue,
		ShadowBoundingRadius,
		TileIntersectionResources);
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

template<typename TRHICommandList, EDistanceFieldShadowingType DFSType, EDistanceFieldPrimitiveType PrimitiveType, bool bHasPrevOutput>
void RayTraceShadowsDispatch(
	TRHICommandList& RHICmdList,
	const FViewInfo& View,
	FProjectedShadowInfo* ProjectedShadowInfo,
	FLightTileIntersectionResources* TileIntersectionResources,
	FRHITexture* PrevOutput = nullptr)
{
	check(DFSType != DFS_PointLightTiledCulling || PrimitiveType != DFPT_HeightField);

	FIntRect ScissorRect;
	if (!ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetScissorRect(ScissorRect, View, View.ViewRect))
	{
		ScissorRect = View.ViewRect;
	}

	uint32 GroupSizeX = FMath::DivideAndRoundUp(ScissorRect.Size().X / GetDFShadowDownsampleFactor(), GDistanceFieldShadowTileSizeX);
	uint32 GroupSizeY = FMath::DivideAndRoundUp(ScissorRect.Size().Y / GetDFShadowDownsampleFactor(), GDistanceFieldShadowTileSizeY);

	auto DispatchTemplatedCS = [&](auto CS)
	{
		RHICmdList.SetComputeShader(CS->GetComputeShader());
		FSceneRenderTargetItem& RayTracedShadowsRTI = ProjectedShadowInfo->RayTracedShadowsRT->GetRenderTargetItem();
		CS->SetParameters(RHICmdList, View, ProjectedShadowInfo, RayTracedShadowsRTI, FVector2D(GroupSizeX, GroupSizeY), ScissorRect, TileIntersectionResources, PrevOutput);
		DispatchComputeShader(RHICmdList, *CS, GroupSizeX, GroupSizeY, 1);
		CS->UnsetParameters(RHICmdList, RayTracedShadowsRTI);
	};

	int32 const DFShadowQuality = PrimitiveType == DFPT_HeightField ? GetHFShadowQuality() : GetDFShadowQuality();

	if (DFShadowQuality == 1)
	{
		TShaderMapRef<TDistanceFieldShadowingCS<DFSType, 1, PrimitiveType, bHasPrevOutput>> ComputeShader(View.ShaderMap);
		DispatchTemplatedCS(ComputeShader);
	}
	else if (DFShadowQuality == 2)
	{
		TShaderMapRef<TDistanceFieldShadowingCS<DFSType, 2, PrimitiveType, bHasPrevOutput>> ComputeShader(View.ShaderMap);
		DispatchTemplatedCS(ComputeShader);
	}
	else if (DFShadowQuality == 3)
	{
		TShaderMapRef<TDistanceFieldShadowingCS<DFSType, 3, PrimitiveType, bHasPrevOutput>> ComputeShader(View.ShaderMap);
		DispatchTemplatedCS(ComputeShader);
	}
}

template<typename TRHICommandList>
void RayTraceShadows(TRHICommandList& RHICmdList, const FViewInfo& View, FProjectedShadowInfo* ProjectedShadowInfo, FLightTileIntersectionResources* TileIntersectionResources)
{
	int32 const DFShadowQuality = GetDFShadowQuality();
	if (ProjectedShadowInfo->bDirectionalLight && GShadowScatterTileCulling)
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_DirectionalLightScatterTileCulling, DFPT_SignedDistanceField, false>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources);
	}
	else if (ProjectedShadowInfo->bDirectionalLight)
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_DirectionalLightTiledCulling, DFPT_SignedDistanceField, false>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources);
	}
	else
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_PointLightTiledCulling, DFPT_SignedDistanceField, false>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources);
	}
}

template <typename TRHICommandList>
void RayTraceHeightFieldShadows(
	TRHICommandList& RHICmdList,
	const FViewInfo& View,
	FProjectedShadowInfo* ProjectedShadowInfo,
	FLightTileIntersectionResources* TileIntersectionResources,
	bool bHasPrevOutput,
	FRHITexture* PrevOutput)
{
	check(ProjectedShadowInfo->bDirectionalLight);

	if (GShadowScatterTileCulling && bHasPrevOutput)
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_DirectionalLightScatterTileCulling, DFPT_HeightField, true>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources, PrevOutput);
	}
	else if (GShadowScatterTileCulling)
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_DirectionalLightScatterTileCulling, DFPT_HeightField, false>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources);
	}
	else if (bHasPrevOutput)
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_DirectionalLightTiledCulling, DFPT_HeightField, true>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources, PrevOutput);
	}
	else
	{
		RayTraceShadowsDispatch<TRHICommandList, DFS_DirectionalLightTiledCulling, DFPT_HeightField, false>(RHICmdList, View, ProjectedShadowInfo, TileIntersectionResources);
	}
}

void FProjectedShadowInfo::BeginRenderRayTracedDistanceFieldProjection(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	const bool bDFShadowSupported = SupportsDistanceFieldShadows(View.GetFeatureLevel(), View.GetShaderPlatform());
	const bool bHFShadowSupported = SupportsHeightFieldShadows(View.GetFeatureLevel(), View.GetShaderPlatform());
	const bool bBufferAliasingEnabled = IsTransientResourceBufferAliasingEnabled();
	const FScene* Scene = (const FScene*)(View.Family->Scene);

	if (bDFShadowSupported && View.Family->EngineShowFlags.RayTracedDistanceFieldShadows)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BeginRenderRayTracedDistanceFieldShadows);
		SCOPED_DRAW_EVENT(RHICmdList, BeginRayTracedDistanceFieldShadow);

		if (GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI && Scene->DistanceFieldSceneData.NumObjectsInBuffer > 0)
		{
			check(!Scene->DistanceFieldSceneData.HasPendingOperations());

			UnbindRenderTargets(RHICmdList);

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
				NumPlanes = CasterFrustum.Planes.Num();
				PlaneData = CasterFrustum.Planes.GetData();
				ShadowBoundingSphereValue = FVector4(PreShadowTranslation, 0);
			}

			const FMatrix WorldToShadowValue = FTranslationMatrix(PreShadowTranslation) * SubjectAndReceiverMatrix;

			CullDistanceFieldObjectsForLight(
				RHICmdList,
				View,
				LightSceneInfo->Proxy,
				WorldToShadowValue,
				NumPlanes,
				PlaneData,
				ShadowBoundingSphereValue,
				ShadowBounds.W,
				LightSceneInfo->TileIntersectionResources
				);

			// Note: using the same TileIntersectionResources for multiple views, breaks splitscreen / stereo
			FLightTileIntersectionResources* TileIntersectionResources = LightSceneInfo->TileIntersectionResources.Get();

			View.HeightfieldLightingViewInfo.ComputeRayTracedShadowing(View, RHICmdList, this, TileIntersectionResources, GShadowCulledObjectBuffers);

			{
				const FIntPoint BufferSize = GetBufferSizeForDFShadows();
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G16R16F, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_UAV, false));
				Desc.Flags |= GFastVRamConfig.DistanceFieldShadows;
				GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RayTracedShadowsRT, TEXT("RayTracedShadows"));
			}

			SCOPED_DRAW_EVENT(RHICmdList, RayTraceShadows);
			UnbindRenderTargets(RHICmdList);

			RayTraceShadows(RHICmdList, View, this, TileIntersectionResources);

			if (bBufferAliasingEnabled)
			{
				GShadowCulledObjectBuffers.Buffers.DiscardTransientResource();
			}
		}
	}

	if (bDirectionalLight
		&& View.Family->EngineShowFlags.RayTracedDistanceFieldShadows
		&& GHeightFieldTextureAtlas.GetAtlasTexture()
		&& Scene->DistanceFieldSceneData.NumHeightFieldObjectsInBuffer > 0
		&& bHFShadowSupported)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BeginRenderRayTracedHeightFieldShadows);
		SCOPED_DRAW_EVENT(RHICmdList, BeginRenderRayTracedHeightFieldShadows);

		check(!Scene->DistanceFieldSceneData.HasPendingHeightFieldOperations());

		UnbindRenderTargets(RHICmdList);

		const int32 NumPlanes = CascadeSettings.ShadowBoundsAccurate.Planes.Num();
		const FPlane* PlaneData = CascadeSettings.ShadowBoundsAccurate.Planes.GetData();
		const FVector4 ShadowBoundingSphereValue(0.f, 0.f, 0.f, 0.f);
		const FMatrix WorldToShadowValue = FTranslationMatrix(PreShadowTranslation) * SubjectAndReceiverMatrix;

		CullHeightFieldObjectsForLight(
			RHICmdList,
			View,
			LightSceneInfo->Proxy,
			WorldToShadowValue,
			NumPlanes,
			PlaneData,
			ShadowBoundingSphereValue,
			ShadowBounds.W,
			LightSceneInfo->HeightFieldTileIntersectionResources);

		FLightTileIntersectionResources* TileIntersectionResources = LightSceneInfo->HeightFieldTileIntersectionResources.Get();
		const bool bHasPrevOutput = !!RayTracedShadowsRT;
		TRefCountPtr<IPooledRenderTarget> PrevOutputRT;

		if (!RHISupports4ComponentUAVReadWrite(View.GetShaderPlatform()))
		{
			PrevOutputRT = RayTracedShadowsRT;
			RayTracedShadowsRT = nullptr;
		}

		if (!RayTracedShadowsRT)
		{
			const FIntPoint BufferSize = GetBufferSizeForDFShadows();
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G16R16F, FClearValueBinding::None, TexCreate_None, TexCreate_RenderTargetable | TexCreate_UAV, false));
			Desc.Flags |= GFastVRamConfig.DistanceFieldShadows;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RayTracedShadowsRT, TEXT("RayTracedShadows"));
		}

		SCOPED_DRAW_EVENT(RHICmdList, RayTraceHeightFieldShadows);
		UnbindRenderTargets(RHICmdList);

		RayTraceHeightFieldShadows(RHICmdList, View, this, TileIntersectionResources, bHasPrevOutput, PrevOutputRT ? PrevOutputRT->GetRenderTargetItem().ShaderResourceTexture : nullptr);

		if (bBufferAliasingEnabled)
		{
			GShadowCulledHeightFieldObjectBuffers.Buffers.DiscardTransientResource();
		}
	}
}

void FProjectedShadowInfo::RenderRayTracedDistanceFieldProjection(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FIntRect& ScissorRect, IPooledRenderTarget* ScreenShadowMaskTexture, bool bProjectingForForwardShading)
{
	check(ScissorRect.Area() > 0);

	BeginRenderRayTracedDistanceFieldProjection(RHICmdList, View);

	if (RayTracedShadowsRT)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_RenderRayTracedDistanceFieldShadows);
		SCOPED_DRAW_EVENT(RHICmdList, RayTracedDistanceFieldShadow);

		{
			FRHIRenderPassInfo RPInfo(ScreenShadowMaskTexture->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
			RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_Store);
			RPInfo.DepthStencilRenderTarget.DepthStencilTarget = FSceneRenderTargets::Get(RHICmdList).GetSceneDepthSurface();
			RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;

			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("RenderRayTracedDistanceFieldShadows"));
			{

				SCOPED_DRAW_EVENT(RHICmdList, Upsample);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				RHICmdList.SetViewport(ScissorRect.Min.X, ScissorRect.Min.Y, 0.0f, ScissorRect.Max.X, ScissorRect.Max.Y, 1.0f);
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				SetBlendStateForProjection(GraphicsPSOInit, bProjectingForForwardShading, false);

				TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				GraphicsPSOInit.bDepthBounds = bDirectionalLight;

				if (GFullResolutionDFShadowing)
				{
					TShaderMapRef<TDistanceFieldShadowingUpsamplePS<false> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);
					PixelShader->SetParameters(RHICmdList, View, this, ScissorRect, RayTracedShadowsRT);
				}
				else
				{
					TShaderMapRef<TDistanceFieldShadowingUpsamplePS<true> > PixelShader(View.ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);
					PixelShader->SetParameters(RHICmdList, View, this, ScissorRect, RayTracedShadowsRT);
				}

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
					*VertexShader);
			}
			RHICmdList.EndRenderPass();
		}

		RayTracedShadowsRT = NULL;
		RayTracedShadowsEndFence = NULL;
	}
}
