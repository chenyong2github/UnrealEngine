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

IMPLEMENT_GLOBAL_SHADER(FClearLumenCardsPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "ClearLumenCardsPS", SF_Pixel);

class FInitializeCardPageIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInitializeCardPageIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FInitializeCardPageIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDrawCardPagesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBuildTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, QuadAllocator)
		SHADER_PARAMETER(uint32, VertexCountPerInstanceIndirect)
		SHADER_PARAMETER(uint32, MaxScatterInstanceCount)
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

IMPLEMENT_GLOBAL_SHADER(FInitializeCardPageIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "InitializeCardPageIndirectArgsCS", SF_Compute);

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
		SHADER_PARAMETER(uint32, CardScatterInstanceIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardPagesToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardPagesToRenderHashMap)
		SHADER_PARAMETER(uint32, FrameId)
		SHADER_PARAMETER(float, CardLightingUpdateFrequencyScale)
		SHADER_PARAMETER(uint32, CardLightingUpdateMinFrequency)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCullCardsShapeParameters, ShapeParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FOperateOnCardPagesMode : SHADER_PERMUTATION_ENUM_CLASS("OPERATE_ON_CARD_TILES_MODE", ECullCardsMode);
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

IMPLEMENT_GLOBAL_SHADER(FCullCardPagesToShapeCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "CullCardPagesToShapeCS", SF_Compute);

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
		SHADER_PARAMETER(uint32, CardScatterInstanceIndex)
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

IMPLEMENT_GLOBAL_SHADER(FBuildCardTilesCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "BuildCardTilesCS", SF_Compute);

bool FRasterizeToCardsVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportLumenGI(Parameters.Platform);
}

bool FRasterizeToCardTilesVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportLumenGI(Parameters.Platform);
}

IMPLEMENT_GLOBAL_SHADER(FRasterizeToCardsVS,"/Engine/Private/Lumen/LumenSceneLighting.usf","RasterizeToCardsVS",SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FRasterizeToCardTilesVS,"/Engine/Private/Lumen/LumenSceneLighting.usf","RasterizeToCardTilesVS",SF_Vertex);

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
	TArray<FLumenCardScatterInstance, SceneRenderingAllocator> ScatterInstances;
	FLumenCardScatterInstance& ScatterInstance = ScatterInstances.AddDefaulted_GetRef();
	ScatterInstance.ShapeParameters = ShapeParameters;
	ScatterInstance.ShapeType = ShapeType;

	Build(GraphBuilder,
		View,
		LumenSceneData,
		LumenCardRenderer,
		LumenCardSceneUniformBuffer,
		InBuildCardTiles,
		InCardsCullMode,
		UpdateFrequencyScale,
		ScatterInstances,
		1);
}

