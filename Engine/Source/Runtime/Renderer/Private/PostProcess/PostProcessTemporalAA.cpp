// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessTemporalAA.cpp: Post process MotionBlur implementation.
=============================================================================*/

#include "PostProcess/PostProcessTemporalAA.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "PostProcess/PostProcessTonemap.h"
#include "PostProcess/PostProcessMitchellNetravali.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "PostProcessing.h"
#include "RenderGraph.h"
#include "SceneTextureParameters.h"
#include "PixelShaderUtils.h"

extern int32 GetPostProcessAAQuality();

const int32 GTemporalAATileSizeX = 8;
const int32 GTemporalAATileSizeY = 8;

static TAutoConsoleVariable<float> CVarTemporalAAFilterSize(
	TEXT("r.TemporalAAFilterSize"),
	1.0f,
	TEXT("Size of the filter kernel. (1.0 = smoother, 0.0 = sharper but aliased)."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTemporalAACatmullRom(
	TEXT("r.TemporalAACatmullRom"),
	0,
	TEXT("Whether to use a Catmull-Rom filter kernel. Should be a bit sharper than Gaussian."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTemporalAAPauseCorrect(
	TEXT("r.TemporalAAPauseCorrect"),
	1,
	TEXT("Correct temporal AA in pause. This holds onto render targets longer preventing reuse and consumes more memory."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTemporalAACurrentFrameWeight(
	TEXT("r.TemporalAACurrentFrameWeight"),
	.04f,
	TEXT("Weight of current frame's contribution to the history.  Low values cause blurriness and ghosting, high values fail to hide jittering."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTemporalAAUpsampleFiltered(
	TEXT("r.TemporalAAUpsampleFiltered"),
	1,
	TEXT("Use filtering to fetch color history during TamporalAA upsampling (see AA_FILTERED define in TAA shader). Disabling this makes TAAU faster, but lower quality. "),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTemporalAAHistorySP(
	TEXT("r.TemporalAA.HistoryScreenPercentage"),
	100.0f,
	TEXT("Size of temporal AA's history."),
	ECVF_RenderThreadSafe);

static float CatmullRom( float x )
{
	float ax = FMath::Abs(x);
	if( ax > 1.0f )
		return ( ( -0.5f * ax + 2.5f ) * ax - 4.0f ) *ax + 2.0f;
	else
		return ( 1.5f * ax - 2.5f ) * ax*ax + 1.0f;
}

static float GetTemporalAAHistoryUpscaleFactor(const FViewInfo& View)
{
	// We only support history upscale on PC with feature level SM5+
	if (!IsPCPlatform(View.GetShaderPlatform()) || !IsFeatureLevelSupported(View.GetShaderPlatform(), ERHIFeatureLevel::SM5))
	{
		return 1.0f;
	}

	return FMath::Clamp(CVarTemporalAAHistorySP.GetValueOnRenderThread() / 100.0f, 1.0f, 2.0f);
}

// ---------------------------------------------------- Shader permutation dimensions

namespace
{

class FTAAPassConfigDim : SHADER_PERMUTATION_ENUM_CLASS("TAA_PASS_CONFIG", ETAAPassConfig);
class FTAAFastDim : SHADER_PERMUTATION_BOOL("TAA_FAST");
class FTAAResponsiveDim : SHADER_PERMUTATION_BOOL("TAA_RESPONSIVE");
class FTAAScreenPercentageDim : SHADER_PERMUTATION_INT("TAA_SCREEN_PERCENTAGE_RANGE", 4);
class FTAAUpsampleFilteredDim : SHADER_PERMUTATION_BOOL("TAA_UPSAMPLE_FILTERED");
class FTAADownsampleDim : SHADER_PERMUTATION_BOOL("TAA_DOWNSAMPLE");

}


// ---------------------------------------------------- Shaders

class FTemporalAACS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTemporalAACS);
	SHADER_USE_PARAMETER_STRUCT(FTemporalAACS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<
		FTAAPassConfigDim,
		FTAAFastDim,
		FTAAScreenPercentageDim,
		FTAAUpsampleFilteredDim,
		FTAADownsampleDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, ViewportUVToInputBufferUV)
		SHADER_PARAMETER(FVector4, MaxViewportUVAndSvPositionToViewportUV)
		SHADER_PARAMETER(FVector2D, ScreenPosAbsMax)
		SHADER_PARAMETER(float, HistoryPreExposureCorrection)
		SHADER_PARAMETER(float, CurrentFrameWeight)
		SHADER_PARAMETER(int32, bCameraCut)

		SHADER_PARAMETER_ARRAY(float, SampleWeights, [9])
		SHADER_PARAMETER_ARRAY(float, PlusWeights, [5])

		SHADER_PARAMETER(FVector4, InputSceneColorSize)
		SHADER_PARAMETER(FVector4, OutputViewportSize)
		SHADER_PARAMETER(FVector4, OutputViewportRect)

		// History parameters
		SHADER_PARAMETER(FVector4, HistoryBufferSize)
		SHADER_PARAMETER(FVector4, HistoryBufferUVMinMax)
		SHADER_PARAMETER(FVector4, ScreenPosToHistoryBufferUV)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptation)

		// Inputs
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneMetadata)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSceneMetadataSampler)

		// History resources
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryBuffer0)
		SHADER_PARAMETER_SAMPLER(SamplerState, HistoryBuffer0Sampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryBuffer1)
		SHADER_PARAMETER_SAMPLER(SamplerState, HistoryBuffer1Sampler)

		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthBufferSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneVelocityBufferSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		
		// Temporal upsample specific parameters.
		SHADER_PARAMETER(FVector4, InputViewSize)
		SHADER_PARAMETER(FVector2D, InputViewMin)
		SHADER_PARAMETER(FVector2D, TemporalJitterPixels)
		SHADER_PARAMETER(float, ScreenPercentage)
		SHADER_PARAMETER(float, UpscaleFactor)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutComputeTex0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutComputeTex1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutComputeTexDownsampled)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// Screen percentage dimension is only for upsampling permutation.
		if (!IsTAAUpsamplingConfig(PermutationVector.Get<FTAAPassConfigDim>()) &&
			PermutationVector.Get<FTAAScreenPercentageDim>() != 0)
		{
			return false;
		}

		if (PermutationVector.Get<FTAAPassConfigDim>() == ETAAPassConfig::MainSuperSampling)
		{
			// Super sampling is only high end PC SM5 functionality.
			if (!IsPCPlatform(Parameters.Platform))
			{
				return false;
			}

			// No point disabling filtering.
			if (!PermutationVector.Get<FTAAUpsampleFilteredDim>())
			{
				return false;
			}

			// No point doing a fast permutation since it is PC only.
			if (PermutationVector.Get<FTAAFastDim>())
			{
				return false;
			}
		}

		// No point disabling filtering if not using the fast permutation already.
		if (!PermutationVector.Get<FTAAUpsampleFilteredDim>() &&
			!PermutationVector.Get<FTAAFastDim>())
		{
			return false;
		}

		// No point downsampling if not using the fast permutation already.
		if (PermutationVector.Get<FTAADownsampleDim>() &&
			!PermutationVector.Get<FTAAFastDim>())
		{
			return false;
		}

		// Screen percentage range 3 is only for super sampling.
		if (PermutationVector.Get<FTAAPassConfigDim>() != ETAAPassConfig::MainSuperSampling &&
			PermutationVector.Get<FTAAScreenPercentageDim>() == 3)
		{
			return false;
		}

		// Fast dimensions is only for Main and Diaphragm DOF.
		if (PermutationVector.Get<FTAAFastDim>() &&
			!IsMainTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()) &&
			!IsDOFTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()))
		{
			return false;
		}
		
		// Non filtering option is only for upsampling.
		if (!PermutationVector.Get<FTAAUpsampleFilteredDim>() &&
			PermutationVector.Get<FTAAPassConfigDim>() != ETAAPassConfig::MainUpsampling)
		{
			return false;
		}

		// TAA_DOWNSAMPLE is only only for Main and MainUpsampling configs.
		if (PermutationVector.Get<FTAADownsampleDim>() &&
			!IsMainTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()))
		{
			return false;
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GTemporalAATileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GTemporalAATileSizeY);
	}
}; // class FTemporalAACS


