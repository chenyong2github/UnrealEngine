// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneLighting.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "SceneTextureParameters.h"
#include "LumenCubeMapTree.h"
#include "LumenRadianceCache.h"

int32 GLumenSceneCardLightingForceFullUpdate = 0;
FAutoConsoleVariableRef CVarLumenSceneCardLightingForceFullUpdate(
	TEXT("r.LumenScene.CardLightingForceFullUpdate"),
	GLumenSceneCardLightingForceFullUpdate,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneCardLightingUpdateMinFrequency = 3;
FAutoConsoleVariableRef CVarLumenSceneCardLightingUpdateMinFrequency(
	TEXT("r.LumenScene.CardLightingUpdateMinFrequency"),
	GLumenSceneCardLightingUpdateMinFrequency,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneDiffuseReflectivityOverride = 0;
FAutoConsoleVariableRef CVarLumenSceneDiffuseReflectivityOverride(
	TEXT("r.LumenScene.DiffuseReflectivityOverride"),
	GLumenSceneDiffuseReflectivityOverride,
	TEXT(""),
	ECVF_RenderThreadSafe
);


namespace Lumen
{
	bool UseIrradianceAtlas()
	{
		return UseHardwareRayTracedReflections() &&
			(GetHardwareRayTracedReflectionsLightingMode() == EHardwareRayTracedReflectionsLightingMode::EvaluateMaterial);
	}

	bool UseIndirectIrradianceAtlas()
	{
		return UseHardwareRayTracedReflections() &&
			(GetHardwareRayTracedReflectionsLightingMode() == EHardwareRayTracedReflectionsLightingMode::EvaluateMaterialAndDirectLighting);
	}
}

FLumenCardTracingInputs::FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	LumenCardScene = LumenSceneData.UniformBuffer;

	if (LumenSceneData.Cards.Num() > 0)
	{
		FinalLightingAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.FinalLightingAtlas);
		OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
		DilatedDepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);

		auto RegisterOptionalAtlas = [&GraphBuilder](bool (*UseAtlas)(), TRefCountPtr<IPooledRenderTarget> Atlas) {
			return UseAtlas() ? GraphBuilder.RegisterExternalTexture(Atlas) : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		};
		IrradianceAtlas = RegisterOptionalAtlas(Lumen::UseIrradianceAtlas, LumenSceneData.IrradianceAtlas);
		IndirectIrradianceAtlas = RegisterOptionalAtlas(Lumen::UseIndirectIrradianceAtlas, LumenSceneData.IndirectIrradianceAtlas);
	}
	else
	{
		FinalLightingAtlas = IrradianceAtlas = IndirectIrradianceAtlas = OpacityAtlas = DilatedDepthAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	}

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
}

