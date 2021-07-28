// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessHistogram.cpp: Post processing histogram implementation.
=============================================================================*/

#include "PostProcess/PostProcessHistogram.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "ShaderCompilerCore.h"

TAutoConsoleVariable<int32> CVarUseAtomicHistogram(
	TEXT("r.Histogram.UseAtomic"), 0,
	TEXT("Uses atomic to speed up the generation of the histogram."),
	ECVF_RenderThreadSafe);

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

class FHistogramAtomicCS : public FGlobalShader
{
public:
	// Changing these numbers requires Histogram.usf to be recompiled.
	static const uint32 ThreadGroupSizeX = 64; //tested at 32, 64, 128 all fast.  but 2 or 256 were DISASTER.
	static const uint32 HistogramSize = 64;		//should be power of two

	// /4 as we store 4 buckets in one ARGB texel.
	static const uint32 HistogramTexelCount = HistogramSize;

	// The number of texels on each axis processed by a single thread group.
	//	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_GLOBAL_SHADER(FHistogramAtomicCS);
	SHADER_USE_PARAMETER_STRUCT(FHistogramAtomicCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
		SHADER_PARAMETER(FIntPoint, ThreadGroupCount)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistogramScatter64Output)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistogramScatter32Output)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_SIZE"), HistogramSize);
	}
};

class FHistogramAtomicConvertCS : public FGlobalShader
{
public:
	// Changing these numbers requires Histogram.usf to be recompiled.
	static const uint32 ThreadGroupSizeX = FHistogramAtomicCS::ThreadGroupSizeX;
	static const uint32 HistogramSize = FHistogramAtomicCS::HistogramSize;

	// /4 as we store 4 buckets in one ARGB texel.
	static const uint32 HistogramTexelCount = HistogramSize;

	// The number of texels on each axis processed by a single thread group.
	//	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_GLOBAL_SHADER(FHistogramAtomicConvertCS);
	SHADER_USE_PARAMETER_STRUCT(FHistogramAtomicConvertCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistogramScatter64Texture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistogramScatter32Texture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, HistogramOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_SIZE"), HistogramSize);
	}
};

const FIntPoint FHistogramCS::TexelsPerThreadGroup(ThreadGroupSizeX* LoopCountX, ThreadGroupSizeY* LoopCountY);

IMPLEMENT_GLOBAL_SHADER(FHistogramCS, "/Engine/Private/PostProcessHistogram.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FHistogramReducePS, "/Engine/Private/PostProcessHistogramReduce.usf", "MainPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FHistogramAtomicCS,        "/Engine/Private/Histogram.usf", "MainAtomicCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FHistogramAtomicConvertCS, "/Engine/Private/Histogram.usf", "HistogramConvertCS", SF_Compute);

} //! namespace

static FRDGTextureRef AddHistogramLegacyPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	const FScreenPassTexture& SceneColor,
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

		const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2D(
			TextureExtent,
			PF_FloatRGBA,
			FClearValueBinding::None,
			GFastVRamConfig.Histogram | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_ShaderResource);

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
			ComputeShader,
			PassParameters,
			FIntVector(HistogramThreadGroupCount.X, HistogramThreadGroupCount.Y, 1));
	}

	FRDGTextureRef HistogramReduceTexture = nullptr;

	// Second pass further reduces the histogram to a single line. The second line contains the eye adaptation value (two line texture).
	{
		const FIntPoint TextureExtent = FIntPoint(FHistogramCS::HistogramTexelCount, 2);

		const FRDGTextureDesc TextureDesc = FRDGTextureDesc::Create2D(
			TextureExtent,
			FHistogramReducePS::OutputFormat,
			FClearValueBinding::None,
			GFastVRamConfig.HistogramReduce | TexCreate_RenderTargetable | TexCreate_ShaderResource);

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
			PixelShader,
			PassParameters);
	}

	return HistogramReduceTexture;
}

