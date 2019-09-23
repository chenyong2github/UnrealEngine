// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessBloomSetup.h"

namespace
{
const int32 GBloomSetupComputeTileSizeX = 8;
const int32 GBloomSetupComputeTileSizeY = 8;

BEGIN_SHADER_PARAMETER_STRUCT(FBloomSetupParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptation)
	SHADER_PARAMETER(float, BloomThreshold)
END_SHADER_PARAMETER_STRUCT()

FBloomSetupParameters GetBloomSetupParameters(
	const FViewInfo& View,
	const FScreenPassTextureViewport& InputViewport,
	FRDGTextureRef InputTexture,
	FRDGTextureRef EyeAdaptationTexture,
	float BloomThreshold)
{
	FBloomSetupParameters Parameters;
	Parameters.View = View.ViewUniformBuffer;
	Parameters.Input = GetScreenPassTextureViewportParameters(InputViewport);
	Parameters.InputTexture = InputTexture;
	Parameters.InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters.EyeAdaptation = EyeAdaptationTexture;
	Parameters.BloomThreshold = BloomThreshold;
	return Parameters;
}

class FBloomSetupVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBloomSetupVS);

	// FDrawRectangleParameters is filled by DrawScreenPass.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FBloomSetupVS, FGlobalShader);

	using FParameters = FBloomSetupParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBloomSetupVS, "/Engine/Private/PostProcessBloom.usf", "BloomSetupVS", SF_Vertex);

class FBloomSetupPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FBloomSetupPS);
	SHADER_USE_PARAMETER_STRUCT(FBloomSetupPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBloomSetupParameters, BloomSetup)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBloomSetupPS, "/Engine/Private/PostProcessBloom.usf", "BloomSetupPS", SF_Pixel);

class FBloomSetupCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FBloomSetupCS);
	SHADER_USE_PARAMETER_STRUCT(FBloomSetupCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBloomSetupParameters, BloomSetup)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWOutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GBloomSetupComputeTileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GBloomSetupComputeTileSizeY);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBloomSetupCS, "/Engine/Private/PostProcessBloom.usf", "BloomSetupCS", SF_Compute);
} //!namespace

FRDGTextureRef AddBloomSetupPass(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FRDGTextureRef SceneColorTexture,
	FIntRect SceneColorViewRect,
	FRDGTextureRef EyeAdaptationTexture,
	float BloomThreshold)
{
	check(SceneColorTexture);
	check(!SceneColorViewRect.IsEmpty());
	check(EyeAdaptationTexture);

	// A negative or zero threshold just disables the pass.
	if (BloomThreshold <= 0.0f)
	{
		return SceneColorTexture;
	}

	const FScreenPassTextureViewport Viewport(SceneColorViewRect, SceneColorTexture);

	const bool bIsComputePass = ScreenPassView.bUseComputePasses;

	FRDGTextureDesc OutputTextureDesc = SceneColorTexture->Desc;
	OutputTextureDesc.Reset();
	OutputTextureDesc.TargetableFlags |= bIsComputePass ? TexCreate_UAV : TexCreate_RenderTargetable;

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputTextureDesc, TEXT("BloomSetup"));

	if (bIsComputePass)
	{
		FBloomSetupCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBloomSetupCS::FParameters>();
		PassParameters->BloomSetup = GetBloomSetupParameters(ScreenPassView.View, Viewport, SceneColorTexture, EyeAdaptationTexture, BloomThreshold);
		PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FBloomSetupCS> ComputeShader(ScreenPassView.View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BloomSetup %dx%d (CS)", Viewport.Rect.Width(), Viewport.Rect.Height()),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(Viewport.Rect.Size(), FIntPoint(GBloomSetupComputeTileSizeX, GBloomSetupComputeTileSizeY)));
	}
	else
	{
		FBloomSetupPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBloomSetupPS::FParameters>();
		PassParameters->BloomSetup = GetBloomSetupParameters(ScreenPassView.View, Viewport, SceneColorTexture, EyeAdaptationTexture, BloomThreshold);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ScreenPassView.GetOverwriteLoadAction());

		TShaderMapRef<FBloomSetupVS> VertexShader(ScreenPassView.View.ShaderMap);
		TShaderMapRef<FBloomSetupPS> PixelShader(ScreenPassView.View.ShaderMap);

		const auto SetupFunction = [VertexShader, PixelShader, PassParameters]
			(FRHICommandListImmediate& RHICmdList)
		{
			SetShaderParameters(RHICmdList, *VertexShader, VertexShader->GetVertexShader(), PassParameters->BloomSetup);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
		};

		const FScreenPassDrawInfo ScreenPassDraw(*VertexShader, *PixelShader);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("BloomSetup %dx%d (PS)", Viewport.Rect.Width(), Viewport.Rect.Height()),
			ScreenPassView,
			Viewport,
			Viewport,
			ScreenPassDraw,
			PassParameters,
			SetupFunction);
	}

	return OutputTexture;
}