void FLumenCardTracingInputs::ExtractToScene(FRDGBuilder& GraphBuilder, FScene* Scene, FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	ConvertToExternalTexture(GraphBuilder, OpacityAtlas, LumenSceneData.OpacityAtlas);
	ConvertToExternalTexture(GraphBuilder, FinalLightingAtlas, LumenSceneData.FinalLightingAtlas);
	if (Lumen::UseIrradianceAtlas())
	{
		ConvertToExternalTexture(GraphBuilder, IrradianceAtlas, LumenSceneData.IrradianceAtlas);
	}
	if (Lumen::UseIndirectIrradianceAtlas())
	{
		ConvertToExternalTexture(GraphBuilder, IndirectIrradianceAtlas, LumenSceneData.IndirectIrradianceAtlas);
	}
	ConvertToExternalTexture(GraphBuilder, DilatedDepthAtlas, LumenSceneData.DepthAtlas);

	if (View.ViewState)
	{
		ConvertToExternalTexture(GraphBuilder, VoxelLighting, View.ViewState->Lumen.VoxelLighting);
		View.ViewState->Lumen.VoxelGridResolution = VoxelGridResolution;
		View.ViewState->Lumen.NumClipmapLevels = NumClipmapLevels;
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
	TracingParameters.LumenCardScene = TracingInputs.LumenCardScene;
	TracingParameters.ReflectionStruct = CreateReflectionUniformBuffer(View, UniformBuffer_MultiFrame);
	TracingParameters.FinalLightingAtlas = TracingInputs.FinalLightingAtlas;
	TracingParameters.IrradianceAtlas = TracingInputs.IrradianceAtlas;
	TracingParameters.IndirectIrradianceAtlas = TracingInputs.IndirectIrradianceAtlas;
	TracingParameters.OpacityAtlas = TracingInputs.OpacityAtlas;
	TracingParameters.DilatedDepthAtlas = TracingInputs.DilatedDepthAtlas;

	TracingParameters.VoxelLighting = TracingInputs.VoxelLighting;
	TracingParameters.CubeMapTreeLUTAtlas = GLumenCubeMapTreeLUTAtlas.GetTexture();
	
	GetLumenVoxelTracingParameters(TracingInputs, TracingParameters, bShaderWillTraceCardsOnly);

	TracingParameters.NumGlobalSDFClipmaps = View.GlobalDistanceFieldInfo.Clipmaps.Num();
}

// Nvidia has lower vertex throughput when only processing a few verts per instance
const int32 NumLumenQuadsInBuffer = 16;

IMPLEMENT_GLOBAL_SHADER(FInitializeCardScatterIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "InitializeCardScatterIndirectArgsCS", SF_Compute);

uint32 CullCardsToLightGroupSize = 64;

void FCullCardsToShapeCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), CullCardsToLightGroupSize);
	OutEnvironment.SetDefine(TEXT("NUM_CARDS_TO_RENDER_HASH_MAP_BUCKET_UINT32"), FLumenCardRenderer::NumCardsToRenderHashMapBucketUInt32);
}

IMPLEMENT_GLOBAL_SHADER(FCullCardsToShapeCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "CullCardsToShapeCS", SF_Compute);

bool FRasterizeToCardsVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportLumenGI(Parameters.Platform);
}

IMPLEMENT_GLOBAL_SHADER(FRasterizeToCardsVS,"/Engine/Private/Lumen/LumenSceneLighting.usf","RasterizeToCardsVS",SF_Vertex);

void FLumenCardScatterContext::Init(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData,
	const FLumenCardRenderer& LumenCardRenderer,
	ECullCardsMode InCardsCullMode,
	int32 InMaxCullingInstanceCount)
{
	MaxScatterInstanceCount = InMaxCullingInstanceCount;
	CardsCullMode = InCardsCullMode;

	NumCardsToOperateOn = LumenSceneData.VisibleCardsIndices.Num();

	if (CardsCullMode == ECullCardsMode::OperateOnCardsToRender)
	{
		NumCardsToOperateOn = LumenCardRenderer.CardIdsToRender.Num();
	}

	MaxQuadsPerScatterInstance = NumCardsToOperateOn * 6;
	const int32 NumQuadsInBuffer = FMath::DivideAndRoundUp(MaxQuadsPerScatterInstance * MaxScatterInstanceCount, 1024) * 1024;

	FRDGBufferRef QuadAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxScatterInstanceCount), TEXT("QuadAllocator"));
	FRDGBufferRef QuadDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumQuadsInBuffer), TEXT("QuadDataBuffer"));

	FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadAllocator, PF_R32_UINT)), 0);

	QuadAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadAllocator, PF_R32_UINT), ERDGUnorderedAccessViewFlags::SkipBarrier);
	QuadDataUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadDataBuffer, PF_R32_UINT), ERDGUnorderedAccessViewFlags::SkipBarrier);

	Parameters.QuadAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadAllocator, PF_R32_UINT));
	Parameters.QuadData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadDataBuffer, PF_R32_UINT));
	Parameters.MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
	Parameters.TilesPerInstance = NumLumenQuadsInBuffer;
}

