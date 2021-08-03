// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneUtils.cpp
=============================================================================*/

#include "LumenSceneUtils.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "SceneTextureParameters.h"
#include "LumenMeshCards.h"
#include "LumenRadianceCache.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

int32 GLumenSceneLightingForceFullUpdate = 0;
FAutoConsoleVariableRef CVarLumenSceneLightingForceFullUpdate(
	TEXT("r.LumenScene.Lighting.ForceLightingUpdate"),
	GLumenSceneLightingForceFullUpdate,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneLightingMinUpdateFrequency = 3;
FAutoConsoleVariableRef CVarLumenSceneLightingMinUpdateFrequency(
	TEXT("r.LumenScene.Lighting.MinUpdateFrequency"),
	GLumenSceneLightingMinUpdateFrequency,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneSurfaceCacheDiffuseReflectivityOverride = 0;
FAutoConsoleVariableRef CVarLumenSceneDiffuseReflectivityOverride(
	TEXT("r.LumenScene.Lighting.DiffuseReflectivityOverride"),
	GLumenSceneSurfaceCacheDiffuseReflectivityOverride,
	TEXT(""),
	ECVF_RenderThreadSafe
);


namespace Lumen
{
	bool UseIrradianceAtlas(const FViewInfo& View)
	{
		bool bUsedInReflections = UseHardwareRayTracedReflections() && (GetReflectionsHardwareRayTracingLightingMode(View) == EHardwareRayTracingLightingMode::EvaluateMaterial);
		bool bUsedInScreenProbeGather = UseHardwareRayTracedScreenProbeGather() && (GetScreenProbeGatherHardwareRayTracingLightingMode() == EHardwareRayTracingLightingMode::EvaluateMaterial);
		bool bUsedInVisualization = ShouldVisualizeHardwareRayTracing() && (GetVisualizeHardwareRayTracingLightingMode() == EHardwareRayTracingLightingMode::EvaluateMaterial);
		return bUsedInReflections || bUsedInScreenProbeGather || bUsedInVisualization;
	}

	bool UseIndirectIrradianceAtlas(const FViewInfo& View)
	{
		bool bUsedInReflections = UseHardwareRayTracedReflections() && (GetReflectionsHardwareRayTracingLightingMode(View) == EHardwareRayTracingLightingMode::EvaluateMaterialAndDirectLighting);
		bool bUsedInScreenProbeGather = UseHardwareRayTracedScreenProbeGather() && (GetScreenProbeGatherHardwareRayTracingLightingMode() == EHardwareRayTracingLightingMode::EvaluateMaterialAndDirectLighting);
		bool bUsedInVisualization = ShouldVisualizeHardwareRayTracing() && (GetVisualizeHardwareRayTracingLightingMode() == EHardwareRayTracingLightingMode::EvaluateMaterialAndDirectLighting);
		return bUsedInReflections || bUsedInScreenProbeGather || bUsedInVisualization;
	}

	bool UseLumenSceneLightingForceFullUpdate()
	{
		return GLumenSceneLightingForceFullUpdate != 0;
	}
}

FLumenCardTracingInputs::FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View, bool bSurfaceCachaFeedback)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		FLumenCardScene* LumenCardSceneParameters = GraphBuilder.AllocParameters<FLumenCardScene>();
		SetupLumenCardSceneParameters(GraphBuilder, Scene, *LumenCardSceneParameters);
		LumenCardSceneUniformBuffer = GraphBuilder.CreateUniformBuffer(LumenCardSceneParameters);
	}

	check(LumenSceneData.FinalLightingAtlas);

	FinalLightingAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.FinalLightingAtlas);
	OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas);
	OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	NormalAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas);
	EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas);
	DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);

	auto RegisterOptionalAtlas = [&GraphBuilder, &View](bool (*UseAtlas)(const FViewInfo&), TRefCountPtr<IPooledRenderTarget> Atlas) {
		return UseAtlas(View) ? GraphBuilder.RegisterExternalTexture(Atlas) : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	};
	IrradianceAtlas = RegisterOptionalAtlas(Lumen::UseIrradianceAtlas, LumenSceneData.IrradianceAtlas);
	IndirectIrradianceAtlas = RegisterOptionalAtlas(Lumen::UseIndirectIrradianceAtlas, LumenSceneData.IndirectIrradianceAtlas);

	if (View.ViewState && View.ViewState->Lumen.VoxelLighting)
	{
		VoxelLighting = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.VoxelLighting);
		VoxelGridResolution = View.ViewState->Lumen.VoxelGridResolution;
		NumClipmapLevels = View.ViewState->Lumen.NumClipmapLevels;

		for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmapLevels; ++ClipmapIndex)
		{
			const FLumenVoxelLightingClipmapState& Clipmap = View.ViewState->Lumen.VoxelLightingClipmapState[ClipmapIndex];

			ClipmapWorldToUVScale[ClipmapIndex] = FVector(1.0f) / (2.0f * Clipmap.Extent);
			ClipmapWorldToUVBias[ClipmapIndex] = -(Clipmap.Center - Clipmap.Extent) * ClipmapWorldToUVScale[ClipmapIndex];
			ClipmapVoxelSizeAndRadius[ClipmapIndex] = FVector4(Clipmap.VoxelSize, Clipmap.VoxelRadius);
			ClipmapWorldCenter[ClipmapIndex] = Clipmap.Center;
			ClipmapWorldExtent[ClipmapIndex] = Clipmap.Extent;
			ClipmapWorldSamplingExtent[ClipmapIndex] = Clipmap.Extent - 0.5f * Clipmap.VoxelSize;
		}
	}
	else
	{
		VoxelLighting = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
		VoxelGridResolution = FIntVector(1);
		NumClipmapLevels = 0;
	}

	if (LumenSceneData.SurfaceCacheFeedbackResources.Buffer && bSurfaceCachaFeedback)
	{
		SurfaceCacheFeedbackBufferAllocatorUAV = GraphBuilder.CreateUAV(LumenSceneData.SurfaceCacheFeedbackResources.BufferAllocator, PF_R32_UINT);
		SurfaceCacheFeedbackBufferUAV = GraphBuilder.CreateUAV(LumenSceneData.SurfaceCacheFeedbackResources.Buffer, PF_R32G32_UINT);
		SurfaceCacheFeedbackBufferSize = LumenSceneData.SurfaceCacheFeedbackResources.BufferSize;
		SurfaceCacheFeedbackBufferTileJitter = LumenSceneData.SurfaceCacheFeedback.GetFeedbackBufferTileJitter();
		SurfaceCacheFeedbackBufferTileWrapMask = Lumen::GetFeedbackBufferTileWrapMask();
	}
	else
	{
		SurfaceCacheFeedbackBufferAllocatorUAV = LumenSceneData.SurfaceCacheFeedback.GetDummyFeedbackAllocatorUAV(GraphBuilder);
		SurfaceCacheFeedbackBufferUAV = LumenSceneData.SurfaceCacheFeedback.GetDummyFeedbackUAV(GraphBuilder);
		SurfaceCacheFeedbackBufferSize = 0;
		SurfaceCacheFeedbackBufferTileJitter = FIntPoint(0, 0);
		SurfaceCacheFeedbackBufferTileWrapMask = 0;
	}
}