IMPLEMENT_GLOBAL_SHADER(FTemporalAACS, "/Engine/Private/PostProcessTemporalAA.usf", "MainCS", SF_Compute);


static void SetupSampleWeightParameters(FTemporalAACS::FParameters* OutTAAParameters, const FTAAPassParameters& PassParameters, FVector2D TemporalJitterPixels)
{
	float JitterX = TemporalJitterPixels.X;
	float JitterY = TemporalJitterPixels.Y;
	float ResDivisorInv = 1.0f / float(PassParameters.ResolutionDivisor);

	static const float SampleOffsets[9][2] =
	{
		{ -1.0f, -1.0f },
		{  0.0f, -1.0f },
		{  1.0f, -1.0f },
		{ -1.0f,  0.0f },
		{  0.0f,  0.0f },
		{  1.0f,  0.0f },
		{ -1.0f,  1.0f },
		{  0.0f,  1.0f },
		{  1.0f,  1.0f },
	};

	float FilterSize = CVarTemporalAAFilterSize.GetValueOnRenderThread();
	int32 bCatmullRom = CVarTemporalAACatmullRom.GetValueOnRenderThread();

	// Compute 3x3 weights
	{
		float TotalWeight = 0.0f;
		for (int32 i = 0; i < 9; i++)
		{
			float PixelOffsetX = SampleOffsets[i][0] - JitterX * ResDivisorInv;
			float PixelOffsetY = SampleOffsets[i][1] - JitterY * ResDivisorInv;

			PixelOffsetX /= FilterSize;
			PixelOffsetY /= FilterSize;

			if (bCatmullRom)
			{
				OutTAAParameters->SampleWeights[i] = CatmullRom(PixelOffsetX) * CatmullRom(PixelOffsetY);
				TotalWeight += OutTAAParameters->SampleWeights[i];
			}
			else
			{
				// Normal distribution, Sigma = 0.47
				OutTAAParameters->SampleWeights[i] = FMath::Exp(-2.29f * (PixelOffsetX * PixelOffsetX + PixelOffsetY * PixelOffsetY));
				TotalWeight += OutTAAParameters->SampleWeights[i];
			}
		}
	
		for (int32 i = 0; i < 9; i++)
			OutTAAParameters->SampleWeights[i] /= TotalWeight;
	}

	// Compute 3x3 + weights.
	{
		OutTAAParameters->PlusWeights[0] = OutTAAParameters->SampleWeights[1];
		OutTAAParameters->PlusWeights[1] = OutTAAParameters->SampleWeights[3];
		OutTAAParameters->PlusWeights[2] = OutTAAParameters->SampleWeights[4];
		OutTAAParameters->PlusWeights[3] = OutTAAParameters->SampleWeights[5];
		OutTAAParameters->PlusWeights[4] = OutTAAParameters->SampleWeights[7];
		float TotalWeightPlus = (
			OutTAAParameters->SampleWeights[1] +
			OutTAAParameters->SampleWeights[3] +
			OutTAAParameters->SampleWeights[4] +
			OutTAAParameters->SampleWeights[5] +
			OutTAAParameters->SampleWeights[7]);
	
		for (int32 i = 0; i < 5; i++)
			OutTAAParameters->PlusWeights[i] /= TotalWeightPlus;
	}
} // SetupSampleWeightParameters()


