// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenVoxelDistanceField.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"

class FVoxelLightingToDistanceFieldCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelLightingToDistanceFieldCS)
	SHADER_USE_PARAMETER_STRUCT(FVoxelLightingToDistanceFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint4>, RWNearestVoxelAtlas)
		SHADER_PARAMETER(FIntVector, ClipmapResolution)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(uint32, MaxDistanceFieldValue)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(8, 8, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVoxelLightingToDistanceFieldCS, "/Engine/Private/Lumen/LumenVoxelDistanceField.usf", "VoxelLightingToDistanceFieldCS", SF_Compute);

class FPropagateDistanceFieldCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPropagateDistanceFieldCS)
	SHADER_USE_PARAMETER_STRUCT(FPropagateDistanceFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWNearestVoxelAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, RWDistanceFieldAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint4>, PrevNearestVoxelAtlas)
		SHADER_PARAMETER(FIntVector, ClipmapResolution)
		SHADER_PARAMETER(uint32, ClipmapIndex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(8, 8, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), GetGroupSize().Y);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Z"), GetGroupSize().Z);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPropagateDistanceFieldCS, "/Engine/Private/Lumen/LumenVoxelDistanceField.usf", "PropagateDistanceFieldCS", SF_Compute);

void Lumen::UpdateVoxelDistanceField(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const TArray<int32, SceneRenderingAllocator>& ClipmapsToUpdate,
	FLumenCardTracingInputs& TracingInputs)
{
	if (!Lumen::UseVoxelRayTracing())
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "VoxelDistanceField");

	const int32 NumClipmapLevels = TracingInputs.NumClipmapLevels;
	const FIntVector ClipmapResolution = TracingInputs.VoxelGridResolution;
	const FIntVector AtlasResolution = FIntVector(TracingInputs.VoxelGridResolution.X, TracingInputs.VoxelGridResolution.Y * NumClipmapLevels, TracingInputs.VoxelGridResolution.Z);

	FPooledRenderTargetDesc VoxelDistanceFieldDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
		AtlasResolution.X,
		AtlasResolution.Y,
		AtlasResolution.Z,
		PF_R8G8B8A8_UINT,
		FClearValueBinding::Black,
		TexCreate_None,
		TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_3DTiling,
		false));
	VoxelDistanceFieldDesc.AutoWritable = false;
	
	FRDGTextureRef VoxelDistanceField = TracingInputs.VoxelDistanceField;
	if (!VoxelDistanceField || !VoxelDistanceField->Desc.Compare(VoxelDistanceFieldDesc, true))
	{
		VoxelDistanceField = GraphBuilder.CreateTexture(VoxelDistanceFieldDesc, TEXT("VoxelDistanceField"));
	}	
	FRDGTextureUAVRef VoxelDistanceFieldUAV = GraphBuilder.CreateUAV(VoxelDistanceField);

	FRDGTextureRef TempVoxelDistanceField = GraphBuilder.CreateTexture(VoxelDistanceFieldDesc, TEXT("TempVoxelDistanceField"));
	FRDGTextureUAVRef TempVoxelDistanceFieldUAV = GraphBuilder.CreateUAV(TempVoxelDistanceField);

	const uint32 MaxDistanceFieldValue = 15;

	// Convert face lighting to volume with indices of the nearest voxels
	for (int32 ClipmapIndex : ClipmapsToUpdate)
	{
		FVoxelLightingToDistanceFieldCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVoxelLightingToDistanceFieldCS::FParameters>();
		PassParameters->RWNearestVoxelAtlas = TempVoxelDistanceFieldUAV;
		PassParameters->ClipmapResolution = ClipmapResolution;
		PassParameters->ClipmapIndex = ClipmapIndex;
		PassParameters->MaxDistanceFieldValue = MaxDistanceFieldValue;
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);

		auto ComputeShader = View.ShaderMap->GetShader<FVoxelLightingToDistanceFieldCS>();
		const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(ClipmapResolution, FVoxelLightingToDistanceFieldCS::GetGroupSize());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("VoxelLightingToDistanceField Clipmap:%d", ClipmapIndex),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	// Propagate distance field
	const int32 NumPropagationIterations = MaxDistanceFieldValue;
	for (int32 IterationIndex = 0; IterationIndex < NumPropagationIterations; ++IterationIndex)
	{
		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			FPropagateDistanceFieldCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPropagateDistanceFieldCS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RWNearestVoxelAtlas = IterationIndex % 2 == 0 ? VoxelDistanceFieldUAV : TempVoxelDistanceFieldUAV;
			PassParameters->PrevNearestVoxelAtlas = IterationIndex % 2 == 0 ? TempVoxelDistanceField : VoxelDistanceField;
			PassParameters->ClipmapResolution = ClipmapResolution;
			PassParameters->ClipmapIndex = ClipmapIndex;

			auto ComputeShader = View.ShaderMap->GetShader<FPropagateDistanceFieldCS>();
			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(ClipmapResolution, FPropagateDistanceFieldCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PropagateDistanceField Clipmap:%d", ClipmapIndex),
				ComputeShader,
				PassParameters,
				GroupSize);
		}
	}

	TracingInputs.VoxelDistanceField = VoxelDistanceField;
}
