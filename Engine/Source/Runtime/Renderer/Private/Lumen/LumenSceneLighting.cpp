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

float GLumenSceneSurfaceCacheDiffuseReflectivityOverride = 0;
FAutoConsoleVariableRef CVarLumenSceneDiffuseReflectivityOverride(
	TEXT("r.LumenScene.Lighting.DiffuseReflectivityOverride"),
	GLumenSceneSurfaceCacheDiffuseReflectivityOverride,
	TEXT(""),
	ECVF_RenderThreadSafe
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
	TEXT("GPU print out Lumen lighting update stats."),
	ECVF_RenderThreadSafe
);

namespace LumenSceneLighting
{
	bool UseFeedback()
	{
		return Lumen::UseHardwareRayTracing() && GLumenSceneLightingFeedback != 0;
	}
}

bool Lumen::UseHardwareRayTracedSceneLighting()
{
	return Lumen::UseHardwareRayTracedDirectLighting() || Lumen::UseHardwareRayTracedRadiosity();
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
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWQuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWQuadData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, MaxQuadsPerScatterInstance)
		SHADER_PARAMETER(uint32, CardScatterInstanceIndex)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardPageIndexData)
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
		return 64;
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
	const FLumenCardUpdateContext& CardUpdateContext,
	bool InBuildCardTiles,
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
		CardUpdateContext,
		InBuildCardTiles,
		ScatterInstances,
		1);
}

void FLumenCardScatterContext::Build(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData,
	const FLumenCardRenderer& LumenCardRenderer,
	TRDGUniformBufferRef<FLumenCardScene> LumenCardSceneUniformBuffer,
	const FLumenCardUpdateContext& CardUpdateContext,
	bool InBuildCardTiles,
	const TArray<FLumenCardScatterInstance, SceneRenderingAllocator>& ScatterInstances,
	int32 InMaxScatterInstanceCount)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Culling %d instances", ScatterInstances.Num());

	MaxScatterInstanceCount = InMaxScatterInstanceCount;
	MaxQuadsPerScatterInstance = LumenSceneData.GetNumCardPages();
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
			PassParameters->IndirectArgs = CardUpdateContext.CardPageIndexIndirectArgs;
			PassParameters->RWQuadAllocator = QuadAllocatorUAV;
			PassParameters->RWQuadData = QuadDataUAV;
			PassParameters->CardScatterInstanceIndex = CardScatterInstanceIndex;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->LumenCardScene = LumenCardSceneUniformBuffer;
			PassParameters->ShapeParameters = ScatterInstance.ShapeParameters;
			PassParameters->MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
			PassParameters->CardPageIndexAllocator = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexAllocator);
			PassParameters->CardPageIndexData = GraphBuilder.CreateSRV(CardUpdateContext.CardPageIndexData);

			FCullCardPagesToShapeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FCullCardPagesToShapeCS::FShapeType>((int32)ScatterInstance.ShapeType);
			auto ComputeShader = View.ShaderMap->GetShader<FCullCardPagesToShapeCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CullCardPagesToShape"),
				ComputeShader,
				PassParameters,
				CardUpdateContext.CardPageIndexIndirectArgs,
				0);
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

	PassParameters->RenderTargets[0] = FRenderTargetBinding(TracingInputs.FinalLightingAtlas, ERenderTargetLoadAction::ELoad);
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
		[MaxAtlasSize = Scene->LumenSceneData->GetPhysicalAtlasSize(), PassParameters, GlobalShaderMap = View.ShaderMap](FRHICommandList& RHICmdList)
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
		RDG_EVENT_SCOPE(GraphBuilder, "LumenSceneLighting%s", View.bLumenPropagateGlobalLightingChange ? TEXT(" PROPAGATE GLOBAL CHANGE!") : TEXT(""));
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenSceneLighting);

		LumenSceneData.FrameTemporaries = FLumenSceneFrameTemporaries();
		LumenSceneData.IncrementSurfaceCacheUpdateFrameIndex();

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
				IndirectLightingCardUpdateContext);

			LumenSceneData.DirectLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.DirectLightingAtlas);
			LumenSceneData.IndirectLightingAtlas = GraphBuilder.ConvertToExternalTexture(TracingInputs.IndirectLightingAtlas);
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDirectLightingCardPageIndexIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectLightingCardPageIndexIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DirectLightingCardPageIndexAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, IndirectLightingCardPageIndexAllocator)
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

	SetLightingUpdateAtlasSize(LumenSceneData.GetPhysicalAtlasSize(), GLumenDirectLightingUpdateFactor, DirectLightingCardUpdateContext);
	SetLightingUpdateAtlasSize(LumenSceneData.GetPhysicalAtlasSize(), GLumenRadiosityUpdateFactor, IndirectLightingCardUpdateContext);

	DirectLightingCardUpdateContext.CardPageIndexAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.DirectLightingCardPageIndexAllocator"));
	DirectLightingCardUpdateContext.CardPageIndexData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::RoundUpToPowerOfTwo(NumCardPages)), TEXT("Lumen.DirectLightingCardPageIndexData"));
	DirectLightingCardUpdateContext.CardPageIndexIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.DirectLightingCardPageIndexIndirectArgs"));

	IndirectLightingCardUpdateContext.CardPageIndexAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.IndirectLightingCardPageIndexAllocator"));
	IndirectLightingCardUpdateContext.CardPageIndexData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FMath::RoundUpToPowerOfTwo(NumCardPages)), TEXT("Lumen.IndirectLightingCardPageIndexData"));
	IndirectLightingCardUpdateContext.CardPageIndexIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.IndirectLightingCardPageIndexIndirectArgs"));

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
		PassParameters->RWDirectLightingCardPageIndexIndirectArgs = GraphBuilder.CreateUAV(DirectLightingCardUpdateContext.CardPageIndexIndirectArgs);
		PassParameters->RWIndirectLightingCardPageIndexIndirectArgs = GraphBuilder.CreateUAV(IndirectLightingCardUpdateContext.CardPageIndexIndirectArgs);
		PassParameters->DirectLightingCardPageIndexAllocator = GraphBuilder.CreateSRV(DirectLightingCardUpdateContext.CardPageIndexAllocator);
		PassParameters->IndirectLightingCardPageIndexAllocator = GraphBuilder.CreateSRV(IndirectLightingCardUpdateContext.CardPageIndexAllocator);

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