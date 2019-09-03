// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessDownsample.cpp: Post processing down sample implementation.
=============================================================================*/

#include "PostProcess/PostProcessDownsample.h"

namespace
{
	
const int32 GDownsampleTileSizeX = 8;
const int32 GDownsampleTileSizeY = 8;

TAutoConsoleVariable<int32> CVarDownsampleQuality(
	TEXT("r.Downsample.Quality"),
	1,
	TEXT("Defines the quality in which the Downsample passes. we might add more quality levels later.\n")
	TEXT(" 0: low quality\n")
	TEXT(">0: high quality (default: 1)\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

BEGIN_SHADER_PARAMETER_STRUCT(FDownsampleParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
END_SHADER_PARAMETER_STRUCT()

FDownsampleParameters GetDownsampleParameters(
	const FViewInfo& View,
	const FIntRect InputViewport,
	FRDGTextureRef InputTexture,
	EDownsampleQuality DownsampleMethod)
{
	check(InputTexture);

	const FScreenPassTextureViewportParameters InputParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(InputViewport, InputTexture));

	FDownsampleParameters Parameters;
	Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters.Input = InputParameters;
	Parameters.InputTexture = InputTexture;
	Parameters.InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	return Parameters;
}

class FDownsampleQualityDimension : SHADER_PERMUTATION_ENUM_CLASS("DOWNSAMPLE_QUALITY", EDownsampleQuality);
using FDownsamplePermutationDomain = TShaderPermutationDomain<FDownsampleQualityDimension>;

class FDownsamplePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDownsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FDownsamplePS, FGlobalShader);

	using FPermutationDomain = FDownsamplePermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDownsampleParameters, Common)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FDownsamplePS, "/Engine/Private/PostProcessDownsample.usf", "MainPS", SF_Pixel);

class FDownsampleCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDownsampleCS);
	SHADER_USE_PARAMETER_STRUCT(FDownsampleCS, FGlobalShader);

	using FPermutationDomain = FDownsamplePermutationDomain;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FDownsampleParameters, Common)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutComputeTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GDownsampleTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GDownsampleTileSizeY);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDownsampleCS, "/Engine/Private/PostProcessDownsample.usf", "MainCS", SF_Compute);

} //! namespace

EDownsampleQuality GetDownsampleQuality()
{
	const int32 DownsampleQuality = FMath::Clamp(CVarDownsampleQuality.GetValueOnRenderThread(), 0, 1);

	return static_cast<EDownsampleQuality>(DownsampleQuality);
}

FDownsamplePassOutputs AddDownsamplePass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FDownsamplePassInputs& Inputs)
{
	check(Inputs.Texture);

	bool bIsComputePass = ScreenPassView.bUseComputePasses;

	if ((Inputs.Flags & EDownsampleFlags::ForceRaster) == EDownsampleFlags::ForceRaster)
	{
		bIsComputePass = false;
	}

	FRDGTextureRef OutputTexture = nullptr;

	// Construct the output texture to be half resolution (rounded up to even) with an optional format override.
	{
		FRDGTextureDesc Desc = Inputs.Texture->Desc;
		Desc.Reset();
		Desc.Extent = FIntPoint::DivideAndRoundUp(Desc.Extent, 2);
		Desc.Extent.X = FMath::Max(1, Desc.Extent.X);
		Desc.Extent.Y = FMath::Max(1, Desc.Extent.Y);
		Desc.TargetableFlags &= ~(TexCreate_RenderTargetable | TexCreate_UAV);
		Desc.TargetableFlags |= bIsComputePass ? TexCreate_UAV : TexCreate_RenderTargetable;
		Desc.Flags |= GFastVRamConfig.Downsample;
		Desc.AutoWritable = false;
		Desc.DebugName = Inputs.Name;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));

		if (Inputs.FormatOverride != PF_Unknown)
		{
			Desc.Format = Inputs.FormatOverride;
		}

		OutputTexture = GraphBuilder.CreateTexture(Desc, Inputs.Name);
	}

	FDownsamplePermutationDomain PermutationVector;
	PermutationVector.Set<FDownsampleQualityDimension>(Inputs.Quality);

	const FIntRect OutputViewport = FIntRect::DivideAndRoundUp(Inputs.Viewport, 2);

	if (bIsComputePass)
	{
		FDownsampleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsampleCS::FParameters>();
		PassParameters->Common = GetDownsampleParameters(ScreenPassView.View, Inputs.Viewport, Inputs.Texture, Inputs.Quality);
		PassParameters->Output = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(OutputViewport, OutputTexture));
		PassParameters->OutComputeTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FDownsampleCS> ComputeShader(ScreenPassView.View.ShaderMap, PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Downsample.%s %dx%d (CS)", Inputs.Name, Inputs.Viewport.Width(), Inputs.Viewport.Height()),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(OutputViewport.Size(), FIntPoint(GDownsampleTileSizeX, GDownsampleTileSizeY)));
	}
	else
	{
		FDownsamplePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsamplePS::FParameters>();
		PassParameters->Common = GetDownsampleParameters(ScreenPassView.View, Inputs.Viewport, Inputs.Texture, Inputs.Quality);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FDownsamplePS> PixelShader(ScreenPassView.View.ShaderMap, PermutationVector);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("Downsample.%s %dx%d (PS)", Inputs.Name, Inputs.Viewport.Width(), Inputs.Viewport.Height()),
			ScreenPassView,
			FScreenPassTextureViewport(OutputViewport, OutputTexture),
			FScreenPassTextureViewport(Inputs.Viewport, Inputs.Texture),
			*PixelShader,
			PassParameters);
	}

	FDownsamplePassOutputs Outputs;
	Outputs.Texture = OutputTexture;
	Outputs.Viewport = OutputViewport;
	return Outputs;
}

FRenderingCompositeOutputRef AddDownsamplePass(
	FRenderingCompositionGraph& Graph,
	const TCHAR *InName,
	FRenderingCompositeOutputRef Input,
	uint32 SceneColorDownsampleFactor,
	EDownsampleQuality InQuality,
	EDownsampleFlags InFlags,
	EPixelFormat InFormatOverride)
{
	FRenderingCompositePass* DownsamplePass = Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<1, 1>(
			[InFormatOverride, InQuality, SceneColorDownsampleFactor, InFlags, InName] (FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		const FIntRect SceneColorViewRect = InContext.GetDownsampledSceneColorViewRect(SceneColorDownsampleFactor);

		FRDGTextureRef InputTexture = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("DownsampleInput"));

		FDownsamplePassInputs PassInputs;
		PassInputs.Name = InName;
		PassInputs.Texture = InputTexture;
		PassInputs.Viewport = SceneColorViewRect;
		PassInputs.FormatOverride = InFormatOverride;
		PassInputs.Quality = InQuality;
		PassInputs.Flags = InFlags;

		FDownsamplePassOutputs PassOutputs = AddDownsamplePass(GraphBuilder, FScreenPassViewInfo(InContext.View), PassInputs);

		Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, PassOutputs.Texture);

		GraphBuilder.Execute();
	}));
	DownsamplePass->SetInput(ePId_Input0, Input);
	return DownsamplePass;
}