DECLARE_GPU_STAT(TAA)


const TCHAR* const kTAAOutputNames[] = {
	TEXT("TemporalAA"),
	TEXT("TemporalAA"),
	TEXT("TemporalAA"),
	TEXT("SSRTemporalAA"),
	TEXT("LightShaftTemporalAA"),
	TEXT("DOFTemporalAA"),
	TEXT("DOFTemporalAA"),
};

const TCHAR* const kTAAPassNames[] = {
	TEXT("Main"),
	TEXT("MainUpsampling"),
	TEXT("MainSuperSampling"),
	TEXT("ScreenSpaceReflections"),
	TEXT("LightShaft"),
	TEXT("DOF"),
	TEXT("DOFUpsampling"),
};


static_assert(ARRAY_COUNT(kTAAOutputNames) == int32(ETAAPassConfig::MAX), "Missing TAA output name.");
static_assert(ARRAY_COUNT(kTAAPassNames) == int32(ETAAPassConfig::MAX), "Missing TAA pass name.");


FIntPoint FTAAPassParameters::GetOutputExtent() const
{
	check(Validate());
	check(SceneColorInput);

	FIntPoint InputExtent = SceneColorInput->Desc.Extent;

	if (!IsTAAUpsamplingConfig(Pass))
		return InputExtent;

	check(OutputViewRect.Min == FIntPoint::ZeroValue);
	FIntPoint PrimaryUpscaleViewSize = FIntPoint::DivideAndRoundUp(OutputViewRect.Size(), ResolutionDivisor);
	FIntPoint QuantizedPrimaryUpscaleViewSize;
	QuantizeSceneBufferSize(PrimaryUpscaleViewSize, QuantizedPrimaryUpscaleViewSize);

	return FIntPoint(
		FMath::Max(InputExtent.X, QuantizedPrimaryUpscaleViewSize.X),
		FMath::Max(InputExtent.Y, QuantizedPrimaryUpscaleViewSize.Y));
}