typedef TUniformBufferRef<FLumenVoxelTracingParameters> FLumenVoxelTracingParametersBufferRef;
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenVoxelTracingParameters, "LumenVoxelTracingParameters");

void GetLumenVoxelParametersForClipmapLevel(const FLumenCardTracingInputs& TracingInputs, FLumenVoxelTracingParameters& LumenVoxelTracingParameters,
									int SrcClipmapLevel, int DstClipmapLevel)
{
	LumenVoxelTracingParameters.ClipmapWorldToUVScale[DstClipmapLevel] = TracingInputs.ClipmapWorldToUVScale[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldToUVBias[DstClipmapLevel] = TracingInputs.ClipmapWorldToUVBias[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapVoxelSizeAndRadius[DstClipmapLevel] = TracingInputs.ClipmapVoxelSizeAndRadius[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldCenter[DstClipmapLevel] = TracingInputs.ClipmapWorldCenter[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldExtent[DstClipmapLevel] = TracingInputs.ClipmapWorldExtent[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldSamplingExtent[DstClipmapLevel] = TracingInputs.ClipmapWorldSamplingExtent[SrcClipmapLevel];
}

//@todo Create the uniform buffer as less as possible.
void GetLumenVoxelTracingParameters(const FLumenCardTracingInputs& TracingInputs, FLumenCardTracingParameters& TracingParameters, bool bShaderWillTraceCardsOnly)
{
	FLumenVoxelTracingParameters LumenVoxelTracingParameters;

	LumenVoxelTracingParameters.NumClipmapLevels = TracingInputs.NumClipmapLevels;

	ensureMsgf(bShaderWillTraceCardsOnly || TracingInputs.NumClipmapLevels > 0, TEXT("Higher level code should have prevented GetLumenCardTracingParameters in a scene with no voxel clipmaps"));

	for (int32 i = 0; i < TracingInputs.NumClipmapLevels; i++)
	{
		GetLumenVoxelParametersForClipmapLevel(TracingInputs, LumenVoxelTracingParameters, i, i);
	}

	TracingParameters.LumenVoxelTracingParameters = CreateUniformBufferImmediate(LumenVoxelTracingParameters, UniformBuffer_SingleFrame);
}

void GetLumenCardTracingParameters(const FViewInfo& View, const FLumenCardTracingInputs& TracingInputs, FLumenCardTracingParameters& TracingParameters, bool bShaderWillTraceCardsOnly)
{
	LLM_SCOPE_BYTAG(Lumen);

	TracingParameters.View = View.ViewUniformBuffer;
	TracingParameters.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
	TracingParameters.ReflectionStruct = CreateReflectionUniformBuffer(View, UniformBuffer_MultiFrame);
	
	const FGPUScene& GPUScene = ((const FScene*)View.Family->Scene)->GPUScene;
	TracingParameters.GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
	TracingParameters.GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;

	extern float GLumenSurfaceCacheFeedbackResLevelBias;
	TracingParameters.RWSurfaceCacheFeedbackBufferAllocator = TracingInputs.SurfaceCacheFeedbackBufferAllocatorUAV;
	TracingParameters.RWSurfaceCacheFeedbackBuffer = TracingInputs.SurfaceCacheFeedbackBufferUAV;
	TracingParameters.SurfaceCacheFeedbackBufferSize = TracingInputs.SurfaceCacheFeedbackBufferSize;
	TracingParameters.SurfaceCacheFeedbackBufferTileJitter = TracingInputs.SurfaceCacheFeedbackBufferTileJitter;
	TracingParameters.SurfaceCacheFeedbackBufferTileWrapMask = TracingInputs.SurfaceCacheFeedbackBufferTileWrapMask;
	TracingParameters.SurfaceCacheFeedbackResLevelBias = GLumenSurfaceCacheFeedbackResLevelBias + 0.5f; // +0.5f required for uint to float rounding in shader

	TracingParameters.FinalLightingAtlas = TracingInputs.FinalLightingAtlas;
	TracingParameters.IrradianceAtlas = TracingInputs.IrradianceAtlas;
	TracingParameters.IndirectIrradianceAtlas = TracingInputs.IndirectIrradianceAtlas;
	TracingParameters.AlbedoAtlas = TracingInputs.AlbedoAtlas;
	TracingParameters.OpacityAtlas = TracingInputs.OpacityAtlas;
	TracingParameters.NormalAtlas = TracingInputs.NormalAtlas;
	TracingParameters.EmissiveAtlas = TracingInputs.EmissiveAtlas;
	TracingParameters.DepthAtlas = TracingInputs.DepthAtlas;
	TracingParameters.VoxelLighting = TracingInputs.VoxelLighting;
	
	if (TracingInputs.NumClipmapLevels > 0)
	{
		GetLumenVoxelTracingParameters(TracingInputs, TracingParameters, bShaderWillTraceCardsOnly);
	}

	TracingParameters.NumGlobalSDFClipmaps = View.GlobalDistanceFieldInfo.Clipmaps.Num();
}

class FInitializeCardPageIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInitializeCardPageIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FInitializeCardPageIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDrawCardPagesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBuildTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, QuadAllocator)
		SHADER_PARAMETER(uint32, VertexCountPerInstanceIndirect)
	END_SHADER_PARAMETER_STRUCT()

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

IMPLEMENT_GLOBAL_SHADER(FInitializeCardPageIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneUtils.usf", "InitializeCardPageIndirectArgsCS", SF_Compute);

class FCullCardPagesToShapeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullCardPagesToShapeCS);
	SHADER_USE_PARAMETER_STRUCT(FCullCardPagesToShapeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWQuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWQuadData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, MaxQuadsPerScatterInstance)
		SHADER_PARAMETER(uint32, NumCardPagesToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardPagesToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardPagesToRenderHashMap)
		SHADER_PARAMETER(uint32, FrameId)
		SHADER_PARAMETER(float, CardLightingUpdateFrequencyScale)
		SHADER_PARAMETER(uint32, CardLightingUpdateMinFrequency)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCullCardsShapeParameters, ShapeParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FOperateOnCardPagesMode : SHADER_PERMUTATION_INT("OPERATE_ON_CARD_TILES_MODE", 3);
	class FShapeType : SHADER_PERMUTATION_INT("SHAPE_TYPE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FOperateOnCardPagesMode, FShapeType>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("NUM_CARD_TILES_TO_RENDER_HASH_MAP_BUCKET_UINT32"), FLumenCardRenderer::NumCardPagesToRenderHashMapBucketUInt32);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullCardPagesToShapeCS, "/Engine/Private/Lumen/LumenSceneUtils.usf", "CullCardPagesToShapeCS", SF_Compute);

class FBuildCardTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildCardTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildCardTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, QuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, QuadData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, MaxQuadsPerScatterInstance)
		SHADER_PARAMETER(uint32, MaxCardTilesPerScatterInstance)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCullCardsShapeParameters, ShapeParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FShapeType : SHADER_PERMUTATION_INT("SHAPE_TYPE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FShapeType>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildCardTilesCS, "/Engine/Private/Lumen/LumenSceneUtils.usf", "BuildCardTilesCS", SF_Compute);

bool FRasterizeToCardsVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportLumenGI(Parameters.Platform);
}

bool FRasterizeToCardTilesVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportLumenGI(Parameters.Platform);
}

IMPLEMENT_GLOBAL_SHADER(FRasterizeToCardsVS,"/Engine/Private/Lumen/LumenSceneUtils.usf","RasterizeToCardsVS",SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FRasterizeToCardTilesVS,"/Engine/Private/Lumen/LumenSceneUtils.usf","RasterizeToCardTilesVS",SF_Vertex);

void FLumenCardScatterContext::Build(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData,
	const FLumenCardRenderer& LumenCardRenderer,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	bool InBuildCardTiles,
	ECullCardsMode InCardsCullMode,
	float UpdateFrequencyScale,
	FCullCardsShapeParameters ShapeParameters,
	ECullCardsShapeType ShapeType)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Card culling %d", (int32)ShapeType);

	CardsCullMode = InCardsCullMode;
	NumCardPagesToOperateOn = LumenSceneData.GetNumCardPages();

	if (CardsCullMode == ECullCardsMode::OperateOnCardPagesToRender)
	{
		NumCardPagesToOperateOn = LumenCardRenderer.CardPagesToRender.Num();
	}

	MaxQuadsPerScatterInstance = NumCardPagesToOperateOn;
	const int32 NumQuadsInBuffer = FMath::DivideAndRoundUp(MaxQuadsPerScatterInstance, 1024) * 1024;

	const uint32 MaxCardTilesX = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().X, Lumen::CardTileSize);
	const uint32 MaxCardTilesY = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().Y, Lumen::CardTileSize);
	MaxCardTilesPerScatterInstance = MaxCardTilesX * MaxCardTilesY;
	const uint32 NumCardTilesInBuffer = MaxCardTilesPerScatterInstance;

	FRDGBufferRef QuadAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.QuadAllocator"));
	FRDGBufferRef QuadDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumQuadsInBuffer), TEXT("Lumen.QuadDataBuffer"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(QuadAllocator, PF_R32_UINT), 0);

	CardPageParameters.QuadAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadAllocator, PF_R32_UINT));
	CardPageParameters.QuadData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadDataBuffer, PF_R32_UINT));
	CardPageParameters.MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;

	FRDGBufferRef CardTileAllocator = nullptr;
	FRDGBufferRef CardTileData = nullptr;
	if (InBuildCardTiles)
	{
		CardTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.CardTileAllocator"));
		CardTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumCardTilesInBuffer), TEXT("Lumen.CardTileData"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CardTileAllocator, PF_R32_UINT), 0);
		CardTileParameters.CardTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTileAllocator, PF_R32_UINT));
		CardTileParameters.CardTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTileData, PF_R32_UINT));
		CardTileParameters.MaxCardTilesPerScatterInstance = MaxCardTilesPerScatterInstance;
	}
	else
	{
		CardTileParameters.CardTileAllocator = nullptr;
		CardTileParameters.CardTileData = nullptr;
		CardTileParameters.MaxCardTilesPerScatterInstance = 0;
		CardTileParameters.DrawIndirectArgs = nullptr;
		CardTileParameters.DispatchIndirectArgs = nullptr;
	}

	// Build a list of card pages
	{
		FCullCardPagesToShapeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullCardPagesToShapeCS::FParameters>();
		PassParameters->RWQuadAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadAllocator));
		PassParameters->RWQuadData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadDataBuffer));
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->ShapeParameters = ShapeParameters;
		PassParameters->MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
		PassParameters->NumCardPagesToRenderIndices = LumenCardRenderer.CardPagesToRender.Num();
		PassParameters->CardPagesToRenderIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LumenCardRenderer.CardPagesToRenderIndexBuffer, PF_R32_UINT));
		PassParameters->CardPagesToRenderHashMap = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LumenCardRenderer.CardPagesToRenderHashMapBuffer, PF_R32_UINT));
		PassParameters->FrameId = View.ViewState->GetFrameIndex();
		PassParameters->CardLightingUpdateFrequencyScale = GLumenSceneLightingForceFullUpdate ? 0.0f : UpdateFrequencyScale;
		PassParameters->CardLightingUpdateMinFrequency = GLumenSceneLightingForceFullUpdate ? 1 : GLumenSceneLightingMinUpdateFrequency;

		FCullCardPagesToShapeCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FCullCardPagesToShapeCS::FOperateOnCardPagesMode>((uint32)CardsCullMode);
		PermutationVector.Set<FCullCardPagesToShapeCS::FShapeType>((int32)ShapeType);
		auto ComputeShader = View.ShaderMap->GetShader< FCullCardPagesToShapeCS >(PermutationVector);

		const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(NumCardPagesToOperateOn, FCullCardPagesToShapeCS::GetGroupSize()), 1, 1);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CullCardPagesToShape"),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShader, GroupSize](FRHICommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupSize);
			});
	}

	// Build card page indirect args
	{
		FRDGBufferRef DrawIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.DrawCardPagesIndirectArgs"));
		FRDGBufferRef DispatchIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("Lumen.DispatchCardPagesIndirectArgs"));

		FInitializeCardPageIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeCardPageIndirectArgsCS::FParameters>();
		PassParameters->RWDrawCardPagesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DrawIndirectArgs));
		PassParameters->RWBuildTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DispatchIndirectArgs));
		PassParameters->QuadAllocator = CardPageParameters.QuadAllocator;
		PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;

		auto ComputeShader = View.ShaderMap->GetShader<FInitializeCardPageIndirectArgsCS>();

		const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(1, FInitializeCardPageIndirectArgsCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitializeCardPageIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);

		CardPageParameters.DrawIndirectArgs = DrawIndirectArgs;
		CardPageParameters.DispatchIndirectArgs = DispatchIndirectArgs;
	}

	// Build a list of card tiles
	if (InBuildCardTiles)
	{
		FBuildCardTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildCardTilesCS::FParameters>();
		PassParameters->IndirectArgBuffer = CardPageParameters.DispatchIndirectArgs;
		PassParameters->RWCardTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CardTileAllocator));
		PassParameters->RWCardTileData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CardTileData));
		PassParameters->QuadAllocator = CardPageParameters.QuadAllocator;
		PassParameters->QuadData = CardPageParameters.QuadData;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->ShapeParameters = ShapeParameters;
		PassParameters->MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
		PassParameters->MaxCardTilesPerScatterInstance = MaxCardTilesPerScatterInstance;

		FBuildCardTilesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FBuildCardTilesCS::FShapeType>((int32)ShapeType);
		auto ComputeShader = View.ShaderMap->GetShader<FBuildCardTilesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildCardTiles"),
			ComputeShader,
			PassParameters,
			CardPageParameters.DispatchIndirectArgs,
			0);
	}

	// Build card tile indirect args
	if (InBuildCardTiles)
	{
		FRDGBufferRef DrawIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.DrawCardTilesIndirectArgs"));
		FRDGBufferRef DispatchIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("Lumen.DispatchCardTilesIndirectArgs"));

		FInitializeCardPageIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeCardPageIndirectArgsCS::FParameters>();
		PassParameters->RWDrawCardPagesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DrawIndirectArgs));
		PassParameters->RWBuildTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DispatchIndirectArgs));
		PassParameters->QuadAllocator = CardTileParameters.CardTileAllocator;
		PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;

		auto ComputeShader = View.ShaderMap->GetShader<FInitializeCardPageIndirectArgsCS>();

		const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(1, FInitializeCardPageIndirectArgsCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitializeCardTileIndirectArgs"),
			ComputeShader,
			PassParameters,
			GroupSize);

		CardTileParameters.DrawIndirectArgs = DrawIndirectArgs;
		CardTileParameters.DispatchIndirectArgs = DispatchIndirectArgs;
	}
}

class FLumenCardLightingInitializePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardLightingInitializePS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardLightingInitializePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityAtlas)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardLightingInitializePS, "/Engine/Private/Lumen/LumenSceneUtils.usf", "LumenCardLightingInitializePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardLightingEmissive, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardLightingInitializePS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FLumenCardCopyAtlasPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardCopyAtlasPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardCopyAtlasPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcAtlas)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCopyAtlasPS, "/Engine/Private/Lumen/LumenSceneUtils.usf", "LumenCardCopyAtlasPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardCopyAtlas, )
SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardCopyAtlasPS::FParameters, PS)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FLumenCardBlendAlbedoPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardBlendAlbedoPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardBlendAlbedoPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
		SHADER_PARAMETER(float, DiffuseReflectivityOverride)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardBlendAlbedoPS, "/Engine/Private/Lumen/LumenSceneUtils.usf", "LumenCardBlendAlbedoPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardBlendAlbedo, )
SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardBlendAlbedoPS::FParameters, PS)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void CombineLumenSceneLighting(
	FScene* Scene, 
	FViewInfo& View,
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef FinalLightingAtlas, 
	FRDGTextureRef OpacityAtlas, 
	FRDGTextureRef RadiosityAtlas, 
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		FLumenCardLightingEmissive* PassParameters = GraphBuilder.AllocParameters<FLumenCardLightingEmissive>();
		
		FVector2D DownsampledInputAtlasSize = FVector2D::ZeroVector;
		if (LumenSceneData.GetRadiosityAtlasSize() != LumenSceneData.GetPhysicalAtlasSize())
		{
			DownsampledInputAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
		}

		PassParameters->RenderTargets[0] = FRenderTargetBinding(FinalLightingAtlas, ERenderTargetLoadAction::ENoAction);
		PassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.CardPageParameters;
		PassParameters->VS.DownsampledInputAtlasSize = DownsampledInputAtlasSize;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->PS.RadiosityAtlas = RadiosityAtlas;
		PassParameters->PS.OpacityAtlas = OpacityAtlas;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LightingCombine"),
			PassParameters,
			ERDGPassFlags::Raster,
			[MaxAtlasSize = Scene->LumenSceneData->GetPhysicalAtlasSize(), PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
		{
			FLumenCardLightingInitializePS::FPermutationDomain PermutationVector;
			auto PixelShader = GlobalShaderMap->GetShader< FLumenCardLightingInitializePS >(PermutationVector);

			DrawQuadsToAtlas(MaxAtlasSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
		});
	}
}

