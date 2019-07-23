// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessTemporalAA.cpp: Post process MotionBlur implementation.
=============================================================================*/

#include "PostProcess/PostProcessMitchellNetravali.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "PostProcess/PostProcessTonemap.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "PostProcessing.h"

class FMitchellNetravaliDownsampleCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMitchellNetravaliDownsampleCS);
	SHADER_USE_PARAMETER_STRUCT(FMitchellNetravaliDownsampleCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsOpenGLPlatform(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptation)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2D, DispatchThreadToInputUVScale)
		SHADER_PARAMETER(FVector2D, DispatchThreadToInputUVBias)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMitchellNetravaliDownsampleCS, "/Engine/Private/PostProcessMitchellNetravali.usf", "DownsampleMainCS", SF_Compute);

FRDGTextureRef ComputeMitchellNetravaliDownsample(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	FRDGTextureRef InputTexture,
	const FIntRect InputViewport,
	const FScreenPassTextureViewport OutputViewport)
{
	FRDGTextureRef EyeAdaptationTexture = nullptr;

	if (ScreenPassView.View.HasValidEyeAdaptation())
	{
		EyeAdaptationTexture = GraphBuilder.RegisterExternalTexture(ScreenPassView.View.GetEyeAdaptation(), TEXT("EyeAdaptation"));
	}
	else
	{
		EyeAdaptationTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy, TEXT("EyeAdaptation"));
	}

	const FRDGTextureDesc OutputTextureDesc = FRDGTextureDesc::Create2DDesc(
		OutputViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding::Black,
		TexCreate_None,
		TexCreate_UAV,
		false);

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputTextureDesc, TEXT("MitchelNetravaliDownsampleOutput"));

	FMitchellNetravaliDownsampleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMitchellNetravaliDownsampleCS::FParameters>();
	PassParameters->ViewUniformBuffer = ScreenPassView.View.ViewUniformBuffer;
	PassParameters->Input = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(InputViewport, InputTexture));
	PassParameters->InputTexture = InputTexture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);
	PassParameters->EyeAdaptation = EyeAdaptationTexture;

	// Scale / Bias factor to map the dispatch thread id to the input texture UV.
	PassParameters->DispatchThreadToInputUVScale.X = InputViewport.Width()  / float(OutputViewport.Rect.Width()  * InputTexture->Desc.Extent.X);
	PassParameters->DispatchThreadToInputUVScale.Y = InputViewport.Height() / float(OutputViewport.Rect.Height() * InputTexture->Desc.Extent.Y);
	PassParameters->DispatchThreadToInputUVBias.X = PassParameters->DispatchThreadToInputUVScale.X * (0.5f * InputViewport.Min.X);
	PassParameters->DispatchThreadToInputUVBias.Y = PassParameters->DispatchThreadToInputUVScale.Y * (0.5f * InputViewport.Min.Y);

	TShaderMapRef<FMitchellNetravaliDownsampleCS> ComputeShader(ScreenPassView.View.ShaderMap);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MitchellNetravaliDownsample %dx%d -> %dx%d", InputViewport.Width(), InputViewport.Height(), OutputViewport.Rect.Width(), OutputViewport.Rect.Height()),
		*ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(OutputViewport.Rect.Size(), FComputeShaderUtils::kGolden2DGroupSize));

	return OutputTexture;
}

FRenderingCompositeOutputRef ComputeMitchellNetravaliDownsample(
	FRenderingCompositionGraph& Graph,
	FRenderingCompositeOutputRef Input,
	FIntRect InputViewport,
	FScreenPassTextureViewport OutputViewport)
{
	FRenderingCompositePass* MitchelNetravaliDownsamplePass = Graph.RegisterPass(new(FMemStack::Get()) TRCPassForRDG<1, 1>(
		[InputViewport, OutputViewport](FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		FRDGTextureRef InputTexture = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("Input"));

		FRDGTextureRef OutputTexture = ComputeMitchellNetravaliDownsample(GraphBuilder, FScreenPassViewInfo(InContext.View), InputTexture, InputViewport, OutputViewport);

		Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

		InContext.SceneColorViewRect = OutputViewport.Rect;
		InContext.ReferenceBufferSize = OutputViewport.Extent;

		GraphBuilder.Execute();
	}));

	MitchelNetravaliDownsamplePass->SetInput(ePId_Input0, Input);

	return FRenderingCompositeOutputRef(MitchelNetravaliDownsamplePass);
}