void FLumenCardScatterContext::Build(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData,
	const FLumenCardRenderer& LumenCardRenderer,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	bool InBuildCardTiles,
	ECullCardsMode InCardsCullMode,
	float UpdateFrequencyScale,
	const TArray<FLumenCardScatterInstance, SceneRenderingAllocator>& ScatterInstances,
	int32 InMaxScatterInstanceCount)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Culling %d instances", ScatterInstances.Num());

	MaxScatterInstanceCount = InMaxScatterInstanceCount;
	CardsCullMode = InCardsCullMode;
	NumCardPagesToOperateOn = LumenSceneData.GetNumCardPages();

	if (CardsCullMode == ECullCardsMode::OperateOnCardPagesToRender)
	{
		NumCardPagesToOperateOn = LumenCardRenderer.CardPagesToRender.Num();
	}

	MaxQuadsPerScatterInstance = NumCardPagesToOperateOn;
	const int32 NumQuadsInBuffer = FMath::DivideAndRoundUp(MaxScatterInstanceCount * MaxQuadsPerScatterInstance, 1024) * 1024;

	const uint32 MaxCardTilesX = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().X, Lumen::CardTileSize);
	const uint32 MaxCardTilesY = FMath::DivideAndRoundUp<uint32>(LumenSceneData.GetPhysicalAtlasSize().Y, Lumen::CardTileSize);
	MaxCardTilesPerScatterInstance = MaxCardTilesX * MaxCardTilesY;
	const uint32 NumCardTilesInBuffer = MaxScatterInstanceCount * MaxCardTilesPerScatterInstance;

	FRDGBufferRef QuadAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxScatterInstanceCount), TEXT("Lumen.QuadAllocator"));
	FRDGBufferRef QuadDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumQuadsInBuffer), TEXT("Lumen.QuadDataBuffer"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(QuadAllocator), 0);

	CardPageParameters.QuadAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadAllocator, PF_R32_UINT));
	CardPageParameters.QuadData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadDataBuffer, PF_R32_UINT));
	CardPageParameters.MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;

	FRDGBufferRef CardTileAllocator = nullptr;
	FRDGBufferRef CardTileData = nullptr;
	if (InBuildCardTiles)
	{
		CardTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxScatterInstanceCount), TEXT("Lumen.CardTileAllocator"));
		CardTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumCardTilesInBuffer), TEXT("Lumen.CardTileData"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CardTileAllocator), 0);
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
		FRDGBufferUAVRef QuadAllocatorUAV = GraphBuilder.CreateUAV(QuadAllocator, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef QuadDataUAV = GraphBuilder.CreateUAV(QuadDataBuffer, ERDGUnorderedAccessViewFlags::SkipBarrier);

		for (int32 CardScatterInstanceIndex = 0; CardScatterInstanceIndex < ScatterInstances.Num(); ++CardScatterInstanceIndex)
		{
			const FLumenCardScatterInstance& ScatterInstance = ScatterInstances[CardScatterInstanceIndex];

			FCullCardPagesToShapeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullCardPagesToShapeCS::FParameters>();
			PassParameters->RWQuadAllocator = QuadAllocatorUAV;
			PassParameters->RWQuadData = QuadDataUAV;
			PassParameters->CardScatterInstanceIndex = CardScatterInstanceIndex;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
			PassParameters->ShapeParameters = ScatterInstance.ShapeParameters;
			PassParameters->MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
			PassParameters->NumCardPagesToRenderIndices = LumenCardRenderer.CardPagesToRender.Num();
			PassParameters->CardPagesToRenderIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LumenCardRenderer.CardPagesToRenderIndexBuffer, PF_R32_UINT));
			PassParameters->CardPagesToRenderHashMap = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LumenCardRenderer.CardPagesToRenderHashMapBuffer, PF_R32_UINT));
			PassParameters->FrameId = View.ViewState->GetFrameIndex();
			PassParameters->CardLightingUpdateFrequencyScale = GLumenSceneLightingForceFullUpdate ? 0.0f : UpdateFrequencyScale;
			PassParameters->CardLightingUpdateMinFrequency = GLumenSceneLightingForceFullUpdate ? 1 : GLumenSceneLightingMinUpdateFrequency;

			FCullCardPagesToShapeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCullCardPagesToShapeCS::FOperateOnCardPagesMode>(CardsCullMode);
			PermutationVector.Set<FCullCardPagesToShapeCS::FShapeType>((int32)ScatterInstance.ShapeType);
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
	}

	// Build card page indirect args
	{
		FRDGBufferRef DrawIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(MaxScatterInstanceCount), TEXT("Lumen.DrawCardPagesIndirectArgs"));
		FRDGBufferRef DispatchIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(MaxScatterInstanceCount), TEXT("Lumen.DispatchCardPagesIndirectArgs"));

		FInitializeCardPageIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeCardPageIndirectArgsCS::FParameters>();
		PassParameters->RWDrawCardPagesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DrawIndirectArgs));
		PassParameters->RWBuildTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DispatchIndirectArgs));
		PassParameters->QuadAllocator = CardPageParameters.QuadAllocator;
		PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;
		PassParameters->MaxScatterInstanceCount = MaxScatterInstanceCount;

		auto ComputeShader = View.ShaderMap->GetShader<FInitializeCardPageIndirectArgsCS>();

		const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(MaxScatterInstanceCount, FInitializeCardPageIndirectArgsCS::GetGroupSize());

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
		FRDGBufferUAVRef CardTileAllocatorUAV = GraphBuilder.CreateUAV(CardTileAllocator, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef CardTileDataUAV = GraphBuilder.CreateUAV(CardTileData, ERDGUnorderedAccessViewFlags::SkipBarrier);

		for (int32 CardScatterInstanceIndex = 0; CardScatterInstanceIndex < ScatterInstances.Num(); ++CardScatterInstanceIndex)
		{
			const FLumenCardScatterInstance& ScatterInstance = ScatterInstances[CardScatterInstanceIndex];

			FBuildCardTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildCardTilesCS::FParameters>();
			PassParameters->IndirectArgBuffer = CardPageParameters.DispatchIndirectArgs;
			PassParameters->RWCardTileAllocator = CardTileAllocatorUAV;
			PassParameters->RWCardTileData = CardTileDataUAV;
			PassParameters->QuadAllocator = CardPageParameters.QuadAllocator;
			PassParameters->QuadData = CardPageParameters.QuadData;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
			PassParameters->ShapeParameters = ScatterInstance.ShapeParameters;
			PassParameters->MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
			PassParameters->MaxCardTilesPerScatterInstance = MaxCardTilesPerScatterInstance;
			PassParameters->CardScatterInstanceIndex = CardScatterInstanceIndex;

			FBuildCardTilesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FBuildCardTilesCS::FShapeType>((int32)ScatterInstance.ShapeType);
			auto ComputeShader = View.ShaderMap->GetShader<FBuildCardTilesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("BuildCardTiles"),
				ComputeShader,
				PassParameters,
				CardPageParameters.DispatchIndirectArgs,
				CardScatterInstanceIndex * sizeof(FRHIDispatchIndirectParameters));
		}
	}

	// Build card tile indirect args
	if (InBuildCardTiles)
	{
		FRDGBufferRef DrawIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(MaxScatterInstanceCount), TEXT("Lumen.DrawCardTilesIndirectArgs"));
		FRDGBufferRef DispatchIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(MaxScatterInstanceCount), TEXT("Lumen.DispatchCardTilesIndirectArgs"));

		FInitializeCardPageIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeCardPageIndirectArgsCS::FParameters>();
		PassParameters->RWDrawCardPagesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DrawIndirectArgs));
		PassParameters->RWBuildTilesIndirectArgs = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DispatchIndirectArgs));
		PassParameters->QuadAllocator = CardTileParameters.CardTileAllocator;
		PassParameters->VertexCountPerInstanceIndirect = GRHISupportsRectTopology ? 3 : 6;
		PassParameters->MaxScatterInstanceCount = MaxCardTilesPerScatterInstance;

		auto ComputeShader = View.ShaderMap->GetShader<FInitializeCardPageIndirectArgsCS>();

		const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(MaxScatterInstanceCount, FInitializeCardPageIndirectArgsCS::GetGroupSize());

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

