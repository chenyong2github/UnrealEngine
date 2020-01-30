// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessCombineLUTs.h"
#include "PostProcess/PostProcessTonemap.h"

namespace
{
TAutoConsoleVariable<float> CVarColorMin(
	TEXT("r.Color.Min"),
	0.0f,
	TEXT("Allows to define where the value 0 in the color channels is mapped to after color grading.\n")
	TEXT("The value should be around 0, positive: a gray scale is added to the darks, negative: more dark values become black, Default: 0"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarColorMid(
	TEXT("r.Color.Mid"),
	0.5f,
	TEXT("Allows to define where the value 0.5 in the color channels is mapped to after color grading (This is similar to a gamma correction).\n")
	TEXT("Value should be around 0.5, smaller values darken the mid tones, larger values brighten the mid tones, Default: 0.5"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarColorMax(
	TEXT("r.Color.Max"),
	1.0f,
	TEXT("Allows to define where the value 1.0 in the color channels is mapped to after color grading.\n")
	TEXT("Value should be around 1, smaller values darken the highlights, larger values move more colors towards white, Default: 1"),
	ECVF_RenderThreadSafe);

int32 GLUTSize = 32;
FAutoConsoleVariableRef CVarLUTSize(
	TEXT("r.LUT.Size"),
	GLUTSize,
	TEXT("Size of film LUT"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTonemapperFilm(
	TEXT("r.TonemapperFilm"),
	1,
	TEXT("Use new film tone mapper"),
	ECVF_RenderThreadSafe);

// Including the neutral one at index 0
const uint32 GMaxLUTBlendCount = 5;

struct FColorTransform
{
	FColorTransform()
	{
		Reset();
	}

	float MinValue;
	float MidValue;
	float MaxValue;

	void Reset()
	{
		MinValue = 0.0f;
		MidValue = 0.5f;
		MaxValue = 1.0f;
	}
};
} //! namespace

// false:use 256x16 texture / true:use volume texture (faster, requires geometry shader)
// USE_VOLUME_LUT: needs to be the same for C++ and HLSL.
// Safe to use at pipeline and run time.
bool PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(EShaderPlatform Platform)
{
	// This is used to know if the target shader platform does not support required volume texture features we need for sure (read, render to).
	return RHIVolumeTextureRenderingSupportGuaranteed(Platform) && (RHISupportsGeometryShaders(Platform) || RHISupportsVertexShaderLayer(Platform));
}

FColorRemapParameters GetColorRemapParameters()
{
	FColorTransform ColorTransform;
	ColorTransform.MinValue = FMath::Clamp(CVarColorMin.GetValueOnRenderThread(), -10.0f, 10.0f);
	ColorTransform.MidValue = FMath::Clamp(CVarColorMid.GetValueOnRenderThread(), -10.0f, 10.0f);
	ColorTransform.MaxValue = FMath::Clamp(CVarColorMax.GetValueOnRenderThread(), -10.0f, 10.0f);

	// x is the input value, y the output value
	// RGB = a, b, c where y = a * x*x + b * x + c
	float c = ColorTransform.MinValue;
	float b = 4 * ColorTransform.MidValue - 3 * ColorTransform.MinValue - ColorTransform.MaxValue;
	float a = ColorTransform.MaxValue - ColorTransform.MinValue - b;

	FColorRemapParameters Parameters;
	Parameters.MappingPolynomial = FVector(a, b, c);
	return Parameters;
}

BEGIN_SHADER_PARAMETER_STRUCT(FCombineLUTParameters, )
	SHADER_PARAMETER_TEXTURE_ARRAY(Texture2D, Textures, [GMaxLUTBlendCount])
	SHADER_PARAMETER_SAMPLER_ARRAY(SamplerState, Samplers, [GMaxLUTBlendCount])
	SHADER_PARAMETER_ARRAY(float, LUTWeights, [GMaxLUTBlendCount])
	SHADER_PARAMETER(FVector4, OverlayColor)
	SHADER_PARAMETER(FVector, ColorScale)
	SHADER_PARAMETER(FVector4, ColorSaturation)
	SHADER_PARAMETER(FVector4, ColorContrast)
	SHADER_PARAMETER(FVector4, ColorGamma)
	SHADER_PARAMETER(FVector4, ColorGain)
	SHADER_PARAMETER(FVector4, ColorOffset)
	SHADER_PARAMETER(FVector4, ColorSaturationShadows)
	SHADER_PARAMETER(FVector4, ColorContrastShadows)
	SHADER_PARAMETER(FVector4, ColorGammaShadows)
	SHADER_PARAMETER(FVector4, ColorGainShadows)
	SHADER_PARAMETER(FVector4, ColorOffsetShadows)
	SHADER_PARAMETER(FVector4, ColorSaturationMidtones)
	SHADER_PARAMETER(FVector4, ColorContrastMidtones)
	SHADER_PARAMETER(FVector4, ColorGammaMidtones)
	SHADER_PARAMETER(FVector4, ColorGainMidtones)
	SHADER_PARAMETER(FVector4, ColorOffsetMidtones)
	SHADER_PARAMETER(FVector4, ColorSaturationHighlights)
	SHADER_PARAMETER(FVector4, ColorContrastHighlights)
	SHADER_PARAMETER(FVector4, ColorGammaHighlights)
	SHADER_PARAMETER(FVector4, ColorGainHighlights)
	SHADER_PARAMETER(FVector4, ColorOffsetHighlights)
	SHADER_PARAMETER(float, WhiteTemp)
	SHADER_PARAMETER(float, WhiteTint)
	SHADER_PARAMETER(float, ColorCorrectionShadowsMax)
	SHADER_PARAMETER(float, ColorCorrectionHighlightsMin)
	SHADER_PARAMETER(float, BlueCorrection)
	SHADER_PARAMETER(float, ExpandGamut)
	SHADER_PARAMETER(float, FilmSlope)
	SHADER_PARAMETER(float, FilmToe)
	SHADER_PARAMETER(float, FilmShoulder)
	SHADER_PARAMETER(float, FilmBlackClip)
	SHADER_PARAMETER(float, FilmWhiteClip)
	SHADER_PARAMETER(uint32, bUseMobileTonemapper)
	SHADER_PARAMETER_STRUCT_INCLUDE(FColorRemapParameters, ColorRemap)
	SHADER_PARAMETER_STRUCT_INCLUDE(FTonemapperOutputDeviceParameters, OutputDevice)
	SHADER_PARAMETER_STRUCT_INCLUDE(FMobileFilmTonemapParameters, MobileFilmTonemap)
END_SHADER_PARAMETER_STRUCT()

void GetCombineLUTParameters(
	FCombineLUTParameters& Parameters,
	const FViewInfo& View,
	const FTexture* const* Textures,
	const float* Weights,
	uint32 BlendCount)
{
	check(Textures);
	check(Weights);

	const FPostProcessSettings& Settings = View.FinalPostProcessSettings;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	for (uint32 BlendIndex = 0; BlendIndex < BlendCount; ++BlendIndex)
	{
		// Neutral texture occupies the first slot and doesn't actually need to be set.
		if (BlendIndex != 0)
		{
			check(Textures[BlendIndex]);

			// Don't use texture asset sampler as it might have anisotropic filtering enabled
			Parameters.Textures[BlendIndex] = Textures[BlendIndex]->TextureRHI;
			Parameters.Samplers[BlendIndex] = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 1>::GetRHI();
		}

		Parameters.LUTWeights[BlendIndex] = Weights[BlendIndex];
	}

	Parameters.ColorScale = FVector4(View.ColorScale);
	Parameters.OverlayColor = View.OverlayColor;
	Parameters.ColorRemap = GetColorRemapParameters();

	// White balance
	Parameters.WhiteTemp = Settings.WhiteTemp;
	Parameters.WhiteTint = Settings.WhiteTint;

	// Color grade
	Parameters.ColorSaturation = Settings.ColorSaturation;
	Parameters.ColorContrast = Settings.ColorContrast;
	Parameters.ColorGamma = Settings.ColorGamma;
	Parameters.ColorGain = Settings.ColorGain;
	Parameters.ColorOffset = Settings.ColorOffset;

	Parameters.ColorSaturationShadows = Settings.ColorSaturationShadows;
	Parameters.ColorContrastShadows = Settings.ColorContrastShadows;
	Parameters.ColorGammaShadows = Settings.ColorGammaShadows;
	Parameters.ColorGainShadows = Settings.ColorGainShadows;
	Parameters.ColorOffsetShadows = Settings.ColorOffsetShadows;

	Parameters.ColorSaturationMidtones = Settings.ColorSaturationMidtones;
	Parameters.ColorContrastMidtones = Settings.ColorContrastMidtones;
	Parameters.ColorGammaMidtones = Settings.ColorGammaMidtones;
	Parameters.ColorGainMidtones = Settings.ColorGainMidtones;
	Parameters.ColorOffsetMidtones = Settings.ColorOffsetMidtones;

	Parameters.ColorSaturationHighlights = Settings.ColorSaturationHighlights;
	Parameters.ColorContrastHighlights = Settings.ColorContrastHighlights;
	Parameters.ColorGammaHighlights = Settings.ColorGammaHighlights;
	Parameters.ColorGainHighlights = Settings.ColorGainHighlights;
	Parameters.ColorOffsetHighlights = Settings.ColorOffsetHighlights;

	Parameters.ColorCorrectionShadowsMax = Settings.ColorCorrectionShadowsMax;
	Parameters.ColorCorrectionHighlightsMin = Settings.ColorCorrectionHighlightsMin;

	Parameters.BlueCorrection = Settings.BlueCorrection;
	Parameters.ExpandGamut = Settings.ExpandGamut;

	Parameters.FilmSlope = Settings.FilmSlope;
	Parameters.FilmToe = Settings.FilmToe;
	Parameters.FilmShoulder = Settings.FilmShoulder;
	Parameters.FilmBlackClip = Settings.FilmBlackClip;
	Parameters.FilmWhiteClip = Settings.FilmWhiteClip;
	Parameters.bUseMobileTonemapper = CVarTonemapperFilm.GetValueOnRenderThread() == 0;


	Parameters.MobileFilmTonemap = GetMobileFilmTonemapParameters(
		Settings,
		/* UseColorMatrix = */ true,
		/* UseShadowTint = */ true,
		/* UseContrast = */ true);

	Parameters.bUseMobileTonemapper = CVarTonemapperFilm.GetValueOnRenderThread() == 0;

	Parameters.OutputDevice = GetTonemapperOutputDeviceParameters(ViewFamily);

	Parameters.MobileFilmTonemap = GetMobileFilmTonemapParameters(
		Settings,
		/* UseColorMatrix = */ true,
		/* UseShadowTint = */ true,
		/* UseContrast = */ true);
}

class FLUTBlenderShader : public FGlobalShader
{
public:
	static const int32 GroupSize = 8;

	class FBlendCount : SHADER_PERMUTATION_RANGE_INT("BLENDCOUNT", 1, 5);
	using FPermutationDomain = TShaderPermutationDomain<FBlendCount>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);

		const int UseVolumeLUT = PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(Parameters.Platform) ? 1 : 0;
		OutEnvironment.SetDefine(TEXT("USE_VOLUME_LUT"), UseVolumeLUT);
	}

	FLUTBlenderShader() = default;
	FLUTBlenderShader(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FLUTBlenderPS : public FLUTBlenderShader
{
public:
	DECLARE_GLOBAL_SHADER(FLUTBlenderPS);
	SHADER_USE_PARAMETER_STRUCT(FLUTBlenderPS, FLUTBlenderShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCombineLUTParameters, CombineLUT)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLUTBlenderPS, "/Engine/Private/PostProcessCombineLUTs.usf", "MainPS", SF_Pixel);

class FLUTBlenderCS : public FLUTBlenderShader
{
public:
	DECLARE_GLOBAL_SHADER(FLUTBlenderCS);
	SHADER_USE_PARAMETER_STRUCT(FLUTBlenderCS, FLUTBlenderShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCombineLUTParameters, CombineLUT)
		SHADER_PARAMETER(FVector2D, OutputExtentInverse)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWOutputTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLUTBlenderCS, "/Engine/Private/PostProcessCombineLUTs.usf", "MainCS", SF_Compute);

uint32 GenerateFinalTable(const FFinalPostProcessSettings& Settings, const FTexture* OutTextures[], float OutWeights[], uint32 MaxCount)
{
	// Find the n strongest contributors, drop small contributors.
	uint32 LocalCount = 1;

	// Add the neutral one (done in the shader) as it should be the first and always there.
	OutTextures[0] = nullptr;
	OutWeights[0] = 0.0f;

	// Neutral index is the entry with no LUT texture assigned.
	for (int32 Index = 0; Index < Settings.ContributingLUTs.Num(); ++Index)
	{
		if (Settings.ContributingLUTs[Index].LUTTexture == nullptr)
		{
			OutWeights[0] = Settings.ContributingLUTs[Index].Weight;
			break;
		}
	}

	float OutWeightsSum = OutWeights[0];
	for (; LocalCount < MaxCount; ++LocalCount)
	{
		int32 BestIndex = INDEX_NONE;

		// Find the one with the strongest weight, add until full.
		for (int32 InputIndex = 0; InputIndex < Settings.ContributingLUTs.Num(); ++InputIndex)
		{
			bool bAlreadyInArray = false;

			{
				UTexture* LUTTexture = Settings.ContributingLUTs[InputIndex].LUTTexture;
				FTexture* Internal = LUTTexture ? LUTTexture->Resource : nullptr;
				for (uint32 OutputIndex = 0; OutputIndex < LocalCount; ++OutputIndex)
				{
					if (Internal == OutTextures[OutputIndex])
					{
						bAlreadyInArray = true;
						break;
					}
				}
			}

			if (bAlreadyInArray)
			{
				// We already have this one.
				continue;
			}

			// Take the first or better entry.
			if (BestIndex == INDEX_NONE || Settings.ContributingLUTs[BestIndex].Weight <= Settings.ContributingLUTs[InputIndex].Weight)
			{
				BestIndex = InputIndex;
			}
		}

		if (BestIndex == INDEX_NONE)
		{
			// No more elements to process.
			break;
		}

		const float WeightThreshold = 1.0f / 512.0f;

		const float BestWeight = Settings.ContributingLUTs[BestIndex].Weight;

		if (BestWeight < WeightThreshold)
		{
			// Drop small contributor 
			break;
		}

		UTexture* BestLUTTexture = Settings.ContributingLUTs[BestIndex].LUTTexture;
		FTexture* BestInternal = BestLUTTexture ? BestLUTTexture->Resource : 0;

		OutTextures[LocalCount] = BestInternal;
		OutWeights[LocalCount] = BestWeight;
		OutWeightsSum += BestWeight;
	}

	// Normalize the weights.
	if (OutWeightsSum > 0.001f)
	{
		const float OutWeightsSumInverse = 1.0f / OutWeightsSum;

		for (uint32 Index = 0; Index < LocalCount; ++Index)
		{
			OutWeights[Index] *= OutWeightsSumInverse;
		}
	}
	else
	{
		// Just the neutral texture at full weight.
		OutWeights[0] = 1.0f;
		LocalCount = 1;
	}

	return LocalCount;
}

FRDGTextureRef AddCombineLUTPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	const FSceneViewFamily& ViewFamily = *(View.Family);
	const FTexture* LocalTextures[GMaxLUTBlendCount];
	float LocalWeights[GMaxLUTBlendCount];
	uint32 LocalCount = 1;

	// Default to no LUTs.
	LocalTextures[0] = nullptr;
	LocalWeights[0] = 1.0f;

	if (ViewFamily.EngineShowFlags.ColorGrading)
	{
		LocalCount = GenerateFinalTable(View.FinalPostProcessSettings, LocalTextures, LocalWeights, GMaxLUTBlendCount);
	}

	const bool bUseComputePass = View.bUseComputePasses;

	const bool bUseVolumeTextureLUT = PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(View.GetShaderPlatform());

	const bool bUseFloatOutput = ViewFamily.SceneCaptureSource == SCS_FinalColorHDR;

	// Attempt to register the persistent view LUT texture.
	FRDGTextureRef OutputTexture = GraphBuilder.TryRegisterExternalTexture(
		View.GetTonemappingLUT(GraphBuilder.RHICmdList, GLUTSize, bUseVolumeTextureLUT, bUseComputePass, bUseFloatOutput));

	View.SetValidTonemappingLUT();

	// View doesn't support a persistent LUT, so create a temporary one.
	if (!OutputTexture)
	{
		OutputTexture = GraphBuilder.CreateTexture(
			FSceneViewState::CreateLUTRenderTarget(GLUTSize, bUseVolumeTextureLUT, bUseComputePass, bUseFloatOutput),
			TEXT("CombineLUT"));
	}

	check(OutputTexture);

	// For a 3D texture, the viewport is 16x16 (per slice); for a 2D texture, it's unwrapped to 256x16.
	const FIntPoint OutputViewSize(bUseVolumeTextureLUT ? GLUTSize : GLUTSize * GLUTSize, GLUTSize);

	FLUTBlenderShader::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLUTBlenderShader::FBlendCount>(LocalCount);

	if (bUseComputePass)
	{
		FLUTBlenderCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLUTBlenderCS::FParameters>();
		GetCombineLUTParameters(PassParameters->CombineLUT, View, LocalTextures, LocalWeights, LocalCount);
		PassParameters->OutputExtentInverse = FVector2D(1.0f, 1.0f) / FVector2D(OutputViewSize);
		PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(OutputTexture);

		TShaderMapRef<FLUTBlenderCS> ComputeShader(View.ShaderMap, PermutationVector);

		const uint32 GroupSizeXY = FMath::DivideAndRoundUp(OutputViewSize.X, FLUTBlenderCS::GroupSize);
		const uint32 GroupSizeZ = bUseVolumeTextureLUT ? GroupSizeXY : 1;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CombineLUTs (CS)"),
			*ComputeShader,
			PassParameters,
			FIntVector(GroupSizeXY, GroupSizeXY, GroupSizeZ));
	}
	else
	{
		FLUTBlenderPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLUTBlenderPS::FParameters>();
		GetCombineLUTParameters(PassParameters->CombineLUT, View, LocalTextures, LocalWeights, LocalCount);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FLUTBlenderPS> PixelShader(View.ShaderMap, PermutationVector);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CombineLUTS (PS)"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, PixelShader, PassParameters, bUseVolumeTextureLUT] (FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			if (bUseVolumeTextureLUT)
			{
				const FVolumeBounds VolumeBounds(GLUTSize);

				TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);

				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GETSAFERHISHADER_GEOMETRY(*GeometryShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(VolumeBounds.MaxX - VolumeBounds.MinX));

				SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
			}
			else
			{
				TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);

				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *PassParameters);

				DrawRectangle(
					RHICmdList,
					0, 0,										// XY
					GLUTSize * GLUTSize, GLUTSize,				// SizeXY
					0, 0,										// UV
					GLUTSize * GLUTSize, GLUTSize,				// SizeUV
					FIntPoint(GLUTSize * GLUTSize, GLUTSize),	// TargetSize
					FIntPoint(GLUTSize * GLUTSize, GLUTSize),	// TextureSize
					*VertexShader,
					EDRF_UseTriangleOptimization);
			}
		});
	}

	return OutputTexture;
}

FRenderingCompositeOutputRef AddCombineLUTPass(FRenderingCompositionGraph& Graph)
{
	FRenderingCompositePass* Pass = Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<0, 1>(
			[](FRenderingCompositePass* InPass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		FRDGTextureRef OutputTexture = AddCombineLUTPass(GraphBuilder, InContext.View);

		InPass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

		GraphBuilder.Execute();
	}));
	return Pass;
}
