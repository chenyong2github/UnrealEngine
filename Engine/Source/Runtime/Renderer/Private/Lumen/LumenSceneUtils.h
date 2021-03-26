// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneUtils.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "RendererPrivateUtils.h"
#include "Lumen.h"
#include "DistanceFieldLightingShared.h"

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardScatterParameters, )
	RDG_BUFFER_ACCESS(CardIndirectArgs, ERHIAccess::IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadData)
	SHADER_PARAMETER(uint32, MaxQuadsPerScatterInstance)
	SHADER_PARAMETER(uint32, TilesPerInstance)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCullCardsShapeParameters, )
	SHADER_PARAMETER(FVector4, InfluenceSphere)
	SHADER_PARAMETER(FVector, LightPosition)
	SHADER_PARAMETER(FVector, LightDirection)
	SHADER_PARAMETER(float, LightRadius)
	SHADER_PARAMETER(float, CosConeAngle)
	SHADER_PARAMETER(float, SinConeAngle)
END_SHADER_PARAMETER_STRUCT()

enum class ECullCardsMode
{
	OperateOnCardsToRender,
	OperateOnScene,
	OperateOnSceneForceUpdateForCardsToRender,
};

enum class ECullCardsShapeType
{
	None,
	PointLight,
	SpotLight,
	RectLight
};

class FLumenCardScatterContext
{
public:
	int32 MaxQuadCount = 0;
	int32 MaxScatterInstanceCount = 0;
	int32 MaxQuadsPerScatterInstance = 0;
	int32 NumCardsToOperateOn = 0;
	ECullCardsMode CardsCullMode;

	FLumenCardScatterParameters Parameters;

	FRDGBufferUAVRef QuadAllocatorUAV = nullptr;
	FRDGBufferUAVRef QuadDataUAV = nullptr;

	void Init(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FLumenSceneData& LumenSceneData,
		const FLumenCardRenderer& LumenCardRenderer,
		ECullCardsMode InCardsCullMode,
		int32 InMaxScatterInstanceCount = 1);

	void CullCardsToShape(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FLumenSceneData& LumenSceneData,
		const FLumenCardRenderer& LumenCardRenderer,
		TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
		ECullCardsShapeType ShapeType,
		const FCullCardsShapeParameters& ShapeParameters,
		float UpdateFrequencyScale,
		int32 ScatterInstanceIndex);

	void BuildScatterIndirectArgs(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View);

	uint32 GetIndirectArgOffset(int32 ScatterInstanceIndex) const;
};

class FCullCardsToShapeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullCardsToShapeCS);
	SHADER_USE_PARAMETER_STRUCT(FCullCardsToShapeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, MaxQuadsPerScatterInstance)
		SHADER_PARAMETER(uint32, ScatterInstanceIndex)
		SHADER_PARAMETER(uint32, NumVisibleCardsIndices)
		SHADER_PARAMETER(uint32, NumCardsToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VisibleCardsIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardsToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardsToRenderHashMap)
		SHADER_PARAMETER(uint32, FrameId)
		SHADER_PARAMETER(float, CardLightingUpdateFrequencyScale)
		SHADER_PARAMETER(uint32, CardLightingUpdateMinFrequency)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCullCardsShapeParameters, ShapeParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FOperateOnCardsMode : SHADER_PERMUTATION_INT("OPERATE_ON_CARDS_MODE", 3);
	class FShapeType : SHADER_PERMUTATION_INT("SHAPE_TYPE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FOperateOnCardsMode, FShapeType>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};

class FInitializeCardScatterIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInitializeCardScatterIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FInitializeCardScatterIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCardIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
		SHADER_PARAMETER(uint32, MaxScatterInstanceCount)
		SHADER_PARAMETER(uint32, TilesPerInstance)
	END_SHADER_PARAMETER_STRUCT()

	class FRectList : SHADER_PERMUTATION_BOOL("RECT_LIST_TOPOLOGY");
	using FPermutationDomain = TShaderPermutationDomain<FRectList>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

class FRasterizeToCardsVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRasterizeToCardsVS);
	SHADER_USE_PARAMETER_STRUCT(FRasterizeToCardsVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardScatterParameters, CardScatterParameters)
		SHADER_PARAMETER(FVector4, InfluenceSphere)
		SHADER_PARAMETER(FVector2D, CardUVSamplingOffset)
		SHADER_PARAMETER(uint32, ScatterInstanceIndex)
	END_SHADER_PARAMETER_STRUCT()

	class FClampToInfluenceSphere : SHADER_PERMUTATION_BOOL("CLAMP_TO_INFLUENCE_SPHERE");

	using FPermutationDomain = TShaderPermutationDomain<FClampToInfluenceSphere>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};

