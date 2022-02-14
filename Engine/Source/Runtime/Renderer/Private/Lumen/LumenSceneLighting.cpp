// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneLighting.cpp
=============================================================================*/

#include "LumenSceneLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "LumenMeshCards.h"
#include "LumenRadianceCache.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "LumenTracingUtils.h"
#include "ShaderPrintParameters.h"

int32 GLumenSceneLightingForceFullUpdate = 0;
FAutoConsoleVariableRef CVarLumenSceneLightingForceFullUpdate(
	TEXT("r.LumenScene.Lighting.ForceLightingUpdate"),
	GLumenSceneLightingForceFullUpdate,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneLightingFeedback = 1;
FAutoConsoleVariableRef CVarLumenSceneLightingFeedback(
	TEXT("r.LumenScene.Lighting.Feedback"),
	GLumenSceneLightingFeedback,
	TEXT("Whether to prioritize surface cache lighting updates based on the feedback."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenDirectLightingUpdateFactor = 16;
FAutoConsoleVariableRef CVarLumenSceneDirectLightingUpdateFactor(
	TEXT("r.LumenScene.DirectLighting.UpdateFactor"),
	GLumenDirectLightingUpdateFactor,
	TEXT("Controls for how many texels direct lighting will be updated every frame. Texels = SurfaceCacheTexels / Factor."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenRadiosityUpdateFactor = 64;
FAutoConsoleVariableRef CVarLumenSceneRadiosityUpdateFactor(
	TEXT("r.LumenScene.Radiosity.UpdateFactor"),
	GLumenRadiosityUpdateFactor,
	TEXT("Controls for how many texels radiosity will be updated every frame. Texels = SurfaceCacheTexels / Factor."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenLightingStats = 0;
FAutoConsoleVariableRef CVarLumenSceneLightingStats(
	TEXT("r.LumenScene.Lighting.Stats"),
	GLumenLightingStats,
	TEXT("GPU print out Lumen lighting update stats. Requires r.ShaderPrintEnable 1."),
	ECVF_RenderThreadSafe
);

namespace LumenSceneLighting
{
	bool UseFeedback()
	{
		return Lumen::UseHardwareRayTracing() && GLumenSceneLightingFeedback != 0;
	}
}

bool Lumen::UseHardwareRayTracedSceneLighting(const FSceneViewFamily& ViewFamily)
{
	return Lumen::UseHardwareRayTracedDirectLighting() || Lumen::UseHardwareRayTracedRadiosity(ViewFamily);
}

namespace LumenCardUpdateContext
{
	// Must match LumenSceneLighting.usf
	constexpr uint32 CARD_UPDATE_CONTEXT_MAX = 2;
	constexpr uint32 PRIORITY_HISTOGRAM_SIZE = 128;
	constexpr uint32 MAX_UPDATE_BUCKET_STRIDE = 2;
	constexpr uint32 CARD_PAGE_TILE_ALLOCATOR_STRIDE = 2;
};

void SetLightingUpdateAtlasSize(FIntPoint PhysicalAtlasSize, int32 UpdateFactor, FLumenCardUpdateContext& Context)
{
	Context.UpdateAtlasSize = FIntPoint(0, 0);
	Context.MaxUpdateTiles = 0;
	Context.UpdateFactor = FMath::Clamp(UpdateFactor, 1, 1024);

	if (!Lumen::IsSurfaceCacheFrozen())
	{
		if (GLumenSceneLightingForceFullUpdate != 0)
		{
			Context.UpdateFactor = 1;
		}

		const float MultPerComponent = 1.0f / FMath::Sqrt((float)Context.UpdateFactor);

		FIntPoint UpdateAtlasSize;
		UpdateAtlasSize.X = FMath::DivideAndRoundUp<uint32>(PhysicalAtlasSize.X * MultPerComponent + 0.5f, Lumen::CardTileSize) * Lumen::CardTileSize;
		UpdateAtlasSize.Y = FMath::DivideAndRoundUp<uint32>(PhysicalAtlasSize.Y * MultPerComponent + 0.5f, Lumen::CardTileSize) * Lumen::CardTileSize;

		// Update at least one full res card page so that we don't get stuck
		UpdateAtlasSize.X = FMath::Max<int32>(UpdateAtlasSize.X, Lumen::PhysicalPageSize);
		UpdateAtlasSize.Y = FMath::Max<int32>(UpdateAtlasSize.Y, Lumen::PhysicalPageSize);

		const FIntPoint UpdateAtlasSizeInTiles = UpdateAtlasSize / Lumen::CardTileSize;

		Context.UpdateAtlasSize = UpdateAtlasSize;
		Context.MaxUpdateTiles = UpdateAtlasSizeInTiles.X * UpdateAtlasSizeInTiles.Y;
	}
}

IMPLEMENT_GLOBAL_SHADER(FClearLumenCardsPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "ClearLumenCardsPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FCopyCardCaptureLightingToAtlasPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "CopyCardCaptureLightingToAtlasPS", SF_Pixel);

bool FRasterizeToCardsVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportLumenGI(Parameters.Platform);
}

IMPLEMENT_GLOBAL_SHADER(FRasterizeToCardsVS,"/Engine/Private/Lumen/LumenSceneLighting.usf","RasterizeToCardsVS",SF_Vertex);

class FLumenCardCombineLightingPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardCombineLightingPS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardCombineLightingPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DirectLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER(float, DiffuseReflectivityOverride)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCombineLightingPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "CombineLumenSceneLightingPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardCombineLighting, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardCombineLightingPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void Lumen::CombineLumenSceneLighting(
	FScene* Scene, 
	const FViewInfo& View,
	FRDGBuilder& GraphBuilder,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenCardUpdateContext& CardUpdateContext)
{
	LLM_SCOPE_BYTAG(Lumen);
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardCombineLighting* PassParameters = GraphBuilder.AllocParameters<FLumenCardCombineLighting>();

	PassParameters->RenderTargets[0] = FRenderTargetBinding(TracingInputs.FinalLightingAtlas, ERenderTargetLoadAction::ELoad);
	PassParameters->VS.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
	PassParameters->VS.DrawIndirectArgs = CardUpdateContext.DrawCardPageIndicesIndirectArgs;
	PassParameters->VS.CardPageIndexAllocator = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexAllocator);
	PassParameters->VS.CardPageIndexData = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexData);
	PassParameters->VS.IndirectLightingAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
	PassParameters->PS.View = View.ViewUniformBuffer;
	PassParameters->PS.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
	PassParameters->PS.AlbedoAtlas = TracingInputs.AlbedoAtlas;
	PassParameters->PS.EmissiveAtlas = TracingInputs.EmissiveAtlas;
	PassParameters->PS.DirectLightingAtlas = TracingInputs.DirectLightingAtlas;
	PassParameters->PS.IndirectLightingAtlas = TracingInputs.IndirectLightingAtlas;
	PassParameters->PS.OpacityAtlas = TracingInputs.OpacityAtlas;
	PassParameters->PS.DiffuseReflectivityOverride = LumenSurfaceCache::GetDiffuseReflectivityOverride();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CombineLighting"),
		PassParameters,
		ERDGPassFlags::Raster,
		[ViewportSize = Scene->LumenSceneData->GetPhysicalAtlasSize(), PassParameters, GlobalShaderMap = View.ShaderMap](FRHICommandList& RHICmdList)
	{
		auto PixelShader = GlobalShaderMap->GetShader<FLumenCardCombineLightingPS>();
		auto VertexShader = GlobalShaderMap->GetShader<FRasterizeToCardsVS>();

		DrawQuadsToAtlas(
			ViewportSize,
			VertexShader,
			PixelShader,
			PassParameters,
			GlobalShaderMap,
			TStaticBlendState<>::GetRHI(),
			RHICmdList,
			[](FRHICommandList& RHICmdList, TShaderRefBase<FLumenCardCombineLightingPS, FShaderMapPointerTable> Shader, FRHIPixelShader* ShaderRHI, const typename FLumenCardCombineLightingPS::FParameters& Parameters) {},
			PassParameters->VS.DrawIndirectArgs,
			0);
	});
}

DECLARE_GPU_STAT(LumenSceneLighting);

void FDeferredShadingSceneRenderer::RenderLumenSceneLighting(
	FRDGBuilder& GraphBuilder,
	FViewInfo& View,
	FLumenSceneFrameTemporaries& FrameTemporaries)
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
		RDG_EVENT_SCOPE(GraphBuilder, "LumenSceneLighting%s", View.bLumenPropagateGlobalLightingChange ? TEXT(" PROPAGATE GLOBAL CHANGE!") : TEXT(""));
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenSceneLighting);

		LumenSceneData.IncrementSurfaceCacheUpdateFrameIndex();

		FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
		FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, Views[0], FrameTemporaries);

		if (LumenSceneData.GetNumCardPages() > 0)
		{
			if (LumenSceneData.bDebugClearAllCachedState)
			{
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.DirectLightingAtlas);
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.IndirectLightingAtlas);
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.RadiosityNumFramesAccumulatedAtlas);
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.FinalLightingAtlas);
			}

			FLumenCardUpdateContext DirectLightingCardUpdateContext;
			FLumenCardUpdateContext IndirectLightingCardUpdateContext;
			Lumen::BuildCardUpdateContext(
				GraphBuilder,
				View,
				LumenSceneData,
				TracingInputs.LumenCardSceneUniformBuffer,
				DirectLightingCardUpdateContext,
				IndirectLightingCardUpdateContext);

			RenderDirectLightingForLumenScene(
				GraphBuilder,
				TracingInputs,
				GlobalShaderMap,
				DirectLightingCardUpdateContext);

			RenderRadiosityForLumenScene(
				GraphBuilder,
				TracingInputs,
				GlobalShaderMap,
				TracingInputs.IndirectLightingAtlas,
				TracingInputs.RadiosityNumFramesAccumulatedAtlas,
				IndirectLightingCardUpdateContext);

			LumenSceneData.DirectLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.DirectLightingAtlas);
			LumenSceneData.IndirectLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.IndirectLightingAtlas);
			LumenSceneData.RadiosityNumFramesAccumulatedAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.RadiosityNumFramesAccumulatedAtlas);
			LumenSceneData.FinalLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.FinalLightingAtlas);

			LumenSceneData.bFinalLightingAtlasContentsValid = true;
		}

		ComputeLumenSceneVoxelLighting(GraphBuilder, TracingInputs, GlobalShaderMap);

		ComputeLumenTranslucencyGIVolume(GraphBuilder, TracingInputs, GlobalShaderMap);
	}
}

class FClearCardUpdateContextCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearCardUpdateContextCS);
	SHADER_USE_PARAMETER_STRUCT(FClearCardUpdateContextCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardPageTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPriorityHistogram)
	END_SHADER_PARAMETER_STRUCT()

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
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearCardUpdateContextCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "ClearCardUpdateContextCS", SF_Compute);

class FBuildPageUpdatePriorityHistogramCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildPageUpdatePriorityHistogramCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildPageUpdatePriorityHistogramCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPriorityHistogram)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageLastUsedBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageHighResLastUsedBuffer)
		SHADER_PARAMETER(uint32, SurfaceCacheUpdateFrameIndex)
		SHADER_PARAMETER(uint32, FreezeUpdateFrame)
		SHADER_PARAMETER(uint32, CardPageNum)
		SHADER_PARAMETER(float, FirstClipmapWorldExtentRcp)
		SHADER_PARAMETER(float, DirectLightingUpdateFactor)
		SHADER_PARAMETER(float, IndirectLightingUpdateFactor)
	END_SHADER_PARAMETER_STRUCT()

	class FSurfaceCacheFeedback : SHADER_PERMUTATION_BOOL("SURFACE_CACHE_FEEDBACK");
	using FPermutationDomain = TShaderPermutationDomain<FSurfaceCacheFeedback>;

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
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildPageUpdatePriorityHistogramCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "BuildPageUpdatePriorityHistogramCS", SF_Compute);

class FSelectMaxUpdateBucketCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSelectMaxUpdateBucketCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectMaxUpdateBucketCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWMaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PriorityHistogram)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, MaxDirectLightingTilesToUpdate)
		SHADER_PARAMETER(uint32, MaxIndirectLightingTilesToUpdate)
		SHADER_PARAMETER(uint32, SurfaceCacheUpdateFrameIndex)
		SHADER_PARAMETER(uint32, FreezeUpdateFrame)
	END_SHADER_PARAMETER_STRUCT()

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
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FSelectMaxUpdateBucketCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "SelectMaxUpdateBucketCS", SF_Compute);

class FBuildCardsUpdateListCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildCardsUpdateListCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildCardsUpdateListCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDirectLightingCardPageIndexData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectLightingCardPageIndexData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardPageTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaxUpdateBucket)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, LumenCardDataBuffer)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, RWLumenCardPageDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageLastUsedBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageHighResLastUsedBuffer)
		SHADER_PARAMETER(uint32, SurfaceCacheUpdateFrameIndex)
		SHADER_PARAMETER(uint32, FreezeUpdateFrame)
		SHADER_PARAMETER(uint32, CardPageNum)
		SHADER_PARAMETER(float, FirstClipmapWorldExtentRcp)
		SHADER_PARAMETER(uint32, MaxDirectLightingTilesToUpdate)
		SHADER_PARAMETER(uint32, MaxIndirectLightingTilesToUpdate)
		SHADER_PARAMETER(float, DirectLightingUpdateFactor)
		SHADER_PARAMETER(float, IndirectLightingUpdateFactor)
	END_SHADER_PARAMETER_STRUCT()

	class FSurfaceCacheFeedback : SHADER_PERMUTATION_BOOL("SURFACE_CACHE_FEEDBACK");
	using FPermutationDomain = TShaderPermutationDomain<FSurfaceCacheFeedback>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("USE_LUMEN_CARD_DATA_BUFFER"), 1);
		OutEnvironment.SetDefine(TEXT("USE_RW_LUMEN_CARD_PAGE_DATA_BUFFER"), 1);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildCardsUpdateListCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "BuildCardsUpdateListCS", SF_Compute);

class FSetCardPageIndexIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetCardPageIndexIndirectArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSetCardPageIndexIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDirectLightingDrawCardPageIndicesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDirectLightingDispatchCardPageIndicesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectLightingDrawCardPageIndicesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectLightingDispatchCardPageIndicesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, IndirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER(uint32, VertexCountPerInstanceIndirect)
	END_SHADER_PARAMETER_STRUCT()

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
		return 64;
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetCardPageIndexIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "SetCardPageIndexIndirectArgsCS", SF_Compute);

class FLumenSceneLightingStatsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneLightingStatsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneLightingStatsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, CardPageNum)
		SHADER_PARAMETER(uint32, LightingStatMode)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, IndirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PriorityHistogram)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaxUpdateBucket)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageTileAllocator)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneLightingStatsCS, "/Engine/Private/Lumen/LumenSceneLightingDebug.usf", "LumenSceneLightingStatsCS", SF_Compute);

void Lumen::BuildCardUpdateContext(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	FLumenCardUpdateContext& DirectLightingCardUpdateContext,
	FLumenCardUpdateContext& IndirectLightingCardUpdateContext)
{
	RDG_EVENT_SCOPE(GraphBuilder, "BuildCardUpdateContext");

	FRDGBufferRef CardPageLastUsedBuffer = nullptr;
	FRDGBufferRef CardPageHighResLastUsedBuffer = nullptr;
	const bool bUseFeedback = LumenSceneData.CardPageLastUsedBuffer && LumenSceneData.CardPageHighResLastUsedBuffer && LumenSceneLighting::UseFeedback();
	if (bUseFeedback)
	{
		CardPageLastUsedBuffer = GraphBuilder.RegisterExternalBuffer(LumenSceneData.CardPageLastUsedBuffer);
		CardPageHighResLastUsedBuffer = GraphBuilder.RegisterExternalBuffer(LumenSceneData.CardPageHighResLastUsedBuffer);
	}

	const int32 NumCardPages = LumenSceneData.GetNumCardPages();
	const uint32 UpdateFrameIndex = LumenSceneData.GetSurfaceCacheUpdateFrameIndex();
	const uint32 FreezeUpdateFrame = Lumen::IsSurfaceCacheUpdateFrameFrozen() ? 1 : 0;
	const float FirstClipmapWorldExtentRcp = 1.0f / Lumen::GetFirstClipmapWorldExtent();
	const float LumenSceneLightingUpdateSpeed = FMath::Clamp<float>(View.FinalPostProcessSettings.LumenSceneLightingUpdateSpeed, .5f, 16.0f);

	SetLightingUpdateAtlasSize(LumenSceneData.GetPhysicalAtlasSize(), FMath::RoundToInt(GLumenDirectLightingUpdateFactor / LumenSceneLightingUpdateSpeed), DirectLightingCardUpdateContext);
	SetLightingUpdateAtlasSize(LumenSceneData.GetPhysicalAtlasSize(), FMath::RoundToInt(GLumenRadiosityUpdateFactor / LumenSceneLightingUpdateSpeed), IndirectLightingCardUpdateContext);

	DirectLightingCardUpdateContext.CardPageIndexAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.DirectLightingCardPageIndexAllocator"));
	DirectLightingCardUpdateContext.CardPageIndexData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::RoundUpToPowerOfTwo(NumCardPages)), TEXT("Lumen.DirectLightingCardPageIndexData"));
	DirectLightingCardUpdateContext.DrawCardPageIndicesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("Lumen.DirectLighting.DrawCardPageIndicesIndirectArgs"));
	DirectLightingCardUpdateContext.DispatchCardPageIndicesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(FLumenCardUpdateContext::MAX), TEXT("Lumen.DirectLighting.DispatchCardPageIndicesIndirectArgs"));

	IndirectLightingCardUpdateContext.CardPageIndexAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.IndirectLightingCardPageIndexAllocator"));
	IndirectLightingCardUpdateContext.CardPageIndexData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::RoundUpToPowerOfTwo(NumCardPages)), TEXT("Lumen.IndirectLightingCardPageIndexData"));
	IndirectLightingCardUpdateContext.DrawCardPageIndicesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.IndirectLighting.DrawCardPageIndicesIndirectArgs"));
	IndirectLightingCardUpdateContext.DispatchCardPageIndicesIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(FLumenCardUpdateContext::MAX), TEXT("Lumen.IndirectLighting.DispatchCardPageIndicesIndirectArgs"));

	FRDGBufferRef PriorityHistogram = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LumenCardUpdateContext::CARD_UPDATE_CONTEXT_MAX * LumenCardUpdateContext::PRIORITY_HISTOGRAM_SIZE), TEXT("Lumen.PriorityHistogram"));
	FRDGBufferRef MaxUpdateBucket = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LumenCardUpdateContext::CARD_UPDATE_CONTEXT_MAX * LumenCardUpdateContext::MAX_UPDATE_BUCKET_STRIDE), TEXT("Lumen.MaxUpdateBucket"));
	FRDGBufferRef CardPageTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LumenCardUpdateContext::CARD_UPDATE_CONTEXT_MAX * LumenCardUpdateContext::CARD_PAGE_TILE_ALLOCATOR_STRIDE), TEXT("Lumen.CardPageTileAllocator"));

	// Batch clear all resources required for the subsequent card context update pass
	{
		FClearCardUpdateContextCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearCardUpdateContextCS::FParameters>();
		PassParameters->RWDirectLightingCardPageIndexAllocator = GraphBuilder.CreateUAV(DirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->RWIndirectLightingCardPageIndexAllocator = GraphBuilder.CreateUAV(IndirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->RWMaxUpdateBucket = GraphBuilder.CreateUAV(MaxUpdateBucket);
		PassParameters->RWCardPageTileAllocator = GraphBuilder.CreateUAV(CardPageTileAllocator);
		PassParameters->RWPriorityHistogram = GraphBuilder.CreateUAV(PriorityHistogram);

		auto ComputeShader = View.ShaderMap->GetShader<FClearCardUpdateContextCS>();

		const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(LumenCardUpdateContext::CARD_UPDATE_CONTEXT_MAX * LumenCardUpdateContext::PRIORITY_HISTOGRAM_SIZE, FClearCardUpdateContextCS::GetGroupSize()), 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearCardUpdateContext"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	// Prepare update priority histogram
	{
		FBuildPageUpdatePriorityHistogramCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildPageUpdatePriorityHistogramCS::FParameters>();
		PassParameters->RWPriorityHistogram = GraphBuilder.CreateUAV(PriorityHistogram);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->CardPageLastUsedBuffer = CardPageLastUsedBuffer ? GraphBuilder.CreateSRV(CardPageLastUsedBuffer) : nullptr;
		PassParameters->CardPageHighResLastUsedBuffer = CardPageHighResLastUsedBuffer ? GraphBuilder.CreateSRV(CardPageHighResLastUsedBuffer) : nullptr;
		PassParameters->CardPageNum = NumCardPages;
		PassParameters->SurfaceCacheUpdateFrameIndex = UpdateFrameIndex;
		PassParameters->FreezeUpdateFrame = FreezeUpdateFrame;
		PassParameters->FirstClipmapWorldExtentRcp = FirstClipmapWorldExtentRcp;
		PassParameters->DirectLightingUpdateFactor = DirectLightingCardUpdateContext.UpdateFactor;
		PassParameters->IndirectLightingUpdateFactor = IndirectLightingCardUpdateContext.UpdateFactor;

		FBuildPageUpdatePriorityHistogramCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FBuildPageUpdatePriorityHistogramCS::FSurfaceCacheFeedback>(bUseFeedback);
		auto ComputeShader = View.ShaderMap->GetShader<FBuildPageUpdatePriorityHistogramCS>(PermutationVector);

		const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(LumenSceneData.GetNumCardPages(), FBuildPageUpdatePriorityHistogramCS::GetGroupSize()), 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildPageUpdatePriorityHistogram"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	// Compute prefix sum and pick max bucket
	{
		FSelectMaxUpdateBucketCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectMaxUpdateBucketCS::FParameters>();
		PassParameters->RWMaxUpdateBucket = GraphBuilder.CreateUAV(MaxUpdateBucket);
		PassParameters->PriorityHistogram = GraphBuilder.CreateSRV(PriorityHistogram);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->MaxDirectLightingTilesToUpdate = DirectLightingCardUpdateContext.MaxUpdateTiles;
		PassParameters->MaxIndirectLightingTilesToUpdate = IndirectLightingCardUpdateContext.MaxUpdateTiles;
		PassParameters->SurfaceCacheUpdateFrameIndex = UpdateFrameIndex;
		PassParameters->FreezeUpdateFrame = FreezeUpdateFrame;

		auto ComputeShader = View.ShaderMap->GetShader<FSelectMaxUpdateBucketCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Select max update bucket"),
			ComputeShader,
			PassParameters,
			FIntVector(2, 1, 1));
	}

	// Build list of tiles to update in this frame
	{
		// TODO: Remove this when everything is properly RDG'd
		AddPass(GraphBuilder, RDG_EVENT_NAME("TransitionLumenCardPageBuffer"), [CardPageBuffer = &LumenSceneData.CardPageBuffer](FRHICommandList& RHICmdList)
		{
			FRHITransitionInfo Transitions[1] =
			{
				FRHITransitionInfo(CardPageBuffer->UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
			};

			RHICmdList.Transition(Transitions);
		});

		FBuildCardsUpdateListCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildCardsUpdateListCS::FParameters>();
		PassParameters->RWDirectLightingCardPageIndexAllocator = GraphBuilder.CreateUAV(DirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->RWDirectLightingCardPageIndexData = GraphBuilder.CreateUAV(DirectLightingCardUpdateContext.CardPageIndexData);
		PassParameters->RWIndirectLightingCardPageIndexAllocator = GraphBuilder.CreateUAV(IndirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->RWIndirectLightingCardPageIndexData = GraphBuilder.CreateUAV(IndirectLightingCardUpdateContext.CardPageIndexData);
		PassParameters->RWCardPageTileAllocator = GraphBuilder.CreateUAV(CardPageTileAllocator);
		PassParameters->MaxUpdateBucket = GraphBuilder.CreateSRV(MaxUpdateBucket);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardDataBuffer = LumenSceneData.CardBuffer.SRV;
		PassParameters->RWLumenCardPageDataBuffer = LumenSceneData.CardPageBuffer.UAV;
		PassParameters->CardPageLastUsedBuffer = CardPageLastUsedBuffer ? GraphBuilder.CreateSRV(CardPageLastUsedBuffer) : nullptr;
		PassParameters->CardPageHighResLastUsedBuffer = CardPageHighResLastUsedBuffer ? GraphBuilder.CreateSRV(CardPageHighResLastUsedBuffer) : nullptr;
		PassParameters->CardPageNum = NumCardPages;
		PassParameters->SurfaceCacheUpdateFrameIndex = UpdateFrameIndex;
		PassParameters->FreezeUpdateFrame = FreezeUpdateFrame;
		PassParameters->FirstClipmapWorldExtentRcp = FirstClipmapWorldExtentRcp;
		PassParameters->MaxDirectLightingTilesToUpdate = DirectLightingCardUpdateContext.MaxUpdateTiles;
		PassParameters->MaxIndirectLightingTilesToUpdate = IndirectLightingCardUpdateContext.MaxUpdateTiles;
		PassParameters->DirectLightingUpdateFactor = DirectLightingCardUpdateContext.UpdateFactor;
		PassParameters->IndirectLightingUpdateFactor = IndirectLightingCardUpdateContext.UpdateFactor;

		FBuildCardsUpdateListCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FBuildCardsUpdateListCS::FSurfaceCacheFeedback>(bUseFeedback);
		auto ComputeShader = View.ShaderMap->GetShader<FBuildCardsUpdateListCS>(PermutationVector);

		const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(LumenSceneData.GetNumCardPages(), FBuildCardsUpdateListCS::GetGroupSize()), 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Build cards update list"),
			ComputeShader,
			PassParameters,
			GroupSize);

		// TODO: Remove this when everything is properly RDG'd
		AddPass(GraphBuilder, RDG_EVENT_NAME("TransitionLumenCardPageBuffer"), [CardPageBuffer = &LumenSceneData.CardPageBuffer](FRHICommandList& RHICmdList)
		{
			FRHITransitionInfo Transitions[1] =
			{
				FRHITransitionInfo(CardPageBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
			};

			RHICmdList.Transition(Transitions);
		});
	}

	// Setup indirect args
	{
		FSetCardPageIndexIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetCardPageIndexIndirectArgsCS::FParameters>();
		PassParameters->RWDirectLightingDrawCardPageIndicesIndirectArgs = GraphBuilder.CreateUAV(DirectLightingCardUpdateContext.DrawCardPageIndicesIndirectArgs);
		PassParameters->RWDirectLightingDispatchCardPageIndicesIndirectArgs = GraphBuilder.CreateUAV(DirectLightingCardUpdateContext.DispatchCardPageIndicesIndirectArgs);
		PassParameters->RWIndirectLightingDrawCardPageIndicesIndirectArgs = GraphBuilder.CreateUAV(IndirectLightingCardUpdateContext.DrawCardPageIndicesIndirectArgs);
		PassParameters->RWIndirectLightingDispatchCardPageIndicesIndirectArgs = GraphBuilder.CreateUAV(IndirectLightingCardUpdateContext.DispatchCardPageIndicesIndirectArgs);
		PassParameters->DirectLightingCardPageIndexAllocator = GraphBuilder.CreateSRV(DirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->IndirectLightingCardPageIndexAllocator = GraphBuilder.CreateSRV(IndirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;

		auto ComputeShader = View.ShaderMap->GetShader<FSetCardPageIndexIndirectArgsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetCardPageIndexIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	if (GLumenLightingStats != 0)
	{
		FLumenSceneLightingStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneLightingStatsCS::FParameters>();
		ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintUniformBuffer);
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
		PassParameters->DirectLightingCardPageIndexAllocator = GraphBuilder.CreateSRV(DirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->IndirectLightingCardPageIndexAllocator = GraphBuilder.CreateSRV(IndirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->PriorityHistogram = GraphBuilder.CreateSRV(PriorityHistogram);
		PassParameters->MaxUpdateBucket = GraphBuilder.CreateSRV(MaxUpdateBucket);
		PassParameters->CardPageTileAllocator = GraphBuilder.CreateSRV(CardPageTileAllocator);
		PassParameters->CardPageNum = LumenSceneData.GetNumCardPages();
		PassParameters->LightingStatMode = GLumenLightingStats;

		auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneLightingStatsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SceneLightingStats"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}
}