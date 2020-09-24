// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessBloomSetup.h"
#include "PostProcess/PostProcessDownsample.h"
#include "PostProcess/PostProcessFFTBloom.h"
#include "PostProcess/PostProcessWeightedSampleSum.h"

namespace
{
const int32 GBloomSetupComputeTileSizeX = 8;
const int32 GBloomSetupComputeTileSizeY = 8;

TAutoConsoleVariable<float> CVarBloomCross(
	TEXT("r.Bloom.Cross"),
	0.0f,
	TEXT("Experimental feature to give bloom kernel a more bright center sample (values between 1 and 3 work without causing aliasing)\n")
	TEXT("Existing bloom get lowered to match the same brightness\n")
	TEXT("<0 for a anisomorphic lens flare look (X only)\n")
	TEXT(" 0 off (default)\n")
	TEXT(">0 for a cross look (X and Y)"),
	ECVF_RenderThreadSafe);

BEGIN_SHADER_PARAMETER_STRUCT(FBloomSetupParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
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
	Parameters.EyeAdaptationTexture = EyeAdaptationTexture;
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

FScreenPassTexture AddBloomSetupPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FBloomSetupInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.EyeAdaptationTexture);
	check(Inputs.Threshold > -1.0f);

	const bool bIsComputePass = View.bUseComputePasses;

	FRDGTextureDesc OutputDesc = Inputs.SceneColor.Texture->Desc;
	OutputDesc.Reset();
	OutputDesc.Flags |= bIsComputePass ? TexCreate_UAV : TexCreate_RenderTargetable;

	const FScreenPassTextureViewport Viewport(Inputs.SceneColor);
	const FScreenPassRenderTarget Output(GraphBuilder.CreateTexture(OutputDesc, TEXT("BloomSetup")), Viewport.Rect, View.GetOverwriteLoadAction());

	if (bIsComputePass)
	{
		FBloomSetupCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBloomSetupCS::FParameters>();
		PassParameters->BloomSetup = GetBloomSetupParameters(View, Viewport, Inputs.SceneColor.Texture, Inputs.EyeAdaptationTexture, Inputs.Threshold);
		PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(Output.Texture);

		TShaderMapRef<FBloomSetupCS> ComputeShader(View.ShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BloomSetup %dx%d (CS)", Viewport.Rect.Width(), Viewport.Rect.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(Viewport.Rect.Size(), FIntPoint(GBloomSetupComputeTileSizeX, GBloomSetupComputeTileSizeY)));
	}
	else
	{
		FBloomSetupPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBloomSetupPS::FParameters>();
		PassParameters->BloomSetup = GetBloomSetupParameters(View, Viewport, Inputs.SceneColor.Texture, Inputs.EyeAdaptationTexture, Inputs.Threshold);
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FBloomSetupVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FBloomSetupPS> PixelShader(View.ShaderMap);

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("BloomSetup %dx%d (PS)", Viewport.Rect.Width(), Viewport.Rect.Height()),
			View,
			Viewport,
			Viewport,
			FScreenPassPipelineState(VertexShader, PixelShader),
			PassParameters,
			[VertexShader, PixelShader, PassParameters] (FRHICommandListImmediate& RHICmdList)
		{
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->BloomSetup);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
		});
	}

	return FScreenPassTexture(Output);
}

EBloomQuality GetBloomQuality()
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BloomQuality"));

	return static_cast<EBloomQuality>(FMath::Clamp(
		CVar->GetValueOnRenderThread(),
		static_cast<int32>(EBloomQuality::Disabled),
		static_cast<int32>(EBloomQuality::MAX)));
}

static_assert(
	static_cast<uint32>(EBloomQuality::MAX) == FSceneDownsampleChain::StageCount,
	"The total number of stages in the scene downsample chain and the number of bloom quality levels must match.");