extern TGlobalResource<FTileTexCoordVertexBuffer> GLumenTileTexCoordVertexBuffer;
extern TGlobalResource<FTileIndexBuffer> GLumenTileIndexBuffer;

extern const int32 NumLumenQuadsInBuffer;

inline bool UseRectTopologyForLumen()
{
	//@todo - debug why rects aren't working
	return false;
	//return GRHISupportsRectTopology != 0;
}

template<typename PixelShaderType, typename PassParametersType>
void DrawQuadsToAtlas(
	FIntPoint ViewportSize,
	TShaderRefBase<PixelShaderType, FShaderMapPointerTable> PixelShader,
	const PassParametersType* PassParameters,
	FGlobalShaderMap* GlobalShaderMap,
	FRHIBlendState* BlendState,
	FRHICommandListImmediate& RHICmdList)
{
	FRasterizeToCardsVS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FRasterizeToCardsVS::FClampToInfluenceSphere >(false);
	auto VertexShader = GlobalShaderMap->GetShader<FRasterizeToCardsVS>(PermutationVector);

	DrawQuadsToAtlas(ViewportSize, 
		VertexShader,
		PixelShader, 
		PassParameters, 
		GlobalShaderMap, 
		BlendState, 
		RHICmdList, 
		[](FRHICommandListImmediate& RHICmdList, TShaderRefBase<PixelShaderType, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const typename PixelShaderType::FParameters& Parameters)
	{
	});
}

template<typename PixelShaderType, typename PassParametersType, typename SetParametersLambdaType>
void DrawQuadsToAtlas(
	FIntPoint ViewportSize,
	TShaderRefBase<FRasterizeToCardsVS, FShaderMapPointerTable> VertexShader,
	TShaderRefBase<PixelShaderType, FShaderMapPointerTable> PixelShader,
	const PassParametersType* PassParameters,
	FGlobalShaderMap* GlobalShaderMap,
	FRHIBlendState* BlendState,
	FRHICommandListImmediate& RHICmdList,
	SetParametersLambdaType&& SetParametersLambda,
	uint32 CardIndirectArgOffset = 0)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	RHICmdList.SetViewport(0, 0, 0.0f, ViewportSize.X, ViewportSize.Y, 1.0f);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = BlendState;

	GraphicsPSOInit.PrimitiveType = UseRectTopologyForLumen() ? PT_RectList : PT_TriangleList;

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTileVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
	SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
	SetParametersLambda(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

	RHICmdList.SetStreamSource(0, GLumenTileTexCoordVertexBuffer.VertexBufferRHI, 0);

	if (UseRectTopologyForLumen())
	{
		RHICmdList.DrawPrimitiveIndirect(PassParameters->VS.CardScatterParameters.CardIndirectArgs->GetIndirectRHICallBuffer(), CardIndirectArgOffset);
	}
	else
	{
		RHICmdList.DrawIndexedPrimitiveIndirect(GLumenTileIndexBuffer.IndexBufferRHI, PassParameters->VS.CardScatterParameters.CardIndirectArgs->GetIndirectRHICallBuffer(), CardIndirectArgOffset);
	}
}

class FHemisphereDirectionSampleGenerator
{
public:
	TArray<FVector4> SampleDirections;
	float ConeHalfAngle = 0;
	int32 Seed = 0;
	int32 PowerOfTwoDivisor = 1;
	bool bFullSphere = false;
	bool bCosineDistribution = false;

