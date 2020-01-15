// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessDownsample.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "PixelShaderUtils.h"

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
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
END_SHADER_PARAMETER_STRUCT()

FDownsampleParameters GetDownsampleParameters(const FViewInfo& View, FScreenPassTexture Output, FScreenPassTexture Input, EDownsampleQuality DownsampleMethod)
{
	check(Output.IsValid());
	check(Input.IsValid());

	const FScreenPassTextureViewportParameters InputParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(Input));
	const FScreenPassTextureViewportParameters OutputParameters = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(Output));

	FDownsampleParameters Parameters;
	Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters.Input = InputParameters;
	Parameters.Output = OutputParameters;
	Parameters.InputTexture = Input.Texture;
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

FScreenPassTexture AddDownsamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FDownsamplePassInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());

	bool bIsComputePass = View.bUseComputePasses;

	if ((Inputs.Flags & EDownsampleFlags::ForceRaster) == EDownsampleFlags::ForceRaster)
	{
		bIsComputePass = false;
	}

	FScreenPassRenderTarget Output;

	// Construct the output texture to be half resolution (rounded up to even) with an optional format override.
	{
		FRDGTextureDesc Desc = Inputs.SceneColor.Texture->Desc;
		Desc.Reset();
		Desc.Extent = FIntPoint::DivideAndRoundUp(Desc.Extent, 2);
		Desc.Extent.X = FMath::Max(1, Desc.Extent.X);
		Desc.Extent.Y = FMath::Max(1, Desc.Extent.Y);
		Desc.TargetableFlags &= ~(TexCreate_RenderTargetable | TexCreate_UAV);
		Desc.TargetableFlags |= bIsComputePass ? TexCreate_UAV : TexCreate_RenderTargetable;
		Desc.Flags |= GFastVRamConfig.Downsample;
		Desc.DebugName = Inputs.Name;
		Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 0));

		if (Inputs.FormatOverride != PF_Unknown)
		{
			Desc.Format = Inputs.FormatOverride;
		}

		Output.Texture = GraphBuilder.CreateTexture(Desc, Inputs.Name);
		Output.ViewRect = FIntRect::DivideAndRoundUp(Inputs.SceneColor.ViewRect, 2);
		Output.LoadAction = ERenderTargetLoadAction::ENoAction;
	}

	FDownsamplePermutationDomain PermutationVector;
	PermutationVector.Set<FDownsampleQualityDimension>(Inputs.Quality);

	const FScreenPassTextureViewport SceneColorViewport(Inputs.SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);

	if (bIsComputePass)
	{
		FDownsampleCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsampleCS::FParameters>();
		PassParameters->Common = GetDownsampleParameters(View, Output, Inputs.SceneColor, Inputs.Quality);
		PassParameters->OutComputeTexture = GraphBuilder.CreateUAV(Output.Texture);

		TShaderMapRef<FDownsampleCS> ComputeShader(View.ShaderMap, PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Downsample.%s %dx%d (CS)", Inputs.Name, Inputs.SceneColor.ViewRect.Width(), Inputs.SceneColor.ViewRect.Height()),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(OutputViewport.Rect.Size(), FIntPoint(GDownsampleTileSizeX, GDownsampleTileSizeY)));
	}
	else
	{
		FDownsamplePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsamplePS::FParameters>();
		PassParameters->Common = GetDownsampleParameters(View, Output, Inputs.SceneColor, Inputs.Quality);
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FDownsamplePS> PixelShader(View.ShaderMap, PermutationVector);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("Downsample.%s %dx%d (PS)", Inputs.Name, Inputs.SceneColor.ViewRect.Width(), Inputs.SceneColor.ViewRect.Height()),
			*PixelShader,
			PassParameters,
			OutputViewport.Rect);
	}

	return MoveTemp(Output);
}

void FSceneDownsampleChain::Init(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FEyeAdaptationParameters& EyeAdaptationParameters,
	FScreenPassTexture HalfResolutionSceneColor,
	EDownsampleQuality DownsampleQuality,
	bool bLogLumaInAlpha)
{
	check(HalfResolutionSceneColor.IsValid());

	RDG_EVENT_SCOPE(GraphBuilder, "SceneDownsample");

	static const TCHAR* PassNames[StageCount] =
	{
		nullptr,
		TEXT("Scene(1/4)"),
		TEXT("Scene(1/8)"),
		TEXT("Scene(1/16)"),
		TEXT("Scene(1/32)"),
		TEXT("Scene(1/64)")
	};
	static_assert(UE_ARRAY_COUNT(PassNames) == StageCount, "PassNames size must equal StageCount");

	// The first stage is the input.
	Textures[0] = HalfResolutionSceneColor;

	for (uint32 StageIndex = 1; StageIndex < StageCount; StageIndex++)
	{
		const uint32 PreviousStageIndex = StageIndex - 1;

		FDownsamplePassInputs PassInputs;
		PassInputs.Name = PassNames[StageIndex];
		PassInputs.SceneColor = Textures[PreviousStageIndex];
		PassInputs.Quality = DownsampleQuality;

		Textures[StageIndex] = AddDownsamplePass(GraphBuilder, View, PassInputs);

		if (bLogLumaInAlpha)
		{
			bLogLumaInAlpha = false;

			Textures[StageIndex] = AddBasicEyeAdaptationSetupPass(GraphBuilder, View, EyeAdaptationParameters, Textures[StageIndex]);
		}
	}

	bInitialized = true;
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
			[InFormatOverride, InQuality, SceneColorDownsampleFactor, InFlags, InName](FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		const FIntRect SceneColorViewRect = InContext.GetDownsampledSceneColorViewRect(SceneColorDownsampleFactor);

		FRDGTextureRef InputTexture = Pass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("DownsampleInput"));

		FDownsamplePassInputs PassInputs;
		PassInputs.Name = InName;
		PassInputs.SceneColor = FScreenPassTexture(InputTexture, SceneColorViewRect);
		PassInputs.FormatOverride = InFormatOverride;
		PassInputs.Quality = InQuality;
		PassInputs.Flags = InFlags;

		FScreenPassTexture PassOutput = AddDownsamplePass(GraphBuilder, InContext.View, PassInputs);

		Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, PassOutput.Texture);

		GraphBuilder.Execute();
	}));
	DownsamplePass->SetInput(ePId_Input0, Input);
	return DownsamplePass;
}