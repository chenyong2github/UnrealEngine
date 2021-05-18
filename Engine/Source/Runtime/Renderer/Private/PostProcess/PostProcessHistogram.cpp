// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessHistogram.cpp: Post processing histogram implementation.
=============================================================================*/

#include "PostProcess/PostProcessHistogram.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "ShaderCompilerCore.h"

namespace
{

class FHistogramCS : public FGlobalShader
{
public:
	// Changing these numbers requires Histogram.usf to be recompiled.
	static const uint32 kHistogramSize = 64;

	DECLARE_GLOBAL_SHADER(FHistogramCS);
	SHADER_USE_PARAMETER_STRUCT(FHistogramCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER(FIntRect, InputSceneColorViewportMinMax)
		SHADER_PARAMETER(FVector2D, InputSceneColorViewportExtentInverse)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, HistogramOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FHistogramConvertCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FHistogramConvertCS);
	SHADER_USE_PARAMETER_STRUCT(FHistogramConvertCS, FGlobalShader);
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER(float, AreaNormalizeFactor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, HistogramBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistogramTextureOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHistogramCS, "/Engine/Private/PostProcessHistogram.usf", "MainCS", SF_Compute);\
IMPLEMENT_GLOBAL_SHADER(FHistogramConvertCS, "/Engine/Private/PostProcessHistogram.usf", "MainConvertCS", SF_Compute);

} //! namespace

FRDGTextureRef AddHistogramPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FScreenPassTexture SceneColor,
	FRDGTextureRef EyeAdaptationTexture)
{
	check(SceneColor.IsValid());
	check(EyeAdaptationTexture);

	RDG_EVENT_SCOPE(GraphBuilder, "Histogram %dx%d", SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height());

	// First pass outputs one flattened histogram per group.
	FRDGBufferRef HistogramBuffer = nullptr;
	{
		{
			FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), FHistogramCS::kHistogramSize);

			if (GFastVRamConfig.Histogram & TexCreate_FastVRAM)
			{
				Desc.Usage |= BUF_FastVRAM | BUF_Transient;
			}

			HistogramBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("Histogram"));
		}
		
		FHistogramCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHistogramCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->EyeAdaptation = EyeAdaptationParameters;
		PassParameters->InputSceneColorViewportMinMax = SceneColor.ViewRect;
		PassParameters->InputSceneColorViewportExtentInverse.X = 1.0f / float(SceneColor.ViewRect.Width());
		PassParameters->InputSceneColorViewportExtentInverse.Y = 1.0f / float(SceneColor.ViewRect.Height());
		PassParameters->InputSceneColorTexture = SceneColor.Texture;
		PassParameters->HistogramOutput = GraphBuilder.CreateUAV(HistogramBuffer);

		static const int32 kGroupSize = 16;
		static const int32 kLoopSize = 8;

		TShaderMapRef<FHistogramCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildHistogram %dx%d", SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(SceneColor.ViewRect.Size(), kGroupSize * kLoopSize));
	}

	// Converts the histogram to a single line in a RGBA texture.
	// The second line contains the eye adaptation value (two line texture).
	FRDGTextureRef HistogramReduceTexture = nullptr;
	{
		{
			const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2D(
				FIntPoint(FHistogramCS::kHistogramSize / 4, 2),
				PF_A32B32G32R32F,
				FClearValueBinding::None,
				GFastVRamConfig.HistogramReduce | TexCreate_UAV | TexCreate_ShaderResource);

			HistogramReduceTexture = GraphBuilder.CreateTexture(TextureDesc, TEXT("Histogram"));
		}

		FHistogramConvertCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHistogramConvertCS::FParameters>();
		PassParameters->EyeAdaptation = EyeAdaptationParameters;
		PassParameters->AreaNormalizeFactor = 1.0f / float(SceneColor.ViewRect.Area());
		PassParameters->EyeAdaptationTexture = EyeAdaptationTexture;
		PassParameters->HistogramBuffer = GraphBuilder.CreateSRV(HistogramBuffer);
		PassParameters->HistogramTextureOutput = GraphBuilder.CreateUAV(HistogramReduceTexture);

		TShaderMapRef<FHistogramConvertCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HistogramConvert"),
			ComputeShader,
			PassParameters,
			FIntVector(2, 1, 1));
	}

	return HistogramReduceTexture;
}