	void GenerateSamples(int32 TargetNumSamples, int32 InPowerOfTwoDivisor, int32 InSeed, bool bInFullSphere = false, bool bInCosineDistribution = false);

	void GetSampleDirections(const FVector4*& OutDirections, int32& OutNumDirections) const
	{
		OutDirections = SampleDirections.GetData();
		OutNumDirections = SampleDirections.Num();
	}
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenVoxelTracingParameters, )
SHADER_PARAMETER(uint32, NumClipmapLevels)
SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldToUVScale, [MaxVoxelClipmapLevels])
SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldToUVBias, [MaxVoxelClipmapLevels])
SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldCenter, [MaxVoxelClipmapLevels])
SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldExtent, [MaxVoxelClipmapLevels])
SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSamplingExtent, [MaxVoxelClipmapLevels])
SHADER_PARAMETER_ARRAY(FVector4, ClipmapVoxelSizeAndRadius, [MaxVoxelClipmapLevels])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern void GetLumenVoxelParametersForClipmapLevel(const FLumenCardTracingInputs& TracingInputs, FLumenVoxelTracingParameters& LumenVoxelTracingParameters, 
								int SrcClipmapLevel, int DstClipmapLevel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardTracingParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FinalLightingAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IrradianceAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectIrradianceAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthAtlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VoxelLighting)
	SHADER_PARAMETER_STRUCT_REF(FLumenVoxelTracingParameters, LumenVoxelTracingParameters)
	SHADER_PARAMETER(uint32, NumGlobalSDFClipmaps)
END_SHADER_PARAMETER_STRUCT()

class FLumenCardTracingInputs
{
public:

	FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View);

	FRDGTextureRef FinalLightingAtlas;
	FRDGTextureRef IrradianceAtlas;
	FRDGTextureRef IndirectIrradianceAtlas;
	FRDGTextureRef OpacityAtlas;
	FRDGTextureRef DepthAtlas;
	FRDGTextureRef VoxelLighting;
	FIntVector VoxelGridResolution;
	int32 NumClipmapLevels;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVScale;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldToUVBias;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldCenter;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldExtent;
	TStaticArray<FVector, MaxVoxelClipmapLevels> ClipmapWorldSamplingExtent;
	TStaticArray<FVector4, MaxVoxelClipmapLevels> ClipmapVoxelSizeAndRadius;
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer;
};

// Must match LIGHT_TYPE_* in LumenSceneDirectLighting.usf
enum class ELumenLightType
{
	Directional,
	Point,
	Spot,
	Rect,

	MAX
};

struct FLumenDirectLightingHardwareRayTracingData
{
public:
	FLumenDirectLightingHardwareRayTracingData();

	void Initialize(FRDGBuilder& GraphBuilder, const FScene* Scene);

	void BeginLumenDirectLightingUpdate();
	void EndLumenDirectLightingUpdate();

	int GetLightId();
	bool ShouldClearLightMask();
	bool IsInterpolantsTextureCreated();

	FRDGTextureRef LightMaskTexture;
	FRDGTextureRef ShadowMaskAtlas;
	FRDGTextureRef CardInterpolantsTexture;
	FRDGBufferRef CardInterpolantsBuffer;
private:
	int LightId;	// Used for efficient raytracing when indirect dispatch is not supported.
	bool bSouldClearLightMask;
	bool bIsInterpolantsTextureCreated;
};

void RenderHardwareRayTracedShadowIntoLumenCards(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef OpacityAtlas,
	const FLightSceneInfo* LightSceneInfo,
	const FString& LightName,
	const FLumenCardScatterContext& CardScatterContext,
	int32 ScatterInstanceIndex,
	FLumenDirectLightingHardwareRayTracingData& LumenDirectLightingHardwareRayTracingData,
	bool bDynamicallyShadowed,
	ELumenLightType LumenLightType);