void CopyLumenCardAtlas(
	FScene* Scene,
	FViewInfo& View,
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef SrcAtlas,
	FRDGTextureRef DstAtlas,
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext
)
{
	LLM_SCOPE_BYTAG(Lumen);
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardCopyAtlas* PassParameters = GraphBuilder.AllocParameters<FLumenCardCopyAtlas>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(DstAtlas, ERenderTargetLoadAction::ENoAction);
	PassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
	PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.CardPageParameters;
	PassParameters->VS.DownsampledInputAtlasSize = FVector2D::ZeroVector;
	PassParameters->PS.View = View.ViewUniformBuffer;
	PassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
	PassParameters->PS.SrcAtlas = SrcAtlas;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CopyLumenCardAtlas"),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = Scene->LumenSceneData->GetPhysicalAtlasSize(), PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
	{
		FLumenCardCopyAtlasPS::FPermutationDomain PermutationVector;
		auto PixelShader = GlobalShaderMap->GetShader< FLumenCardCopyAtlasPS >(PermutationVector);

		DrawQuadsToAtlas(MaxAtlasSize,
			PixelShader,
			PassParameters,
			GlobalShaderMap,
			TStaticBlendState<>::GetRHI(),
			RHICmdList);
	});
}

void ApplyLumenCardAlbedo(
	FScene* Scene,
	FViewInfo& View,
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FRDGTextureRef FinalLightingAtlas,
	FRDGTextureRef AlbedoAtlas,
	FRDGTextureRef EmissiveAtlas, 
	FGlobalShaderMap* GlobalShaderMap,
	const FLumenCardScatterContext& VisibleCardScatterContext
)
{
	LLM_SCOPE_BYTAG(Lumen);
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardBlendAlbedo* PassParameters = GraphBuilder.AllocParameters<FLumenCardBlendAlbedo>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(FinalLightingAtlas, ERenderTargetLoadAction::ENoAction);
	PassParameters->VS.LumenCardScene = LumenCardSceneUniformBuffer;
	PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.CardPageParameters;
	PassParameters->VS.DownsampledInputAtlasSize = FVector2D::ZeroVector;
	PassParameters->PS.View = View.ViewUniformBuffer;
	PassParameters->PS.LumenCardScene = LumenCardSceneUniformBuffer;
	PassParameters->PS.AlbedoAtlas = AlbedoAtlas;
	PassParameters->PS.EmissiveAtlas = EmissiveAtlas;
	PassParameters->PS.DiffuseReflectivityOverride = FMath::Clamp<float>(GLumenSceneSurfaceCacheDiffuseReflectivityOverride, 0.0f, 1.0f);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ApplyLumenCardAlbedo"),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = Scene->LumenSceneData->GetPhysicalAtlasSize(), PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
	{
		FLumenCardCopyAtlasPS::FPermutationDomain PermutationVector;
		auto PixelShader = GlobalShaderMap->GetShader< FLumenCardBlendAlbedoPS >(PermutationVector);

		DrawQuadsToAtlas(MaxAtlasSize,
			PixelShader,
			PassParameters,
			GlobalShaderMap,
			TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_Source1Color>::GetRHI(),	// Add Emissive, multiply accumulated lighting with Albedo which is output to SV_Target1 (dual source blending)
			RHICmdList);
	});
}