void FLumenCardScatterContext::CullCardsToShape(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneData& LumenSceneData, 
	const FLumenCardRenderer& LumenCardRenderer,
	ECullCardsShapeType ShapeType,
	const FCullCardsShapeParameters& ShapeParameters,
	float UpdateFrequencyScale,
	int32 ScatterInstanceIndex)
{
	LLM_SCOPE_BYTAG(Lumen);

	FRDGBufferRef VisibleCardsIndexBuffer = GraphBuilder.RegisterExternalBuffer(LumenSceneData.VisibleCardsIndexBuffer);
	FRDGBufferRef CardsToRenderIndexBuffer = GraphBuilder.RegisterExternalBuffer(LumenCardRenderer.CardsToRenderIndexBuffer);
	FRDGBufferRef CardsToRenderHashMapBuffer = GraphBuilder.RegisterExternalBuffer(LumenCardRenderer.CardsToRenderHashMapBuffer);

	FCullCardsToShapeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullCardsToShapeCS::FParameters>();
	PassParameters->RWQuadAllocator = QuadAllocatorUAV;
	PassParameters->RWQuadData = QuadDataUAV;
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->LumenCardScene = LumenSceneData.UniformBuffer;
	PassParameters->ShapeParameters = ShapeParameters;
	PassParameters->MaxQuadsPerScatterInstance = MaxQuadsPerScatterInstance;
	PassParameters->ScatterInstanceIndex = ScatterInstanceIndex;
	PassParameters->NumVisibleCardsIndices = LumenSceneData.VisibleCardsIndices.Num();
	PassParameters->NumCardsToRenderIndices = LumenCardRenderer.CardIdsToRender.Num();
	PassParameters->VisibleCardsIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VisibleCardsIndexBuffer, PF_R32_UINT));
	PassParameters->CardsToRenderIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardsToRenderIndexBuffer, PF_R32_UINT));
	PassParameters->CardsToRenderHashMap = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardsToRenderHashMapBuffer, PF_R32_UINT));
	PassParameters->FrameId = View.ViewState->GetFrameIndex();
	PassParameters->CardLightingUpdateFrequencyScale = GLumenSceneCardLightingForceFullUpdate ? 0.0f : UpdateFrequencyScale;
	PassParameters->CardLightingUpdateMinFrequency = GLumenSceneCardLightingForceFullUpdate ? 1 : GLumenSceneCardLightingUpdateMinFrequency;

	FCullCardsToShapeCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FCullCardsToShapeCS::FOperateOnCardsMode>((uint32)CardsCullMode);
	PermutationVector.Set<FCullCardsToShapeCS::FShapeType>((int32)ShapeType);
	auto ComputeShader = View.ShaderMap->GetShader< FCullCardsToShapeCS >(PermutationVector);

	const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(NumCardsToOperateOn, CullCardsToLightGroupSize), 1, 1);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CullCardsToShape %u", (int32)ShapeType),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, ComputeShader, GroupSize](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupSize);
		});
}

void FLumenCardScatterContext::BuildScatterIndirectArgs(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View)
{
	FRDGBufferRef CardIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndexedIndirectParameters>(MaxScatterInstanceCount), TEXT("CardIndirectArgsBuffer"));
	FRDGBufferUAVRef CardIndirectArgsBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CardIndirectArgsBuffer));

	FInitializeCardScatterIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeCardScatterIndirectArgsCS::FParameters>();
	PassParameters->RWCardIndirectArgs = CardIndirectArgsBufferUAV;
	PassParameters->QuadAllocator = Parameters.QuadAllocator;
	PassParameters->MaxScatterInstanceCount = MaxScatterInstanceCount;
	PassParameters->TilesPerInstance = NumLumenQuadsInBuffer;

	FInitializeCardScatterIndirectArgsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FInitializeCardScatterIndirectArgsCS::FRectList >(UseRectTopologyForLumen());
	auto ComputeShader = View.ShaderMap->GetShader< FInitializeCardScatterIndirectArgsCS >(PermutationVector);

	const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(MaxScatterInstanceCount, FInitializeCardScatterIndirectArgsCS::GetGroupSize());

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("InitializeCardScatterIndirectArgsCS"),
		ComputeShader,
		PassParameters,
		GroupSize);

	Parameters.CardIndirectArgs = CardIndirectArgsBuffer;
}

uint32 FLumenCardScatterContext::GetIndirectArgOffset(int32 ScatterInstanceIndex) const
{
	return ScatterInstanceIndex * sizeof(FRHIDrawIndexedIndirectParameters);
}

class FLumenCardLightingInitializePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardLightingInitializePS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardLightingInitializePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityAtlas)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardLightingInitializePS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardLightingInitializePS", SF_Pixel);

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
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SrcAtlas)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardCopyAtlasPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardCopyAtlasPS", SF_Pixel);

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
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
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