bool FTAAPassParameters::Validate() const
{
	if (IsTAAUpsamplingConfig(Pass))
	{
		check(OutputViewRect.Min == FIntPoint::ZeroValue);
	}
	else
	{
		check(InputViewRect == OutputViewRect);
	}
	return true;
}

FTAAOutputs AddTemporalAAPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FTAAPassParameters& Inputs,
	const FTemporalAAHistory& InputHistory,
	FTemporalAAHistory* OutputHistory)
{
	check(Inputs.Validate());

	// Number of render target in TAA history.
	const int32 RenderTargetCount = IsDOFTAAConfig(Inputs.Pass) && FPostProcessing::HasAlphaChannelSupport() ? 2 : 1;
	
	// Whether this is main TAA pass;
	const bool bIsMainPass = IsMainTAAConfig(Inputs.Pass);
	
	// Whether to use camera cut shader permutation or not.
	const bool bCameraCut = !InputHistory.IsValid() || View.bCameraCut;

	const FIntPoint OutputExtent = Inputs.GetOutputExtent();

	// Src rectangle.
	const FIntRect SrcRect = Inputs.InputViewRect;
	const FIntRect DestRect = Inputs.OutputViewRect;
	const FIntRect PracticableSrcRect = FIntRect::DivideAndRoundUp(SrcRect, Inputs.ResolutionDivisor);
	const FIntRect PracticableDestRect = FIntRect::DivideAndRoundUp(DestRect, Inputs.ResolutionDivisor);

	const uint32 PassIndex = static_cast<uint32>(Inputs.Pass);

	// Name of the pass.
	const TCHAR* PassName = kTAAPassNames[PassIndex];

	// Create outputs
	FTAAOutputs Outputs;

	{
		FRDGTextureDesc SceneColorDesc = FRDGTextureDesc::Create2DDesc(
			OutputExtent,
			PF_FloatRGBA,
			FClearValueBinding::Black,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		if (Inputs.bOutputRenderTargetable)
		{
			SceneColorDesc.TargetableFlags |= TexCreate_RenderTargetable;
		}

		const TCHAR* OutputName = kTAAOutputNames[PassIndex];

		Outputs.SceneColor = GraphBuilder.CreateTexture(
			SceneColorDesc,
			OutputName,
			ERDGResourceFlags::MultiFrame);

		if (RenderTargetCount == 2)
		{
			Outputs.SceneMetadata = GraphBuilder.CreateTexture(
				SceneColorDesc,
				OutputName,
				ERDGResourceFlags::MultiFrame);
		}

		if (Inputs.bDownsample)
		{
			const FRDGTextureDesc HalfResSceneColorDesc = FRDGTextureDesc::Create2DDesc(
				SceneColorDesc.Extent / 2,
				Inputs.DownsampleOverrideFormat != PF_Unknown ? Inputs.DownsampleOverrideFormat : Inputs.SceneColorInput->Desc.Format,
				FClearValueBinding::Black,
				/* InFlags = */ TexCreate_None,
				/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_Transient | TexCreate_UAV,
				/* bInForceSeparateTargetAndShaderResource = */ false);

			Outputs.DownsampledSceneColor = GraphBuilder.CreateTexture(HalfResSceneColorDesc, TEXT("SceneColorHalfRes"));
		}
	}

	RDG_GPU_STAT_SCOPE(GraphBuilder, TAA);

	{
		FTemporalAACS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTAAPassConfigDim>(Inputs.Pass);
		PermutationVector.Set<FTAAFastDim>(Inputs.bUseFast);
		PermutationVector.Set<FTAADownsampleDim>(Inputs.bDownsample);
		PermutationVector.Set<FTAAUpsampleFilteredDim>(true);

		if (IsTAAUpsamplingConfig(Inputs.Pass))
		{
			const bool bUpsampleFiltered = CVarTemporalAAUpsampleFiltered.GetValueOnRenderThread() != 0 || Inputs.Pass != ETAAPassConfig::MainUpsampling;
			PermutationVector.Set<FTAAUpsampleFilteredDim>(bUpsampleFiltered);

			// If screen percentage > 100% on X or Y axes, then use screen percentage range = 2 shader permutation to disable LDS caching.
			if (SrcRect.Width() > DestRect.Width() ||
				SrcRect.Height() > DestRect.Height())
			{
				PermutationVector.Set<FTAAScreenPercentageDim>(2);
			}
			// If screen percentage < 50% on X and Y axes, then use screen percentage range = 3 shader permutation.
			else if (SrcRect.Width() * 100 < 50 * DestRect.Width() &&
				SrcRect.Height() * 100 < 50 * DestRect.Height() &&
				Inputs.Pass == ETAAPassConfig::MainSuperSampling)
			{
				PermutationVector.Set<FTAAScreenPercentageDim>(3);
			}
			// If screen percentage < 71% on X and Y axes, then use screen percentage range = 1 shader permutation to have smaller LDS caching.
			else if (SrcRect.Width() * 100 < 71 * DestRect.Width() &&
				SrcRect.Height() * 100 < 71 * DestRect.Height())
			{
				PermutationVector.Set<FTAAScreenPercentageDim>(1);
			}
		}

		FTemporalAACS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTemporalAACS::FParameters>();

		// Setups common shader parameters
		const FIntPoint InputExtent = Inputs.SceneColorInput->Desc.Extent;
		const FIntRect InputViewRect = Inputs.InputViewRect;
		const FIntRect OutputViewRect = Inputs.OutputViewRect;

		if (!IsTAAUpsamplingConfig(Inputs.Pass))
		{
			SetupSampleWeightParameters(PassParameters, Inputs, View.TemporalJitterPixels);
		}

		const float ResDivisor = Inputs.ResolutionDivisor;
		const float ResDivisorInv = 1.0f / ResDivisor;

		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->CurrentFrameWeight = CVarTemporalAACurrentFrameWeight.GetValueOnRenderThread();
		PassParameters->bCameraCut = bCameraCut;

		PassParameters->SceneTextures = SceneTextures;
		PassParameters->SceneDepthBufferSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->SceneVelocityBufferSampler = TStaticSamplerState<SF_Point>::GetRHI();

		// We need a valid velocity buffer texture. Use black (no velocity) if none exists.
		if (!PassParameters->SceneTextures.SceneVelocityBuffer)
		{
			PassParameters->SceneTextures.SceneVelocityBuffer = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);;
		}

		// Input buffer shader parameters
		{
			PassParameters->InputSceneColorSize = FVector4(
				InputExtent.X,
				InputExtent.Y,
				1.0f / float(InputExtent.X),
				1.0f / float(InputExtent.Y));
			PassParameters->InputSceneColor = Inputs.SceneColorInput;
			PassParameters->InputSceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
			PassParameters->InputSceneMetadata = Inputs.SceneMetadataInput;
			PassParameters->InputSceneMetadataSampler = TStaticSamplerState<SF_Point>::GetRHI();
		}

		PassParameters->OutputViewportSize = FVector4(
			PracticableDestRect.Width(), PracticableDestRect.Height(), 1.0f / float(PracticableDestRect.Width()), 1.0f / float(PracticableDestRect.Height()));
		PassParameters->OutputViewportRect = FVector4(PracticableDestRect.Min.X, PracticableDestRect.Min.Y, PracticableDestRect.Max.X, PracticableDestRect.Max.Y);

		// Set history shader parameters.
		{
			if (bCameraCut)
			{
				FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

				PassParameters->ScreenPosToHistoryBufferUV = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				PassParameters->ScreenPosAbsMax = FVector2D(0.0f, 0.0f);
				PassParameters->HistoryBufferUVMinMax = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
				PassParameters->HistoryBufferSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				PassParameters->HistoryBuffer0 = BlackDummy;
				PassParameters->HistoryBuffer1 = BlackDummy;

				// Remove dependency of the velocity buffer on camera cut, given it's going to be ignored by the shader.
				PassParameters->SceneTextures.SceneVelocityBuffer = BlackDummy;
			}
			else
			{
				FIntPoint ReferenceViewportOffset = InputHistory.ViewportRect.Min;
				FIntPoint ReferenceViewportExtent = InputHistory.ViewportRect.Size();
				FIntPoint ReferenceBufferSize = InputHistory.ReferenceBufferSize;

				float InvReferenceBufferSizeX = 1.f / float(InputHistory.ReferenceBufferSize.X);
				float InvReferenceBufferSizeY = 1.f / float(InputHistory.ReferenceBufferSize.Y);

				PassParameters->ScreenPosToHistoryBufferUV = FVector4(
					ReferenceViewportExtent.X * 0.5f * InvReferenceBufferSizeX,
					-ReferenceViewportExtent.Y * 0.5f * InvReferenceBufferSizeY,
					(ReferenceViewportExtent.X * 0.5f + ReferenceViewportOffset.X) * InvReferenceBufferSizeX,
					(ReferenceViewportExtent.Y * 0.5f + ReferenceViewportOffset.Y) * InvReferenceBufferSizeY);

				FIntPoint ViewportOffset = ReferenceViewportOffset / Inputs.ResolutionDivisor;
				FIntPoint ViewportExtent = FIntPoint::DivideAndRoundUp(ReferenceViewportExtent, Inputs.ResolutionDivisor);
				FIntPoint BufferSize = ReferenceBufferSize / Inputs.ResolutionDivisor;

				PassParameters->ScreenPosAbsMax = FVector2D(1.0f - 1.0f / float(ViewportExtent.X), 1.0f - 1.0f / float(ViewportExtent.Y));

				float InvBufferSizeX = 1.f / float(BufferSize.X);
				float InvBufferSizeY = 1.f / float(BufferSize.Y);

				PassParameters->HistoryBufferUVMinMax = FVector4(
					(ViewportOffset.X + 0.5f) * InvBufferSizeX,
					(ViewportOffset.Y + 0.5f) * InvBufferSizeY,
					(ViewportOffset.X + ViewportExtent.X - 0.5f) * InvBufferSizeX,
					(ViewportOffset.Y + ViewportExtent.Y - 0.5f) * InvBufferSizeY);

				PassParameters->HistoryBufferSize = FVector4(BufferSize.X, BufferSize.Y, InvBufferSizeX, InvBufferSizeY);

				PassParameters->HistoryBuffer0 = GraphBuilder.RegisterExternalTexture(InputHistory.RT[0]);
				if (InputHistory.RT[1].IsValid())
					PassParameters->HistoryBuffer1 = GraphBuilder.RegisterExternalTexture(InputHistory.RT[1]);
			}

			PassParameters->HistoryBuffer0Sampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			PassParameters->HistoryBuffer1Sampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		}

		PassParameters->MaxViewportUVAndSvPositionToViewportUV = FVector4(
			(PracticableDestRect.Width() - 0.5f * ResDivisor) / float(PracticableDestRect.Width()),
			(PracticableDestRect.Height() - 0.5f * ResDivisor) / float(PracticableDestRect.Height()),
			ResDivisor / float(DestRect.Width()),
			ResDivisor / float(DestRect.Height()));

		PassParameters->HistoryPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;

		{
			float InvSizeX = 1.0f / float(InputExtent.X);
			float InvSizeY = 1.0f / float(InputExtent.Y);
			PassParameters->ViewportUVToInputBufferUV = FVector4(
				ResDivisorInv * InputViewRect.Width() * InvSizeX,
				ResDivisorInv * InputViewRect.Height() * InvSizeY,
				ResDivisorInv * InputViewRect.Min.X * InvSizeX,
				ResDivisorInv * InputViewRect.Min.Y * InvSizeY);
		}

		PassParameters->EyeAdaptation = GetEyeAdaptationTexture(GraphBuilder, View);

		// Temporal upsample specific shader parameters.
		{
			// Temporal AA upscale specific params.
			float InputViewSizeInvScale = Inputs.ResolutionDivisor;
			float InputViewSizeScale = 1.0f / InputViewSizeInvScale;

			PassParameters->TemporalJitterPixels = InputViewSizeScale * View.TemporalJitterPixels;
			PassParameters->ScreenPercentage = float(InputViewRect.Width()) / float(OutputViewRect.Width());
			PassParameters->UpscaleFactor = float(OutputViewRect.Width()) / float(InputViewRect.Width());
			PassParameters->InputViewMin = InputViewSizeScale * FVector2D(InputViewRect.Min.X, InputViewRect.Min.Y);
			PassParameters->InputViewSize = FVector4(
				InputViewSizeScale * InputViewRect.Width(), InputViewSizeScale * InputViewRect.Height(),
				InputViewSizeInvScale / InputViewRect.Width(), InputViewSizeInvScale / InputViewRect.Height());
		}

		// UAVs
		{
			PassParameters->OutComputeTex0 = GraphBuilder.CreateUAV(Outputs.SceneColor);
			if (Outputs.SceneMetadata)
			{
				PassParameters->OutComputeTex1 = GraphBuilder.CreateUAV(Outputs.SceneMetadata);
			}
			if (Outputs.DownsampledSceneColor)
			{
				PassParameters->OutComputeTexDownsampled = GraphBuilder.CreateUAV(Outputs.DownsampledSceneColor);
			}
		}

		TShaderMapRef<FTemporalAACS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TAA %s%s %dx%d -> %dx%d",
				PassName, Inputs.bUseFast ? TEXT(" Fast") : TEXT(""),
				PracticableSrcRect.Width(), PracticableSrcRect.Height(),
				PracticableDestRect.Width(), PracticableDestRect.Height()),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(PracticableDestRect.Size(), GTemporalAATileSizeX));
	}
	
	if (!View.bViewStateIsReadOnly)
	{
		OutputHistory->SafeRelease();

		GraphBuilder.QueueTextureExtraction(Outputs.SceneColor, &OutputHistory->RT[0]);

		if (Outputs.SceneMetadata)
		{
			GraphBuilder.QueueTextureExtraction(Outputs.SceneMetadata, &OutputHistory->RT[1]);
		}

		OutputHistory->ViewportRect = DestRect;
		OutputHistory->ReferenceBufferSize = OutputExtent * Inputs.ResolutionDivisor;
	}

	return Outputs;
}

void AddTemporalAAPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FScreenPassViewInfo& ScreenPassView,
	const bool bAllowDownsampleSceneColor,
	const EPixelFormat DownsampleOverrideFormat,
	FRDGTextureRef InSceneColorTexture,
	FRDGTextureRef* OutSceneColorTexture,
	FRDGTextureRef* OutSceneColorHalfResTexture,
	FIntRect* OutSecondaryViewRect)
{
	const FViewInfo& View = ScreenPassView.View;

	check(View.AntiAliasingMethod == AAM_TemporalAA && View.ViewState);

	FTAAPassParameters TAAParameters(View);

	TAAParameters.Pass = View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale
		? ETAAPassConfig::MainUpsampling
		: ETAAPassConfig::Main;

	TAAParameters.SetupViewRect(View);

	const int32 LowQualityTemporalAA = 3;

	TAAParameters.bUseFast = GetPostProcessAAQuality() == LowQualityTemporalAA;

	const FIntRect SecondaryViewRect = TAAParameters.OutputViewRect;

	const float HistoryUpscaleFactor = GetTemporalAAHistoryUpscaleFactor(View);

	// Configures TAA to upscale the history buffer; this is in addition to the secondary screen percentage upscale.
	// We end up with a scene color that is larger than the secondary screen percentage. We immediately downscale
	// afterwards using a Mitchel-Netravali filter.
	if (HistoryUpscaleFactor > 1.0f)
	{
		const FIntPoint HistoryViewSize(
			TAAParameters.OutputViewRect.Width() * HistoryUpscaleFactor,
			TAAParameters.OutputViewRect.Height() * HistoryUpscaleFactor);

		FIntPoint QuantizedMinHistorySize;
		QuantizeSceneBufferSize(HistoryViewSize, QuantizedMinHistorySize);

		TAAParameters.Pass = ETAAPassConfig::MainSuperSampling;
		TAAParameters.bUseFast = false;

		TAAParameters.OutputViewRect.Min.X = 0;
		TAAParameters.OutputViewRect.Min.Y = 0;
		TAAParameters.OutputViewRect.Max = HistoryViewSize;
	}

	TAAParameters.DownsampleOverrideFormat = DownsampleOverrideFormat;

	TAAParameters.bDownsample = bAllowDownsampleSceneColor && TAAParameters.bUseFast;

	TAAParameters.SceneColorInput = InSceneColorTexture;

	const FTemporalAAHistory& InputHistory = View.PrevViewInfo.TemporalAAHistory;

	FTemporalAAHistory& OutputHistory = View.ViewState->PrevFrameViewInfo.TemporalAAHistory;

	const FTAAOutputs TAAOutputs = AddTemporalAAPass(
		GraphBuilder,
		SceneTextures,
		View,
		TAAParameters,
		InputHistory,
		&OutputHistory);

	FRDGTextureRef SceneColorTexture = TAAOutputs.SceneColor;

	// If we upscaled the history buffer, downsize back to the secondary screen percentage size.
	if (HistoryUpscaleFactor > 1.0f)
	{
		const FIntRect InputViewport = TAAParameters.OutputViewRect;

		FIntPoint QuantizedOutputSize;
		QuantizeSceneBufferSize(SecondaryViewRect.Size(), QuantizedOutputSize);

		FScreenPassTextureViewport OutputViewport;
		OutputViewport.Rect = SecondaryViewRect;
		OutputViewport.Extent.X = FMath::Max(InSceneColorTexture->Desc.Extent.X, QuantizedOutputSize.X);
		OutputViewport.Extent.Y = FMath::Max(InSceneColorTexture->Desc.Extent.Y, QuantizedOutputSize.Y);

		SceneColorTexture = ComputeMitchellNetravaliDownsample(GraphBuilder, ScreenPassView, SceneColorTexture, InputViewport, OutputViewport);
	}

	*OutSceneColorTexture = SceneColorTexture;
	*OutSceneColorHalfResTexture = TAAOutputs.DownsampledSceneColor;
	*OutSecondaryViewRect = SecondaryViewRect;
}