DECLARE_GPU_STAT(LumenSceneLighting);

void FDeferredShadingSceneRenderer::RenderLumenSceneLighting(
	FRDGBuilder& GraphBuilder,
	FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::RenderLumenSceneLighting);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const bool bAnyLumenEnabled = GetViewPipelineState(Views[0]).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen 
		|| GetViewPipelineState(Views[0]).ReflectionsMethod == EReflectionsMethod::Lumen;

	if (bAnyLumenEnabled)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RenderLumenSceneLighting);
		QUICK_SCOPE_CYCLE_COUNTER(RenderLumenSceneLighting);
		RDG_EVENT_SCOPE(GraphBuilder, "LumenSceneLighting");
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenSceneLighting);

		FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
		FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, Views[0]);

		if (LumenSceneData.GetNumCardPages() > 0)
		{
			FRDGTextureRef RadiosityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityAtlas, TEXT("Lumen.RadiosityAtlas"));

			if (LumenSceneData.bDebugClearAllCachedState)
			{
				AddClearRenderTargetPass(GraphBuilder, RadiosityAtlas);
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.FinalLightingAtlas);

				if (Lumen::UseIrradianceAtlas(View))
				{
					AddClearRenderTargetPass(GraphBuilder, TracingInputs.IrradianceAtlas);
				}
				if (Lumen::UseIndirectIrradianceAtlas(View))
				{
					AddClearRenderTargetPass(GraphBuilder, TracingInputs.IndirectIrradianceAtlas);
				}
			}

			RenderRadiosityForLumenScene(GraphBuilder, TracingInputs, GlobalShaderMap, RadiosityAtlas);

			FLumenCardScatterContext DirectLightingCardScatterContext;
			extern float GLumenSceneCardDirectLightingUpdateFrequencyScale;

			// Build the indirect args to write to the card faces we are going to update direct lighting for this frame
			DirectLightingCardScatterContext.Build(
				GraphBuilder,
				View,
				LumenSceneData,
				LumenCardRenderer,
				TracingInputs.LumenCardSceneUniformBuffer,
				/*bBuildCardTiles*/ true,
				ECullCardsMode::OperateOnSceneForceUpdateForCardPagesToRender,
				GLumenSceneCardDirectLightingUpdateFrequencyScale,
				FCullCardsShapeParameters(),
				ECullCardsShapeType::None);

			CombineLumenSceneLighting(
				Scene,
				View,
				GraphBuilder,
				TracingInputs.LumenCardSceneUniformBuffer,
				TracingInputs.FinalLightingAtlas,
				TracingInputs.OpacityAtlas,
				RadiosityAtlas,
				GlobalShaderMap, 
				DirectLightingCardScatterContext);

			if (Lumen::UseIndirectIrradianceAtlas(View))
			{
				CopyLumenCardAtlas(
					Scene,
					View,
					GraphBuilder,
					TracingInputs.LumenCardSceneUniformBuffer,
					TracingInputs.FinalLightingAtlas,
					TracingInputs.IndirectIrradianceAtlas,
					GlobalShaderMap,
					DirectLightingCardScatterContext);
			}

			RenderDirectLightingForLumenScene(
				GraphBuilder,
				TracingInputs,
				GlobalShaderMap,
				DirectLightingCardScatterContext);

			if (Lumen::UseIrradianceAtlas(View))
			{
				CopyLumenCardAtlas(
					Scene,
					View,
					GraphBuilder,
					TracingInputs.LumenCardSceneUniformBuffer,
					TracingInputs.FinalLightingAtlas,
					TracingInputs.IrradianceAtlas,
					GlobalShaderMap,
					DirectLightingCardScatterContext);
			}

			FRDGTextureRef AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas, TEXT("Lumen.AlbedoAtlas"));
			FRDGTextureRef EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas, TEXT("Lumen.EmissiveAtlas"));
			ApplyLumenCardAlbedo(
				Scene,
				View,
				GraphBuilder,
				TracingInputs.LumenCardSceneUniformBuffer,
				TracingInputs.FinalLightingAtlas,
				AlbedoAtlas,
				EmissiveAtlas,
				GlobalShaderMap,
				DirectLightingCardScatterContext);

			LumenSceneData.bFinalLightingAtlasContentsValid = true;

			LumenSceneData.FinalLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.FinalLightingAtlas);
			if (Lumen::UseIrradianceAtlas(View))
			{
				LumenSceneData.IrradianceAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.IrradianceAtlas);
			}
			if (Lumen::UseIndirectIrradianceAtlas(View))
			{
				LumenSceneData.IndirectIrradianceAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.IndirectIrradianceAtlas);
			}

			LumenSceneData.RadiosityAtlas = GraphBuilder.ConvertToExternalTexture(RadiosityAtlas);
		}

		ComputeLumenSceneVoxelLighting(GraphBuilder, TracingInputs, GlobalShaderMap);

		ComputeLumenTranslucencyGIVolume(GraphBuilder, TracingInputs, GlobalShaderMap);
	}
}