static FRDGTextureRef AddHistogramAtomicPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	const FScreenPassTexture& SceneColor,
	FRDGTextureRef EyeAdaptationTexture)
{
	check(SceneColor.IsValid());
	check(EyeAdaptationTexture);

	RDG_EVENT_SCOPE(GraphBuilder, "Histogram");

	//FRDGTextureRef HistogramScatter64Texture;
	FRDGTextureRef HistogramScatter32Texture;
	{
		// {
		// 	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		// 		FIntPoint(FHistogramAtomicCS::HistogramTexelCount, 1),
		// 		PF_R32G32_UINT,
		// 		FClearValueBinding::None,
		// 		TexCreate_UAV);
		// 
		// 	if (IsMetalPlatform(View.GetShaderPlatform()))
		// 	{
		// 		Desc.Flags |= TexCreate_NoTiling;
		// 	}
		// 
		// 	HistogramScatter64Texture = GraphBuilder.CreateTexture(Desc, TEXT("Histogram.Scatter64"));
		// }

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				FIntPoint(FHistogramAtomicCS::HistogramTexelCount * 2, 1),
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			if (IsMetalPlatform(View.GetShaderPlatform()))
			{
				Desc.Flags |= TexCreate_NoTiling;
			}

			HistogramScatter32Texture = GraphBuilder.CreateTexture(Desc, TEXT("Histogram.Scatter32"));
		}

		FHistogramAtomicCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHistogramAtomicCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Input = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneColor));
		PassParameters->InputTexture = SceneColor.Texture;
		PassParameters->EyeAdaptationTexture = EyeAdaptationTexture;
		PassParameters->ThreadGroupCount = FIntPoint(SceneColor.ViewRect.Size().Y, 1);
		PassParameters->EyeAdaptation = EyeAdaptationParameters;
		// PassParameters->HistogramScatter64Output = GraphBuilder->CreateUAV(HistogramScatter64Texture);
		PassParameters->HistogramScatter32Output = GraphBuilder.CreateUAV(HistogramScatter32Texture);

		//clear the temp textures
		uint32 ClearValues[4] = { 0, 0, 0, 0 };
		// AddClearUAVPass(GraphBuilder, PassParameters->HistogramScatter64Output, ClearValues);
		AddClearUAVPass(GraphBuilder, PassParameters->HistogramScatter32Output, ClearValues);

		TShaderMapRef<FHistogramAtomicCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Histogram Atomic %dx%d (CS)", SceneColor.ViewRect.Width(), SceneColor.ViewRect.Height()),
			ComputeShader,
			PassParameters,
			FIntVector(PassParameters->ThreadGroupCount.X, PassParameters->ThreadGroupCount.Y, 1));
	}

	FRDGTextureRef HistogramTexture;
	{
		{
			const FRDGTextureDesc TextureDescGather = FRDGTextureDesc::Create2D(
				FIntPoint(FHistogramAtomicCS::HistogramTexelCount / 4, 2),
				PF_A32B32G32R32F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			HistogramTexture = GraphBuilder.CreateTexture(TextureDescGather, TEXT("Histogram"));
		}

		FHistogramAtomicConvertCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHistogramAtomicConvertCS::FParameters>();
		PassParameters->Input = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneColor));
		PassParameters->EyeAdaptationTexture = EyeAdaptationTexture;
		// PassParameters->HistogramScatter64Texture = HistogramScatter64Texture;
		PassParameters->HistogramScatter32Texture = HistogramScatter32Texture;
		PassParameters->HistogramOutput = GraphBuilder.CreateUAV(HistogramTexture);

		uint32 NumGroupsRequired = FMath::Max(1U, FHistogramAtomicConvertCS::HistogramSize / FHistogramAtomicConvertCS::ThreadGroupSizeX);

		const FIntPoint HistogramConvertThreadGroupCount = FIntPoint(NumGroupsRequired, 1);

		TShaderMapRef<FHistogramAtomicConvertCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Histogram Convert"),
			ComputeShader,
			PassParameters,
			FIntVector(HistogramConvertThreadGroupCount.X, HistogramConvertThreadGroupCount.Y, 1));
	}

	return HistogramTexture;
}


FRDGTextureRef AddHistogramPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FScreenPassTexture SceneColor,
	FRDGTextureRef EyeAdaptationTexture)
{
	if (CVarUseAtomicHistogram.GetValueOnRenderThread() == 1)
	{
		return AddHistogramAtomicPass(
			GraphBuilder,
			View,
			EyeAdaptationParameters,
			SceneColor,
			EyeAdaptationTexture);
	}
	else
	{
		return AddHistogramLegacyPass(
			GraphBuilder,
			View,
			EyeAdaptationParameters,
			SceneColor,
			EyeAdaptationTexture);
	}
}

FIntPoint GetHistogramTexelsPerGroup()
{
	return FHistogramCS::TexelsPerThreadGroup;
}