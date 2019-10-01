// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessMorpheus.h"
#include "PostProcess/PostProcessHMD.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"

#if defined(MORPHEUS_ENGINE_DISTORTION) && MORPHEUS_ENGINE_DISTORTION

class FMorpheusShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// we must use a run time check for this because the builds the build machines create will have Morpheus defined,
		// but a user will not necessarily have the Morpheus files
		bool bEnableMorpheus = false;
		if (GConfig->GetBool(TEXT("/Script/MorpheusEditor.MorpheusRuntimeSettings"), TEXT("bEnableMorpheus"), bEnableMorpheus, GEngineIni))
		{
			return bEnableMorpheus;
		}
		return false;
	}

	FMorpheusShader() = default;
	FMorpheusShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FMorpheusPS : public FMorpheusShader
{
public:
	DECLARE_GLOBAL_SHADER(FMorpheusPS);
	SHADER_USE_PARAMETER_STRUCT(FMorpheusPS, FMorpheusShader);

	static const uint32 CoefficientCount = 5;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER(FVector2D, TextureScale)
		SHADER_PARAMETER(FVector2D, TextureOffset)
		SHADER_PARAMETER(float, TextureUVOffset)
		SHADER_PARAMETER_ARRAY(float, RCoefficients, [CoefficientCount])
		SHADER_PARAMETER_ARRAY(float, GCoefficients, [CoefficientCount])
		SHADER_PARAMETER_ARRAY(float, BCoefficients, [CoefficientCount])
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FMorpheusPS, "/Engine/Private/PostProcessHMDMorpheus.usf", "MainPS", SF_Pixel);

class FMorpheusVS : public FMorpheusShader
{
public:
	DECLARE_GLOBAL_SHADER(FMorpheusVS);
	SHADER_USE_PARAMETER_STRUCT(FMorpheusVS, FMorpheusShader);
	using FParameters = FEmptyShaderParameters;
};

IMPLEMENT_GLOBAL_SHADER(FMorpheusVS, "/Engine/Private/PostProcessHMDMorpheus.usf", "MainVS", SF_Vertex);

FScreenPassTexture AddMorpheusDistortionPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FHMDDistortionInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, ERenderTargetLoadAction::ENoAction, TEXT("Morpheus"));
	}

	FMorpheusPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMorpheusPS::FParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->InputTexture = Inputs.SceneColor.Texture;
	PassParameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();

	{
		static const FName MorpheusName(TEXT("PSVR"));
		check(GEngine->XRSystem.IsValid());
		check(GEngine->XRSystem->GetSystemName() == MorpheusName);

		IHeadMountedDisplay* HMDDevice = GEngine->XRSystem->GetHMDDevice();
		check(HMDDevice);

		const float* RCoefs = HMDDevice->GetRedDistortionParameters();
		const float* GCoefs = HMDDevice->GetGreenDistortionParameters();
		const float* BCoefs = HMDDevice->GetBlueDistortionParameters();
		check(RCoefs && GCoefs && BCoefs);

		for (uint32 i = 0; i < FMorpheusPS::CoefficientCount; ++i)
		{
			PassParameters->RCoefficients[i] = RCoefs[i];
			PassParameters->GCoefficients[i] = GCoefs[i];
			PassParameters->BCoefficients[i] = BCoefs[i];
		}

		check(View.StereoPass != eSSP_FULL);
		if (View.StereoPass == eSSP_LEFT_EYE)
		{
			PassParameters->TextureScale = HMDDevice->GetTextureScaleLeft();
			PassParameters->TextureOffset = HMDDevice->GetTextureOffsetLeft();
			PassParameters->TextureUVOffset = 0.0f;
		}
		else
		{
			PassParameters->TextureScale = HMDDevice->GetTextureScaleRight();
			PassParameters->TextureOffset = HMDDevice->GetTextureOffsetRight();
			PassParameters->TextureUVOffset = -0.5f;
		}
	}

	// Hard coding the output dimensions. Most VR pathways can send whatever resolution to the API, and it will handle
	// scaling, but here the output is just regular windows desktop, so we need it to be the right size regardless of
	// pixel density.
	Output.ViewRect = FIntRect(0, 0, 960, 1080);

	if (View.StereoPass == eSSP_RIGHT_EYE)
	{
		Output.ViewRect.Min.X += 960;
		Output.ViewRect.Max.X += 960;
	}

	TShaderMapRef<FMorpheusVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FMorpheusPS> PixelShader(View.ShaderMap);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("Morpheus"),
		View,
		FScreenPassTextureViewport(Output),
		FScreenPassTextureViewport(Inputs.SceneColor),
		FScreenPassPipelineState(*VertexShader, *PixelShader),
		EScreenPassDrawFlags::None,
		PassParameters,
		[PixelShader, PassParameters](FRHICommandList& RHICmdList)
	{
		SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);
	});

	return MoveTemp(Output);
}

#endif // MORPHEUS_ENGINE_DISTORTION