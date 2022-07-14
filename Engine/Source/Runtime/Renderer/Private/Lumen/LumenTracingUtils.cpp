// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenTracingUtils.h"
#include "LumenSceneRendering.h"

FLumenCardTracingInputs::FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, FLumenSceneData& LumenSceneData, const FLumenSceneFrameTemporaries& FrameTemporaries, bool bSurfaceCacheFeedback)
{
	LLM_SCOPE_BYTAG(Lumen);

	LumenCardSceneUniformBuffer = FrameTemporaries.LumenCardSceneUniformBuffer;

	check(FrameTemporaries.FinalLightingAtlas);

	AlbedoAtlas = FrameTemporaries.AlbedoAtlas;
	OpacityAtlas = FrameTemporaries.OpacityAtlas;
	NormalAtlas = FrameTemporaries.NormalAtlas;
	EmissiveAtlas = FrameTemporaries.EmissiveAtlas;
	DepthAtlas = FrameTemporaries.DepthAtlas;

	DirectLightingAtlas = FrameTemporaries.DirectLightingAtlas;
	IndirectLightingAtlas = FrameTemporaries.IndirectLightingAtlas;
	RadiosityNumFramesAccumulatedAtlas = FrameTemporaries.RadiosityNumFramesAccumulatedAtlas;
	FinalLightingAtlas = FrameTemporaries.FinalLightingAtlas;

	if (FrameTemporaries.CardPageLastUsedBufferUAV && FrameTemporaries.CardPageHighResLastUsedBufferUAV)
	{
		CardPageLastUsedBufferUAV = FrameTemporaries.CardPageLastUsedBufferUAV;
		CardPageHighResLastUsedBufferUAV = FrameTemporaries.CardPageHighResLastUsedBufferUAV;
	}
	else
	{
		CardPageLastUsedBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.DummyCardPageLastUsedBuffer")));
		CardPageHighResLastUsedBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.DummyCardPageHighResLastUsedBuffer")));
	}

	if (FrameTemporaries.SurfaceCacheFeedbackResources.BufferUAV && bSurfaceCacheFeedback)
	{
		SurfaceCacheFeedbackBufferAllocatorUAV = FrameTemporaries.SurfaceCacheFeedbackResources.BufferAllocatorUAV;
		SurfaceCacheFeedbackBufferUAV = FrameTemporaries.SurfaceCacheFeedbackResources.BufferUAV;
		SurfaceCacheFeedbackBufferSize = FrameTemporaries.SurfaceCacheFeedbackResources.BufferSize;
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

FLumenViewCardTracingInputs::FLumenViewCardTracingInputs(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (View.ViewState && View.ViewState->Lumen.VoxelLighting)
	{
		VoxelLighting = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.VoxelLighting);
		VoxelGridResolution = View.ViewState->Lumen.VoxelGridResolution;
		NumClipmapLevels = View.ViewState->Lumen.NumClipmapLevels;
	}
	else
	{
		VoxelLighting = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
		VoxelGridResolution = FIntVector(1);
		NumClipmapLevels = 0;
	}

	for (int32 ClipmapIndex = 0; ClipmapIndex < MaxVoxelClipmapLevels; ++ClipmapIndex)
	{
		if (ClipmapIndex < NumClipmapLevels)
		{
			const FLumenVoxelLightingClipmapState& Clipmap = View.ViewState->Lumen.VoxelLightingClipmapState[ClipmapIndex];

			ClipmapWorldToUVScale[ClipmapIndex] = Clipmap.Extent.GetMax() > 0.0f ? FVector(1.0f) / (2.0f * Clipmap.Extent) : FVector(1.0f);
			ClipmapWorldToUVBias[ClipmapIndex] = -(Clipmap.Center - Clipmap.Extent) * ClipmapWorldToUVScale[ClipmapIndex];
			ClipmapWorldCenter[ClipmapIndex] = Clipmap.Center;
			ClipmapWorldExtent[ClipmapIndex] = Clipmap.Extent;
			ClipmapWorldSamplingExtent[ClipmapIndex] = Clipmap.Extent - 0.5f * Clipmap.VoxelSize;
			ClipmapVoxelSizeAndRadius[ClipmapIndex] = FVector4f((FVector3f)Clipmap.VoxelSize, Clipmap.VoxelRadius);
		}
		else
		{
			ClipmapWorldToUVScale[ClipmapIndex] = FVector(1.0);
			ClipmapWorldToUVBias[ClipmapIndex] = FVector(0.0);
			ClipmapWorldCenter[ClipmapIndex] = FVector(0.0);
			ClipmapWorldExtent[ClipmapIndex] = FVector(1.0);
			ClipmapWorldSamplingExtent[ClipmapIndex] = FVector(1.0);
			ClipmapVoxelSizeAndRadius[ClipmapIndex] = FVector4f(1.0f);
		}
	}
}

typedef TUniformBufferRef<FLumenVoxelTracingParameters> FLumenVoxelTracingParametersBufferRef;
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenVoxelTracingParameters, "LumenVoxelTracingParameters");