IMPLEMENT_GLOBAL_SHADER(FLumenCardBlendAlbedoPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardBlendAlbedoPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardBlendAlbedo, )
SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardBlendAlbedoPS::FParameters, PS)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void CombineLumenSceneLighting(
	FScene* Scene, 
	FViewInfo& View,
	FRDGBuilder& GraphBuilder,
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
		
		extern int32 GLumenRadiosityDownsampleFactor;
		FVector2D CardUVSamplingOffset = FVector2D::ZeroVector;
		if (GLumenRadiosityDownsampleFactor > 1)
		{
			// Offset bilinear samples in order to not sample outside of the lower res radiosity card bounds
			CardUVSamplingOffset.X = (GLumenRadiosityDownsampleFactor * 0.25f) / LumenSceneData.MaxAtlasSize.X;
			CardUVSamplingOffset.Y = (GLumenRadiosityDownsampleFactor * 0.25f) / LumenSceneData.MaxAtlasSize.Y;
		}

		PassParameters->RenderTargets[0] = FRenderTargetBinding(FinalLightingAtlas, ERenderTargetLoadAction::ENoAction);
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.Parameters;
		PassParameters->VS.ScatterInstanceIndex = 0;
		PassParameters->VS.CardUVSamplingOffset = CardUVSamplingOffset;
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->PS.RadiosityAtlas = RadiosityAtlas;
		PassParameters->PS.OpacityAtlas = OpacityAtlas;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LightingCombine"),
			PassParameters,
			ERDGPassFlags::Raster,
			[MaxAtlasSize = Scene->LumenSceneData->MaxAtlasSize, PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
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
	PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
	PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.Parameters;
	PassParameters->VS.ScatterInstanceIndex = 0;
	PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
	PassParameters->PS.View = View.ViewUniformBuffer;
	PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
	PassParameters->PS.SrcAtlas = SrcAtlas;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("CopyLumenCardAtlas"),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = Scene->LumenSceneData->MaxAtlasSize, PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
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
	PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
	PassParameters->VS.CardScatterParameters = VisibleCardScatterContext.Parameters;
	PassParameters->VS.ScatterInstanceIndex = 0;
	PassParameters->VS.CardUVSamplingOffset = FVector2D::ZeroVector;
	PassParameters->PS.View = View.ViewUniformBuffer;
	PassParameters->PS.LumenCardScene = LumenSceneData.UniformBuffer;
	PassParameters->PS.AlbedoAtlas = AlbedoAtlas;
	PassParameters->PS.EmissiveAtlas = EmissiveAtlas;
	PassParameters->PS.DiffuseReflectivityOverride = FMath::Clamp<float>(GLumenSceneDiffuseReflectivityOverride, 0.0f, 1.0f);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ApplyLumenCardAlbedo"),
		PassParameters,
		ERDGPassFlags::Raster,
		[MaxAtlasSize = Scene->LumenSceneData->MaxAtlasSize, PassParameters, GlobalShaderMap](FRHICommandListImmediate& RHICmdList)
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

TGlobalResource<FTileTexCoordVertexBuffer> GLumenTileTexCoordVertexBuffer(NumLumenQuadsInBuffer);
TGlobalResource<FTileIndexBuffer> GLumenTileIndexBuffer(NumLumenQuadsInBuffer);

void ClearAtlasRDG(FRDGBuilder& GraphBuilder, FRDGTextureRef AtlasTexture)
{
	LLM_SCOPE_BYTAG(Lumen);

	{
		FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(AtlasTexture, ERenderTargetLoadAction::EClear);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ClearAtlas"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters](FRHICommandListImmediate& RHICmdList)
		{
		});
	}
}

DECLARE_GPU_STAT(LumenSceneLighting);

void FDeferredShadingSceneRenderer::RenderLumenSceneLighting(
	FRDGBuilder& GraphBuilder,
	FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	if (Lumen::ShouldRenderLumenCardsForView(Scene, Views[0]) && ViewFamily.EngineShowFlags.Lighting)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RenderLumenSceneLighting);
		QUICK_SCOPE_CYCLE_COUNTER(RenderLumenSceneLighting);
		RDG_EVENT_SCOPE(GraphBuilder, "LumenSceneLighting");
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenSceneLighting);

		FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
		FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, Views[0]);

		FRDGTextureRef RadiosityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityAtlas, TEXT("RadiosityAtlas"));

		RenderRadiosityForLumenScene(GraphBuilder, TracingInputs, GlobalShaderMap, RadiosityAtlas);

		ConvertToExternalTexture(GraphBuilder, RadiosityAtlas, LumenSceneData.RadiosityAtlas);

		FLumenCardScatterContext DirectLightingCardScatterContext;
		extern float GLumenSceneCardDirectLightingUpdateFrequencyScale;

		// Build the indirect args to write to the card faces we are going to update direct lighting for this frame
		DirectLightingCardScatterContext.Init(
			GraphBuilder,
			View,
			LumenSceneData,
			LumenCardRenderer,
			ECullCardsMode::OperateOnSceneForceUpdateForCardsToRender,
			1);

		DirectLightingCardScatterContext.CullCardsToShape(
			GraphBuilder,
			View,
			LumenSceneData,
			LumenCardRenderer,
			ECullCardsShapeType::None,
			FCullCardsShapeParameters(),
			GLumenSceneCardDirectLightingUpdateFrequencyScale,
			0);

		DirectLightingCardScatterContext.BuildScatterIndirectArgs(
			GraphBuilder,
			View);

		extern int32 GLumenSceneRecaptureLumenSceneEveryFrame;

		if (GLumenSceneRecaptureLumenSceneEveryFrame)
		{
			ClearAtlasRDG(GraphBuilder, TracingInputs.FinalLightingAtlas);
			if (Lumen::UseIrradianceAtlas())
			{
				ClearAtlasRDG(GraphBuilder, TracingInputs.IrradianceAtlas);
			}
			if (Lumen::UseIndirectIrradianceAtlas())
			{
				ClearAtlasRDG(GraphBuilder, TracingInputs.IndirectIrradianceAtlas);
			}
		}

		CombineLumenSceneLighting(
			Scene,
			View,
			GraphBuilder,
			TracingInputs.FinalLightingAtlas,
			TracingInputs.OpacityAtlas,
			RadiosityAtlas,
			GlobalShaderMap, 
			DirectLightingCardScatterContext);

		if (Lumen::UseIndirectIrradianceAtlas())
		{
			CopyLumenCardAtlas(
				Scene,
				View,
				GraphBuilder,
				TracingInputs.FinalLightingAtlas,
				TracingInputs.IndirectIrradianceAtlas,
				GlobalShaderMap,
				DirectLightingCardScatterContext);
		}

		RenderDirectLightingForLumenScene(
			GraphBuilder,
			TracingInputs.FinalLightingAtlas,
			TracingInputs.OpacityAtlas,
			GlobalShaderMap,
			DirectLightingCardScatterContext);

		if (Lumen::UseIrradianceAtlas())
		{
			CopyLumenCardAtlas(
				Scene,
				View,
				GraphBuilder,
				TracingInputs.FinalLightingAtlas,
				TracingInputs.IrradianceAtlas,
				GlobalShaderMap,
				DirectLightingCardScatterContext);
		}

		FRDGTextureRef AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas, TEXT("AlbedoAtlas"));
		FRDGTextureRef EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas, TEXT("EmissiveAtlas"));
		ApplyLumenCardAlbedo(
			Scene,
			View,
			GraphBuilder,
			TracingInputs.FinalLightingAtlas,
			AlbedoAtlas,
			EmissiveAtlas,
			GlobalShaderMap,
			DirectLightingCardScatterContext);

		LumenSceneData.bFinalLightingAtlasContentsValid = true;

		PrefilterLumenSceneLighting(GraphBuilder, View, TracingInputs, GlobalShaderMap, DirectLightingCardScatterContext);

		ComputeLumenSceneVoxelLighting(GraphBuilder, TracingInputs, GlobalShaderMap);

		ComputeLumenTranslucencyGIVolume(GraphBuilder, TracingInputs, GlobalShaderMap);

		TracingInputs.ExtractToScene(GraphBuilder, Scene, View);
	}
}