extern void GetLumenCardTracingParameters(const FViewInfo& View, const FLumenCardTracingInputs& TracingInputs, FLumenCardTracingParameters& TracingParameters, bool bShaderWillTraceCardsOnly = false);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenMeshSDFTracingParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, DistanceFieldObjectBuffers)
	SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldAtlasParameters, DistanceFieldAtlas)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenMeshSDFGridParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, TracingParameters)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumGridCulledMeshSDFObjects)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectStartOffsetArray)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GridCulledMeshSDFObjectIndicesArray)
	SHADER_PARAMETER(uint32, CardGridPixelSizeShift)
	SHADER_PARAMETER(FVector, CardGridZParams)
	SHADER_PARAMETER(FIntVector, CullGridSize)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenIndirectTracingParameters, )
	SHADER_PARAMETER(float, StepFactor)
	SHADER_PARAMETER(float, VoxelStepFactor)
	SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
	SHADER_PARAMETER(float, DiffuseConeHalfAngle)
	SHADER_PARAMETER(float, TanDiffuseConeHalfAngle)
	SHADER_PARAMETER(float, MinSampleRadius)
	SHADER_PARAMETER(float, MinTraceDistance)
	SHADER_PARAMETER(float, MaxTraceDistance)
	SHADER_PARAMETER(float, MaxMeshSDFTraceDistance)
	SHADER_PARAMETER(float, SurfaceBias)
	SHADER_PARAMETER(float, CardInterpolateInfluenceRadius)
	SHADER_PARAMETER(float, SpecularFromDiffuseRoughnessStart)
	SHADER_PARAMETER(float, SpecularFromDiffuseRoughnessEnd)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenDiffuseTracingParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(HybridIndirectLighting::FCommonParameters, CommonDiffuseParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
	SHADER_PARAMETER(float, SampleWeight)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledNormal)	
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FOctahedralSolidAngleParameters, )
	SHADER_PARAMETER(float, OctahedralSolidAngleTextureResolutionSq)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, OctahedralSolidAngleTexture)
END_SHADER_PARAMETER_STRUCT()

void VisualizeHardwareRayTracing(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRDGTextureRef SceneColor);

extern void CullMeshSDFObjectsToViewGrid(
	const FViewInfo& View,
	const FScene* Scene,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	int32 GridPixelsPerCellXY,
	int32 GridSizeZ,
	FVector ZParams,
	FRDGBuilder& GraphBuilder,
	FLumenMeshSDFGridParameters& OutGridParameters);

extern void CullMeshSDFObjectsToProbes(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	float MaxMeshSDFInfluenceRadius,
	float CardTraceEndDistanceFromCamera,
	const LumenProbeHierarchy::FHierarchyParameters& ProbeHierarchyParameters,
	const LumenProbeHierarchy::FEmitProbeParameters& EmitProbeParameters,
	FLumenMeshSDFGridParameters& OutGridParameters);

extern void CullForCardTracing(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	FLumenCardTracingInputs TracingInputs,
	const FLumenIndirectTracingParameters& IndirectTracingParameters,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters);

extern void SetupLumenDiffuseTracingParameters(FLumenIndirectTracingParameters& OutParameters);
extern void SetupLumenDiffuseTracingParametersForProbe(FLumenIndirectTracingParameters& OutParameters, float DiffuseConeAngle);
extern void ClearAtlasRDG(FRDGBuilder& GraphBuilder, FRDGTextureRef AtlasTexture);
extern FVector GetLumenSceneViewOrigin(const FViewInfo& View, int32 ClipmapIndex);
extern int32 GetNumLumenVoxelClipmaps();
extern void UpdateDistantScene(FScene* Scene, FViewInfo& View);
extern FIntPoint GetRadiosityAtlasSize(FIntPoint MaxAtlasSize);
extern float ComputeMaxCardUpdateDistanceFromCamera();

extern FRDGTextureRef InitializeOctahedralSolidAngleTexture(
	FRDGBuilder& GraphBuilder, 
	FGlobalShaderMap* ShaderMap,
	int32 OctahedralSolidAngleTextureSize,
	TRefCountPtr<IPooledRenderTarget>& OctahedralSolidAngleTextureRT);

extern int32 GLumenIrradianceFieldGather;

namespace LumenIrradianceFieldGather
{
	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs();
}

namespace LumenRadiosity
{
	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs();
}