FBloomOutputs AddBloomPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FBloomInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.SceneDownsampleChain);

	const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;

	const EBloomQuality BloomQuality = GetBloomQuality();

	FScreenPassTexture SceneColor = Inputs.SceneColor;
	FScreenPassTexture Bloom;

	if (BloomQuality != EBloomQuality::Disabled)
	{
		const bool bFFTBloomEnabled = IsFFTBloomEnabled(View);

		if (bFFTBloomEnabled)
		{
			FScreenPassTexture FullResolution = Inputs.SceneColor;
			FScreenPassTexture HalfResolution = Inputs.SceneDownsampleChain->GetFirstTexture();

			FFFTBloomInputs PassInputs;
			PassInputs.FullResolutionTexture = FullResolution.Texture;
			PassInputs.FullResolutionViewRect = FullResolution.ViewRect;
			PassInputs.HalfResolutionTexture = HalfResolution.Texture;
			PassInputs.HalfResolutionViewRect = HalfResolution.ViewRect;

			SceneColor.Texture = AddFFTBloomPass(GraphBuilder, View, PassInputs);
		}
		else
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Bloom");

			const float CrossBloom = CVarBloomCross.GetValueOnRenderThread();

			const FVector2D CrossCenterWeight(FMath::Max(CrossBloom, 0.0f), FMath::Abs(CrossBloom));

			check(BloomQuality != EBloomQuality::Disabled);
			const uint32 BloomQualityIndex = static_cast<uint32>(BloomQuality);
			const uint32 BloomQualityCountMax = static_cast<uint32>(EBloomQuality::MAX);

			struct FBloomStage
			{
				const float Size;
				const FLinearColor& Tint;
			};

			FBloomStage BloomStages[] =
			{
				{ Settings.Bloom6Size, Settings.Bloom6Tint },
				{ Settings.Bloom5Size, Settings.Bloom5Tint },
				{ Settings.Bloom4Size, Settings.Bloom4Tint },
				{ Settings.Bloom3Size, Settings.Bloom3Tint },
				{ Settings.Bloom2Size, Settings.Bloom2Tint },
				{ Settings.Bloom1Size, Settings.Bloom1Tint }
			};

			const uint32 BloomQualityToSceneDownsampleStage[] =
			{
				static_cast<uint32>(-1), // Disabled (sentinel entry to preserve indices)
				3, // Q1
				3, // Q2
				4, // Q3
				5, // Q4
				6  // Q5
			};

			static_assert(UE_ARRAY_COUNT(BloomStages) == BloomQualityCountMax, "Array must be one less than the number of bloom quality entries.");
			static_assert(UE_ARRAY_COUNT(BloomQualityToSceneDownsampleStage) == BloomQualityCountMax, "Array must be one less than the number of bloom quality entries.");

			// Use bloom quality to select the number of downsample stages to use for bloom.
			const uint32 BloomStageCount = BloomQualityToSceneDownsampleStage[BloomQualityIndex];

			const float TintScale = 1.0f / BloomQualityCountMax;

			for (uint32 StageIndex = 0, SourceIndex = BloomQualityCountMax - 1; StageIndex < BloomStageCount; ++StageIndex, --SourceIndex)
			{
				const FBloomStage& BloomStage = BloomStages[StageIndex];

				if (BloomStage.Size > SMALL_NUMBER)
				{
					FGaussianBlurInputs PassInputs;
					PassInputs.NameX = TEXT("BloomX");
					PassInputs.NameY = TEXT("BloomY");
					PassInputs.Filter = Inputs.SceneDownsampleChain->GetTexture(SourceIndex);
					PassInputs.Additive = Bloom;
					PassInputs.CrossCenterWeight = CrossCenterWeight;
					PassInputs.KernelSizePercent = BloomStage.Size * Settings.BloomSizeScale;
					PassInputs.TintColor = BloomStage.Tint * TintScale;

					Bloom = AddGaussianBlurPass(GraphBuilder, View, PassInputs);
				}
			}
		}
	}

	FBloomOutputs PassOutputs;
	PassOutputs.SceneColor = SceneColor;
	PassOutputs.Bloom = Bloom;
	return PassOutputs;
}