void GetLumenVoxelParametersForClipmapLevel(const FLumenViewCardTracingInputs& ViewTracingInputs, FLumenVoxelTracingParameters& LumenVoxelTracingParameters,
	int SrcClipmapLevel, int DstClipmapLevel)
{
	// LWC_TODO: precision loss
	LumenVoxelTracingParameters.ClipmapWorldToUVScale[DstClipmapLevel] = (FVector3f)ViewTracingInputs.ClipmapWorldToUVScale[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldToUVBias[DstClipmapLevel] = (FVector3f)ViewTracingInputs.ClipmapWorldToUVBias[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapVoxelSizeAndRadius[DstClipmapLevel] = ViewTracingInputs.ClipmapVoxelSizeAndRadius[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldCenter[DstClipmapLevel] = (FVector3f)ViewTracingInputs.ClipmapWorldCenter[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldExtent[DstClipmapLevel] = (FVector3f)ViewTracingInputs.ClipmapWorldExtent[SrcClipmapLevel];
	LumenVoxelTracingParameters.ClipmapWorldSamplingExtent[DstClipmapLevel] = (FVector3f)ViewTracingInputs.ClipmapWorldSamplingExtent[SrcClipmapLevel];
}

void GetLumenVoxelTracingParameters(const FLumenViewCardTracingInputs& ViewTracingInputs, FLumenCardTracingParameters& TracingParameters, bool bShaderWillTraceCardsOnly)
{
	FLumenVoxelTracingParameters LumenVoxelTracingParameters;

	LumenVoxelTracingParameters.NumClipmapLevels = ViewTracingInputs.NumClipmapLevels;

	ensureMsgf(bShaderWillTraceCardsOnly || ViewTracingInputs.NumClipmapLevels > 0, TEXT("Higher level code should have prevented GetLumenCardTracingParameters in a scene with no voxel clipmaps"));

	for (int32 i = 0; i < ViewTracingInputs.NumClipmapLevels; i++)
	{
		GetLumenVoxelParametersForClipmapLevel(ViewTracingInputs, LumenVoxelTracingParameters, i, i);
	}

	TracingParameters.LumenVoxelTracingParameters = CreateUniformBufferImmediate(LumenVoxelTracingParameters, UniformBuffer_SingleFrame);
}

void GetLumenCardTracingParameters(
	const FViewInfo& View, 
	const FLumenCardTracingInputs& TracingInputs, 
	const FLumenViewCardTracingInputs& ViewTracingInputs,
	FLumenCardTracingParameters& TracingParameters, 
	bool bShaderWillTraceCardsOnly)
{
	LLM_SCOPE_BYTAG(Lumen);

	TracingParameters.View = View.ViewUniformBuffer;
	TracingParameters.LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
	TracingParameters.ReflectionStruct = CreateReflectionUniformBuffer(View, UniformBuffer_MultiFrame);
	
	// GPUScene
	const FScene* Scene = ((const FScene*)View.Family->Scene);
	const FGPUSceneResourceParameters GPUSceneParameters = Scene->GPUScene.GetShaderParameters();

	TracingParameters.GPUSceneInstanceSceneData = GPUSceneParameters.GPUSceneInstanceSceneData;
	TracingParameters.GPUSceneInstancePayloadData = GPUSceneParameters.GPUSceneInstancePayloadData;
	TracingParameters.GPUScenePrimitiveSceneData = GPUSceneParameters.GPUScenePrimitiveSceneData;

	// Feedback
	extern float GLumenSurfaceCacheFeedbackResLevelBias;
	TracingParameters.RWCardPageLastUsedBuffer = TracingInputs.CardPageLastUsedBufferUAV;
	TracingParameters.RWCardPageHighResLastUsedBuffer = TracingInputs.CardPageHighResLastUsedBufferUAV;
	TracingParameters.RWSurfaceCacheFeedbackBufferAllocator = TracingInputs.SurfaceCacheFeedbackBufferAllocatorUAV;
	TracingParameters.RWSurfaceCacheFeedbackBuffer = TracingInputs.SurfaceCacheFeedbackBufferUAV;
	TracingParameters.SurfaceCacheFeedbackBufferSize = TracingInputs.SurfaceCacheFeedbackBufferSize;
	TracingParameters.SurfaceCacheFeedbackBufferTileJitter = TracingInputs.SurfaceCacheFeedbackBufferTileJitter;
	TracingParameters.SurfaceCacheFeedbackBufferTileWrapMask = TracingInputs.SurfaceCacheFeedbackBufferTileWrapMask;
	TracingParameters.SurfaceCacheFeedbackResLevelBias = GLumenSurfaceCacheFeedbackResLevelBias + 0.5f; // +0.5f required for uint to float rounding in shader
	TracingParameters.SurfaceCacheUpdateFrameIndex = Scene->GetLumenSceneData(View)->GetSurfaceCacheUpdateFrameIndex();

	// Lumen surface cache atlas
	TracingParameters.DirectLightingAtlas = TracingInputs.DirectLightingAtlas;
	TracingParameters.IndirectLightingAtlas = TracingInputs.IndirectLightingAtlas;
	TracingParameters.FinalLightingAtlas = TracingInputs.FinalLightingAtlas;
	TracingParameters.AlbedoAtlas = TracingInputs.AlbedoAtlas;
	TracingParameters.OpacityAtlas = TracingInputs.OpacityAtlas;
	TracingParameters.NormalAtlas = TracingInputs.NormalAtlas;
	TracingParameters.EmissiveAtlas = TracingInputs.EmissiveAtlas;
	TracingParameters.DepthAtlas = TracingInputs.DepthAtlas;
	TracingParameters.VoxelLighting = ViewTracingInputs.VoxelLighting;

	if (ViewTracingInputs.NumClipmapLevels > 0)
	{
		GetLumenVoxelTracingParameters(ViewTracingInputs, TracingParameters, bShaderWillTraceCardsOnly);
	}

	TracingParameters.NumGlobalSDFClipmaps = View.GlobalDistanceFieldInfo.Clipmaps.Num();
}