// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenTracingUtils.h"
#include "LumenSceneRendering.h"

FLumenCardTracingInputs::FLumenCardTracingInputs(FRDGBuilder& GraphBuilder, const FScene* Scene, FLumenSceneFrameTemporaries& FrameTemporaries, bool bSurfaceCacheFeedback)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		FLumenCardScene* LumenCardSceneParameters = GraphBuilder.AllocParameters<FLumenCardScene>();
		SetupLumenCardSceneParameters(GraphBuilder, Scene, *LumenCardSceneParameters);
		LumenCardSceneUniformBuffer = GraphBuilder.CreateUniformBuffer(LumenCardSceneParameters);
	}

	check(LumenSceneData.FinalLightingAtlas);

	AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas);
	OpacityAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.OpacityAtlas);
	NormalAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas);
	EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas);
	DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas);

	DirectLightingAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DirectLightingAtlas);
	IndirectLightingAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.IndirectLightingAtlas);
	RadiosityNumFramesAccumulatedAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.RadiosityNumFramesAccumulatedAtlas);
	FinalLightingAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.FinalLightingAtlas);

	if (FrameTemporaries.CardPageLastUsedBuffer && FrameTemporaries.CardPageHighResLastUsedBuffer)
	{
		CardPageLastUsedBufferUAV = GraphBuilder.CreateUAV(FrameTemporaries.CardPageLastUsedBuffer, ERDGUnorderedAccessViewFlags::SkipBarrier);
		CardPageHighResLastUsedBufferUAV = GraphBuilder.CreateUAV(FrameTemporaries.CardPageHighResLastUsedBuffer, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}
	else
	{
		CardPageLastUsedBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_UINT);
		CardPageHighResLastUsedBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_UINT);
	}

	if (FrameTemporaries.SurfaceCacheFeedbackResources.Buffer && bSurfaceCacheFeedback)
	{
		SurfaceCacheFeedbackBufferAllocatorUAV = GraphBuilder.CreateUAV(FrameTemporaries.SurfaceCacheFeedbackResources.BufferAllocator, ERDGUnorderedAccessViewFlags::SkipBarrier);
		SurfaceCacheFeedbackBufferUAV = GraphBuilder.CreateUAV(FrameTemporaries.SurfaceCacheFeedbackResources.Buffer, ERDGUnorderedAccessViewFlags::SkipBarrier);
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

		for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmapLevels; ++ClipmapIndex)
		{
			const FLumenVoxelLightingClipmapState& Clipmap = View.ViewState->Lumen.VoxelLightingClipmapState[ClipmapIndex];

			ClipmapWorldToUVScale[ClipmapIndex] = FVector(1.0f) / (2.0f * Clipmap.Extent);
			ClipmapWorldToUVBias[ClipmapIndex] = -(Clipmap.Center - Clipmap.Extent) * ClipmapWorldToUVScale[ClipmapIndex];
			ClipmapVoxelSizeAndRadius[ClipmapIndex] = FVector4f((FVector3f)Clipmap.VoxelSize, Clipmap.VoxelRadius);
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
	const FGPUScene& GPUScene = Scene->GPUScene;
	TracingParameters.GPUSceneInstanceSceneData = GPUScene.InstanceSceneDataBuffer.SRV;
	TracingParameters.GPUSceneInstancePayloadData = GPUScene.InstancePayloadDataBuffer.SRV;
	TracingParameters.GPUScenePrimitiveSceneData = GPUScene.PrimitiveBuffer.SRV;

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
	TracingParameters.SurfaceCacheUpdateFrameIndex = Scene->LumenSceneData->GetSurfaceCacheUpdateFrameIndex();

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