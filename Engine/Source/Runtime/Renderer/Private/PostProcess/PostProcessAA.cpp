// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessAA.h"
#include "PostProcess/PostProcessing.h"

BEGIN_SHADER_PARAMETER_STRUCT(FFXAAParameters, )
	SHADER_PARAMETER_STRUCT(FScreenPassTextureInput, Input)
	SHADER_PARAMETER(FVector4, fxaaConsoleRcpFrameOpt)
	SHADER_PARAMETER(FVector4, fxaaConsoleRcpFrameOpt2)
	SHADER_PARAMETER(float, fxaaQualitySubpix)
	SHADER_PARAMETER(float, fxaaQualityEdgeThreshold)
	SHADER_PARAMETER(float, fxaaQualityEdgeThresholdMin)
	SHADER_PARAMETER(float, fxaaConsoleEdgeSharpness)
	SHADER_PARAMETER(float, fxaaConsoleEdgeThreshold)
	SHADER_PARAMETER(float, fxaaConsoleEdgeThresholdMin)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FFXAAVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFXAAVS);
	// FDrawRectangleParameters is filled by DrawScreenPass.
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FFXAAVS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	using FParameters = FFXAAParameters;
};

IMPLEMENT_GLOBAL_SHADER(FFXAAVS, "/Engine/Private/FXAAShader.usf", "FxaaVS", SF_Vertex);

class FFXAAPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFXAAPS);
	SHADER_USE_PARAMETER_STRUCT(FFXAAPS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	class FQualityDimension : SHADER_PERMUTATION_ENUM_CLASS("FXAA_PRESET", EFXAAQuality);
	using FPermutationDomain = TShaderPermutationDomain<FQualityDimension>;
	using FParameters = FFXAAParameters;
};

IMPLEMENT_GLOBAL_SHADER(FFXAAPS, "/Engine/Private/FXAAShader.usf", "FxaaPS", SF_Pixel);

EFXAAQuality GetFXAAQuality()
{
	const EPostProcessAAQuality PostProcessAAQuality = GetPostProcessAAQuality();
	static_assert(uint32(EPostProcessAAQuality::MAX) == uint32(EFXAAQuality::MAX), "FXAA quality levels don't match post process AA quality levels. Can't trivially convert.");
	return static_cast<EFXAAQuality>(PostProcessAAQuality);
}

FScreenPassTexture AddFXAAPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FFXAAInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.Quality != EFXAAQuality::MAX);

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, View.GetOverwriteLoadAction(), TEXT("FXAA"));
	}

	const FVector2D OutputExtentInverse = FVector2D(1.0f / (float)Output.Texture->Desc.Extent.X, 1.0f / (float)Output.Texture->Desc.Extent.Y);

	FRHISamplerState* BilinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FFXAAParameters* PassParameters = GraphBuilder.AllocParameters<FFXAAParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->Input = GetScreenPassTextureInput(Inputs.SceneColor, BilinearClampSampler);

	{
		float N = 0.5f;
		FVector4 Value(-N * OutputExtentInverse.X, -N * OutputExtentInverse.Y, N * OutputExtentInverse.X, N * OutputExtentInverse.Y);
		PassParameters->fxaaConsoleRcpFrameOpt = Value;
	}

	{
		float N = 2.0f;
		FVector4 Value(-N * OutputExtentInverse.X, -N * OutputExtentInverse.Y, N * OutputExtentInverse.X, N * OutputExtentInverse.Y);
		PassParameters->fxaaConsoleRcpFrameOpt2 = Value;
	}

	PassParameters->fxaaQualitySubpix = 0.75f;
	PassParameters->fxaaQualityEdgeThreshold = 0.166f;
	PassParameters->fxaaQualityEdgeThresholdMin = 0.0833f;
	PassParameters->fxaaConsoleEdgeSharpness = 8.0f;
	PassParameters->fxaaConsoleEdgeThreshold = 0.125f;
	PassParameters->fxaaConsoleEdgeThresholdMin = 0.05f;

	FFXAAPS::FPermutationDomain PixelPermutationVector;
	PixelPermutationVector.Set<FFXAAPS::FQualityDimension>(Inputs.Quality);

	TShaderMapRef<FFXAAVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FFXAAPS> PixelShader(View.ShaderMap, PixelPermutationVector);

	const FScreenPassTextureViewport OutputViewport(Output);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("FXAA %dx%d (PS)", OutputViewport.Rect.Width(), OutputViewport.Rect.Height()),
		View,
		OutputViewport,
		FScreenPassTextureViewport(Inputs.SceneColor),
		FScreenPassPipelineState(VertexShader, PixelShader),
		PassParameters,
		EScreenPassDrawFlags::AllowHMDHiddenAreaMask,
		[VertexShader, PixelShader, PassParameters](FRHICommandList& RHICmdList)
	{
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
	});

	return MoveTemp(Output);
}