//////////////////////////////////////////////////////////////////////////
//! Legacy - Only used by DebugViewModeRendering. Remove after porting.

class FRCPassPostProcessTemporalAA : public TRenderingCompositePassBase<3, 3>
{
public:
	FRCPassPostProcessTemporalAA(
		const FPostprocessContext& Context,
		const FTAAPassParameters& InParameters,
		const FTemporalAAHistory& InInputHistory,
		FTemporalAAHistory* OutOutputHistory)
		: SavedParameters(InParameters)
		, InputHistory(InInputHistory)
		, OutputHistory(OutOutputHistory)
	{
		check(InParameters.Pass == ETAAPassConfig::Main);
		check(SavedParameters.Validate());

		bIsComputePass = true;
		bPreferAsyncCompute = false;
	}

	void Process(FRenderingCompositePassContext& Context) override
	{
		WaitForInputPassComputeFences(Context.RHICmdList);

		AsyncEndFence = FComputeFenceRHIRef();

		FRDGBuilder GraphBuilder(Context.RHICmdList);

		FSceneTextureParameters SceneTextures;
		SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

		FTAAPassParameters Parameters = SavedParameters;
		Parameters.SceneColorInput = CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));

		FTAAOutputs Outputs = AddTemporalAAPass(
			GraphBuilder,
			SceneTextures,
			Context.View,
			Parameters,
			InputHistory,
			/* out */ OutputHistory);

		ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, Outputs.SceneColor);

		GraphBuilder.Execute();
	}

	void Release() override { delete this; }

	FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override
	{
		// ExtractRDGTextureForOutput() is doing this work for us already.
		return FPooledRenderTargetDesc();
	}

	FRHIComputeFence* GetComputePassEndFence() const override { return AsyncEndFence; }

private:
	const FTAAPassParameters SavedParameters;

	FComputeFenceRHIRef AsyncEndFence;

	const FTemporalAAHistory& InputHistory;
	FTemporalAAHistory* OutputHistory;
};

FRenderingCompositeOutputRef AddTemporalAADebugViewPass(FPostprocessContext& Context)
{
	check(Context.View.ViewState);

	FSceneViewState* ViewState = Context.View.ViewState;

	FTAAPassParameters Parameters(Context.View);

	FRCPassPostProcessTemporalAA* TemporalAAPass = Context.Graph.RegisterPass(new(FMemStack::Get()) FRCPassPostProcessTemporalAA(
		Context,
		Parameters,
		Context.View.PrevViewInfo.TemporalAAHistory,
		&ViewState->PrevFrameViewInfo.TemporalAAHistory));

	TemporalAAPass->SetInput(ePId_Input0, Context.FinalOutput);
	return FRenderingCompositeOutputRef(TemporalAAPass);
}

//////////////////////////////////////////////////////////////////////////