IMPLEMENT_GLOBAL_SHADER(FLumenCardCombineLightingPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "CombineLumenSceneLighting", SF_Pixel);

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
	const FLumenCardScatterContext& VisibleCardScatterContext)
{
	LLM_SCOPE_BYTAG(Lumen);
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	FLumenCardCombineLighting* PassParameters = GraphBuilder.AllocParameters<FLumenCardCombineLighting>();

	PassParameters->RenderTargets[0] = FRenderTargetBinding(TracingInputs.FinalLightingAtlas, ERenderTargetLoadAction::ENoAction);
	PassParameters->VS.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
	PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.CardPageParameters;
	PassParameters->VS.CardScatterInstanceIndex = 0;
	PassParameters->VS.IndirectLightingAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
	PassParameters->PS.View = View.ViewUniformBuffer;
	PassParameters->PS.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
	PassParameters->PS.AlbedoAtlas = TracingInputs.AlbedoAtlas;
	PassParameters->PS.EmissiveAtlas = TracingInputs.EmissiveAtlas;
	PassParameters->PS.DirectLightingAtlas = TracingInputs.DirectLightingAtlas;
	PassParameters->PS.IndirectLightingAtlas = TracingInputs.IndirectLightingAtlas;
	PassParameters->PS.OpacityAtlas = TracingInputs.OpacityAtlas;
	PassParameters->PS.DiffuseReflectivityOverride = FMath::Clamp<float>(GLumenSceneSurfaceCacheDiffuseReflectivityOverride, 0.0f, 1.0f);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CombineLighting"),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = Scene->LumenSceneData->GetPhysicalAtlasSize(), PassParameters, GlobalShaderMap = View.ShaderMap](FRHICommandListImmediate& RHICmdList)
	{
		auto PixelShader = GlobalShaderMap->GetShader<FLumenCardCombineLightingPS>();

		DrawQuadsToAtlas(MaxAtlasSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
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
			if (LumenSceneData.bDebugClearAllCachedState)
			{
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.DirectLightingAtlas);
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.IndirectLightingAtlas);
				AddClearRenderTargetPass(GraphBuilder, TracingInputs.FinalLightingAtlas);
			}

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
				Lumen::IsSurfaceCacheFrozen() ? ECullCardsMode::OperateOnEmptyList : ECullCardsMode::OperateOnSceneForceUpdateForCardPagesToRender,
				GLumenSceneCardDirectLightingUpdateFrequencyScale,
				FCullCardsShapeParameters(),
				ECullCardsShapeType::None);

			RenderDirectLightingForLumenScene(
				GraphBuilder,
				TracingInputs,
				GlobalShaderMap,
				DirectLightingCardScatterContext);

			RenderRadiosityForLumenScene(GraphBuilder, TracingInputs, GlobalShaderMap, TracingInputs.IndirectLightingAtlas);

			LumenSceneData.DirectLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.DirectLightingAtlas);
			LumenSceneData.IndirectLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.IndirectLightingAtlas);
			LumenSceneData.FinalLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.FinalLightingAtlas);

			LumenSceneData.bFinalLightingAtlasContentsValid = true;
		}

		ComputeLumenSceneVoxelLighting(GraphBuilder, TracingInputs, GlobalShaderMap);

		ComputeLumenTranslucencyGIVolume(GraphBuilder, TracingInputs, GlobalShaderMap);
	}
}
