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

FLumenCardTracingInputs::FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View)
{
	LLM_SCOPE(ELLMTag::Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	LumenCardScene = LumenSceneData.UniformBuffer;

	if (LumenSceneData.Cards.Num() > 0)
	{
		FinalLightingAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.FinalLightingAtlas);
		OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
		DilatedDepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);
	}
	else
	{
		FinalLightingAtlas = OpacityAtlas = DilatedDepthAtlas = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	}

	if (View.ViewState && View.ViewState->Lumen.VoxelLighting)
	{
		VoxelLighting = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.VoxelLighting);
		VoxelGridResolution = View.ViewState->Lumen.VoxelGridResolution;
		NumClipmapLevels = View.ViewState->Lumen.NumClipmapLevels;
		ClipmapWorldToUVScale = View.ViewState->Lumen.ClipmapWorldToUVScale;
		ClipmapWorldToUVBias = View.ViewState->Lumen.ClipmapWorldToUVBias;
		ClipmapVoxelSizeAndRadius = View.ViewState->Lumen.ClipmapVoxelSizeAndRadius;
		ClipmapWorldCenter = View.ViewState->Lumen.ClipmapWorldCenter;
		ClipmapWorldExtent = View.ViewState->Lumen.ClipmapWorldExtent;
		ClipmapWorldSamplingExtent = View.ViewState->Lumen.ClipmapWorldSamplingExtent;
	}
	else
	{
		VoxelLighting = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
		VoxelGridResolution = FIntVector(1, 1, 1);
		NumClipmapLevels = 0;
	}
}

void FLumenCardTracingInputs::ExtractToScene(FRDGBuilder& GraphBuilder, FScene* Scene, FViewInfo& View)
{
	LLM_SCOPE(ELLMTag::Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	GraphBuilder.QueueTextureExtraction(OpacityAtlas, &LumenSceneData.OpacityAtlas);
	GraphBuilder.QueueTextureExtraction(FinalLightingAtlas, &LumenSceneData.FinalLightingAtlas);
	GraphBuilder.QueueTextureExtraction(DilatedDepthAtlas, &LumenSceneData.DepthAtlas);

	if (View.ViewState)
	{
		GraphBuilder.QueueTextureExtraction(VoxelLighting, &View.ViewState->Lumen.VoxelLighting);
		View.ViewState->Lumen.VoxelGridResolution = VoxelGridResolution;
		View.ViewState->Lumen.NumClipmapLevels = NumClipmapLevels;
		View.ViewState->Lumen.ClipmapWorldToUVScale = ClipmapWorldToUVScale;
		View.ViewState->Lumen.ClipmapWorldToUVBias = ClipmapWorldToUVBias;
		View.ViewState->Lumen.ClipmapVoxelSizeAndRadius = ClipmapVoxelSizeAndRadius;
		View.ViewState->Lumen.ClipmapWorldCenter = ClipmapWorldCenter;
		View.ViewState->Lumen.ClipmapWorldExtent = ClipmapWorldExtent;
		View.ViewState->Lumen.ClipmapWorldSamplingExtent = ClipmapWorldSamplingExtent;
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
		/*LumenVoxelTracingParameters.ClipmapWorldToUVScale[i] = TracingInputs.ClipmapWorldToUVScale[i];
		LumenVoxelTracingParameters.ClipmapWorldToUVBias[i] = TracingInputs.ClipmapWorldToUVBias[i];
		LumenVoxelTracingParameters.ClipmapVoxelSizeAndRadius[i] = TracingInputs.ClipmapVoxelSizeAndRadius[i];
		LumenVoxelTracingParameters.ClipmapWorldCenter[i] = TracingInputs.ClipmapWorldCenter[i];
		LumenVoxelTracingParameters.ClipmapWorldExtent[i] = TracingInputs.ClipmapWorldExtent[i];
		LumenVoxelTracingParameters.ClipmapWorldSamplingExtent[i] = TracingInputs.ClipmapWorldSamplingExtent[i];*/
		GetLumenVoxelParametersForClipmapLevel(TracingInputs, LumenVoxelTracingParameters, i, i);
	}

	TracingParameters.LumenVoxelTracingParameters = CreateUniformBufferImmediate(LumenVoxelTracingParameters, UniformBuffer_SingleFrame);
}

void GetLumenCardTracingParameters(const FViewInfo& View, const FLumenCardTracingInputs& TracingInputs, FLumenCardTracingParameters& TracingParameters, bool bShaderWillTraceCardsOnly)
{
	LLM_SCOPE(ELLMTag::Lumen);

	TracingParameters.View = View.ViewUniformBuffer;
	TracingParameters.LumenCardScene = TracingInputs.LumenCardScene;
	TracingParameters.ReflectionStruct = CreateReflectionUniformBuffer(View, UniformBuffer_MultiFrame);
	TracingParameters.FinalLightingAtlas = TracingInputs.FinalLightingAtlas;
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

	QuadAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadAllocator, PF_R32_UINT), ERDGChildResourceFlags::NoUAVBarrier);
	QuadDataUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadDataBuffer, PF_R32_UINT), ERDGChildResourceFlags::NoUAVBarrier);

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
	LLM_SCOPE(ELLMTag::Lumen);

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

class FLumenCardLightingCombinePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardLightingCombinePS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardLightingCombinePS, FGlobalShader);

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

IMPLEMENT_GLOBAL_SHADER(FLumenCardLightingCombinePS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "LumenCardLightingCombinePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardLightingCombine, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRasterizeToCardsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardLightingCombinePS::FParameters, PS)
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
	LLM_SCOPE(ELLMTag::Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		FLumenCardLightingCombine* PassParameters = GraphBuilder.AllocParameters<FLumenCardLightingCombine>();
		
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
			FLumenCardLightingCombinePS::FPermutationDomain PermutationVector;
			auto PixelShader = GlobalShaderMap->GetShader< FLumenCardLightingCombinePS >(PermutationVector);

			DrawQuadsToAtlas(MaxAtlasSize, PixelShader, PassParameters, GlobalShaderMap, TStaticBlendState<>::GetRHI(), RHICmdList);
		});
	}
}

TGlobalResource<FTileTexCoordVertexBuffer> GLumenTileTexCoordVertexBuffer(NumLumenQuadsInBuffer);
TGlobalResource<FTileIndexBuffer> GLumenTileIndexBuffer(NumLumenQuadsInBuffer);

void ClearAtlasRDG(FRDGBuilder& GraphBuilder, FRDGTextureRef AtlasTexture)
{
	LLM_SCOPE(ELLMTag::Lumen);

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
	LLM_SCOPE(ELLMTag::Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	if (DoesPlatformSupportLumenGI(ShaderPlatform)
		&& GAllowLumenScene
		&& ViewFamily.EngineShowFlags.Lighting
		&& LumenSceneData.VisibleCardsIndices.Num() > 0 
		&& LumenSceneData.AlbedoAtlas
		// Don't update scene lighting for secondary views
		&& !View.bIsPlanarReflection 
		&& !View.bIsSceneCapture
		&& !View.bIsReflectionCapture
		&& View.ViewState)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RenderLumenSceneLighting);
		QUICK_SCOPE_CYCLE_COUNTER(RenderLumenSceneLighting);
		RDG_EVENT_SCOPE(GraphBuilder, "LumenSceneLighting");
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenSceneLighting);

		FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
		FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, Views[0]);

		FRDGTextureRef RadiosityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityAtlas, TEXT("RadiosityAtlas"));

		RenderRadiosityForLumenScene(GraphBuilder, TracingInputs, GlobalShaderMap, RadiosityAtlas);

		GraphBuilder.QueueTextureExtraction(RadiosityAtlas, &LumenSceneData.RadiosityAtlas);

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

		LumenSceneData.bFinalLightingAtlasContentsValid = true;

		RenderDirectLightingForLumenScene(
			GraphBuilder,
			TracingInputs.FinalLightingAtlas,
			TracingInputs.OpacityAtlas,
			GlobalShaderMap,
			DirectLightingCardScatterContext);

		PrefilterLumenSceneLighting(GraphBuilder, View, TracingInputs, GlobalShaderMap, DirectLightingCardScatterContext);

		ComputeLumenSceneVoxelLighting(GraphBuilder, TracingInputs, GlobalShaderMap);

		ComputeLumenTranslucencyGIVolume(GraphBuilder, TracingInputs, GlobalShaderMap);

		TracingInputs.ExtractToScene(GraphBuilder, Scene, View);
	}
}
