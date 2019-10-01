// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessHistogram.cpp: Post processing histogram implementation.
=============================================================================*/

#include "PostProcess/PostProcessHistogram.h"
#include "PostProcess/PostProcessEyeAdaptation.h"

namespace
{

class FHistogramCS : public FGlobalShader
{
public:
	// Changing these numbers requires Histogram.usf to be recompiled.
	static const uint32 ThreadGroupSizeX = 8;
	static const uint32 ThreadGroupSizeY = 4;
	static const uint32 LoopCountX = 8;
	static const uint32 LoopCountY = 8;
	static const uint32 HistogramSize = 64;

	// /4 as we store 4 buckets in one ARGB texel.
	static const uint32 HistogramTexelCount = HistogramSize / 4;

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_GLOBAL_SHADER(FHistogramCS);
	SHADER_USE_PARAMETER_STRUCT(FHistogramCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistogramRWTexture)
		SHADER_PARAMETER(FIntPoint, ThreadGroupCount)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEX"), LoopCountX);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEY"), LoopCountY);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_SIZE"), HistogramSize);
		OutEnvironment.CompilerFlags.Add( CFLAG_StandardOptimization );
	}

	// One ThreadGroup processes LoopCountX*LoopCountY blocks of size ThreadGroupSizeX*ThreadGroupSizeY
	static FIntPoint GetThreadGroupCount(FIntPoint InputExtent)
	{
		return FIntPoint::DivideAndRoundUp(InputExtent, TexelsPerThreadGroup);
	}
};

const FIntPoint FHistogramCS::TexelsPerThreadGroup(ThreadGroupSizeX * LoopCountX, ThreadGroupSizeY * LoopCountY);

IMPLEMENT_GLOBAL_SHADER(FHistogramCS, "/Engine/Private/PostProcessHistogram.usf", "MainCS", SF_Compute);

class FHistogramReducePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FHistogramReducePS);
	SHADER_USE_PARAMETER_STRUCT(FHistogramReducePS, FGlobalShader);
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
		SHADER_PARAMETER(uint32, LoopSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	// Uses full float4 to get best quality for smooth eye adaptation transitions.
	static const EPixelFormat OutputFormat = PF_A32B32G32R32F;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, OutputFormat);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHistogramReducePS, "/Engine/Private/PostProcessHistogramReduce.usf", "MainPS", SF_Pixel);

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

	const FIntPoint HistogramThreadGroupCount = FIntPoint::DivideAndRoundUp(SceneColor.ViewRect.Size(), FHistogramCS::TexelsPerThreadGroup);

	const uint32 HistogramThreadGroupCountTotal = HistogramThreadGroupCount.X * HistogramThreadGroupCount.Y;

	FRDGTextureRef HistogramTexture = nullptr;

	RDG_EVENT_SCOPE(GraphBuilder, "Histogram");

	// First pass outputs one flattened histogram per group.
	{
		const FIntPoint TextureExtent = FIntPoint(FHistogramCS::HistogramTexelCount, HistogramThreadGroupCountTotal);

		const FRDGTextureDesc TextureDesc = FPooledRenderTargetDesc::Create2DDesc(
			TextureExtent,
			PF_FloatRGBA,
			FClearValueBinding::None,
			GFastVRamConfig.Histogram,
			TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ShaderResource,
			false);

		HistogramTexture = GraphBuilder.CreateTexture(TextureDesc, TEXT("Histogram"));

		FHistogramCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHistogramCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Input = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneColor));
		PassParameters->InputTexture = SceneColor.Texture;
		PassParameters->HistogramRWTexture = GraphBuilder.CreateUAV(HistogramTexture);
		PassParameters->ThreadGroupCount = HistogramThreadGroupCount;
		PassParameters->EyeAdaptation = EyeAdaptationParameters;

		TShaderMapRef<FHistogramCS> ComputeShader(View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Histogram %dx%d (CS)", SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height()),
			*ComputeShader,
			PassParameters,
			FIntVector(HistogramThreadGroupCount.X, HistogramThreadGroupCount.Y, 1));
	}

	FRDGTextureRef HistogramReduceTexture = nullptr;

	// Second pass further reduces the histogram to a single line. The second line contains the eye adaptation value (two line texture).
	{
		const FIntPoint TextureExtent = FIntPoint(FHistogramCS::HistogramTexelCount, 2);

		const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2DDesc(
			TextureExtent,
			FHistogramReducePS::OutputFormat,
			FClearValueBinding::None,
			GFastVRamConfig.HistogramReduce,
			TexCreate_RenderTargetable | TexCreate_ShaderResource,
			false);

		HistogramReduceTexture = GraphBuilder.CreateTexture(TextureDesc, TEXT("HistogramReduce"));

		const FScreenPassTextureViewport InputViewport(HistogramTexture);
		const FScreenPassTextureViewport OutputViewport(HistogramReduceTexture);

		FHistogramReducePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHistogramReducePS::FParameters>();
		PassParameters->Input = GetScreenPassTextureViewportParameters(InputViewport);
		PassParameters->InputTexture = HistogramTexture;
		PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->LoopSize = HistogramThreadGroupCountTotal;
		PassParameters->EyeAdaptationTexture = EyeAdaptationTexture;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(HistogramReduceTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FHistogramReducePS> PixelShader(View.ShaderMap);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("HistogramReduce %dx%d (PS)", InputViewport.Extent.X, InputViewport.Extent.Y),
			View,
			OutputViewport,
			InputViewport,
			*PixelShader,
			PassParameters);
	}

	return HistogramReduceTexture;
}

FIntPoint GetHistogramTexelsPerGroup()
{
	return FHistogramCS::TexelsPerThreadGroup;
}