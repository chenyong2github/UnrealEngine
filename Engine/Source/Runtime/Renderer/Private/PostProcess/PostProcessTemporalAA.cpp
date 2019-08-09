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
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "PostProcessing.h"


#include "RenderGraph.h"
#include "SceneTextureParameters.h"
#include "PixelShaderUtils.h"


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


static float CatmullRom( float x )
{
	float ax = FMath::Abs(x);
	if( ax > 1.0f )
		return ( ( -0.5f * ax + 2.5f ) * ax - 4.0f ) *ax + 2.0f;
	else
		return ( 1.5f * ax - 2.5f ) * ax*ax + 1.0f;
}


BEGIN_SHADER_PARAMETER_STRUCT(FTAAShaderParameters,)
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

	// History resourrces
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryBuffer0)
	SHADER_PARAMETER_SAMPLER(SamplerState, HistoryBuffer0Sampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryBuffer1)
	SHADER_PARAMETER_SAMPLER(SamplerState, HistoryBuffer1Sampler)
	
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthBufferSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneVelocityBufferSampler)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()


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

class FTemporalAAPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTemporalAAPS);
	SHADER_USE_PARAMETER_STRUCT(FTemporalAAPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<
		FTAAPassConfigDim,
		FTAAFastDim,
		FTAAResponsiveDim>;
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAAShaderParameters, CommonParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// TAAU is compute shader only.
		if (IsTAAUpsamplingConfig(PermutationVector.Get<FTAAPassConfigDim>()))
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
		
		// DOF setup chain is full compute shader, no need for a pixel shader for TAA.
		if (IsDOFTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()))
		{
			return false;
		}

		// Responsive dimension is only for Main.
		if (PermutationVector.Get<FTAAResponsiveDim>() && !SupportsResponsiveDim(PermutationVector))
		{
			return false;
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static bool SupportsResponsiveDim(const FPermutationDomain& PermutationVector)
	{
		return PermutationVector.Get<FTAAPassConfigDim>() == ETAAPassConfig::Main;
	}
}; // class FTemporalAAPS

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
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAAShaderParameters, CommonParameters)
		
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


IMPLEMENT_GLOBAL_SHADER(FTemporalAAPS, "/Engine/Private/PostProcessTemporalAA.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FTemporalAACS, "/Engine/Private/PostProcessTemporalAA.usf", "MainCS", SF_Compute);


static void SetupSampleWeightParameters(FTAAShaderParameters* OutTAAParameters, const FTAAPassParameters& PassParameters, FVector2D TemporalJitterPixels)
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


static_assert(UE_ARRAY_COUNT(kTAAOutputNames) == int32(ETAAPassConfig::MAX), "Missing TAA output name.");
static_assert(UE_ARRAY_COUNT(kTAAPassNames) == int32(ETAAPassConfig::MAX), "Missing TAA pass name.");


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


FTAAOutputs FTAAPassParameters::AddTemporalAAPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FTemporalAAHistory& InputHistory,
	FTemporalAAHistory* OutputHistory) const
{
	check(Validate());

	// Number of render target in TAA history.
	const int32 RenderTargetCount = IsDOFTAAConfig(Pass) && FPostProcessing::HasAlphaChannelSupport() ? 2 : 1;
	
	// Whether this is main TAA pass;
	bool bIsMainPass = IsMainTAAConfig(Pass);
	
	// Whether to use camera cut shader permutation or not.
	const bool bCameraCut = !InputHistory.IsValid() || View.bCameraCut;

	FIntPoint OutputExtent = GetOutputExtent();

	// Src rectangle.
	FIntRect SrcRect = InputViewRect;
	FIntRect DestRect = OutputViewRect;
	FIntRect PracticableSrcRect = FIntRect::DivideAndRoundUp(SrcRect, ResolutionDivisor);
	FIntRect PracticableDestRect = FIntRect::DivideAndRoundUp(DestRect, ResolutionDivisor);

	// Name of the pass.
	const TCHAR* PassName = kTAAPassNames[static_cast<int32>(Pass)];

	// Setups common shader parameters
	FTAAShaderParameters CommonShaderParameters;
	{
		FIntPoint SrcSize = SceneColorInput->Desc.Extent;

		if (!IsTAAUpsamplingConfig(Pass))
			SetupSampleWeightParameters(&CommonShaderParameters, *this, View.TemporalJitterPixels);
	
		float ResDivisor = ResolutionDivisor;
		float ResDivisorInv = 1.0f / ResDivisor;

		CommonShaderParameters.ViewUniformBuffer = View.ViewUniformBuffer;
		CommonShaderParameters.CurrentFrameWeight = CVarTemporalAACurrentFrameWeight.GetValueOnRenderThread();
		CommonShaderParameters.bCameraCut = bCameraCut;

		CommonShaderParameters.SceneTextures = SceneTextures;
		CommonShaderParameters.SceneDepthBufferSampler = TStaticSamplerState<SF_Point>::GetRHI();
		CommonShaderParameters.SceneVelocityBufferSampler = TStaticSamplerState<SF_Point>::GetRHI();

		// Input buffer shader parameters
		{
			CommonShaderParameters.InputSceneColorSize = FVector4(
				SceneColorInput->Desc.Extent.X,
				SceneColorInput->Desc.Extent.Y,
				1.0f / float(SceneColorInput->Desc.Extent.X),
				1.0f / float(SceneColorInput->Desc.Extent.Y));
			CommonShaderParameters.InputSceneColor = SceneColorInput;
			CommonShaderParameters.InputSceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
			CommonShaderParameters.InputSceneMetadata = SceneMetadataInput;
			CommonShaderParameters.InputSceneMetadataSampler = TStaticSamplerState<SF_Point>::GetRHI();
		}

		CommonShaderParameters.OutputViewportSize = FVector4(
			PracticableDestRect.Width(), PracticableDestRect.Height(), 1.0f / float(PracticableDestRect.Width()), 1.0f / float(PracticableDestRect.Height()));
		CommonShaderParameters.OutputViewportRect = FVector4(PracticableDestRect.Min.X, PracticableDestRect.Min.Y, PracticableDestRect.Max.X, PracticableDestRect.Max.Y);

		// Set history shader parameters.
		{
			if (bCameraCut)
			{
				FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

				CommonShaderParameters.ScreenPosToHistoryBufferUV = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				CommonShaderParameters.ScreenPosAbsMax = FVector2D(0.0f, 0.0f);
				CommonShaderParameters.HistoryBufferUVMinMax = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
				CommonShaderParameters.HistoryBufferSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				CommonShaderParameters.HistoryBuffer0 = BlackDummy;
				CommonShaderParameters.HistoryBuffer1 = BlackDummy;
				
				// Remove dependency of the velocity buffer on camera cut, given it's going to be ignored by the shader.
				CommonShaderParameters.SceneTextures.SceneVelocityBuffer = BlackDummy;
			}
			else
			{
				FIntPoint ReferenceViewportOffset = InputHistory.ViewportRect.Min;
				FIntPoint ReferenceViewportExtent = InputHistory.ViewportRect.Size();
				FIntPoint ReferenceBufferSize = InputHistory.ReferenceBufferSize;

				float InvReferenceBufferSizeX = 1.f / float(InputHistory.ReferenceBufferSize.X);
				float InvReferenceBufferSizeY = 1.f / float(InputHistory.ReferenceBufferSize.Y);

				CommonShaderParameters.ScreenPosToHistoryBufferUV = FVector4(
					ReferenceViewportExtent.X * 0.5f * InvReferenceBufferSizeX,
					-ReferenceViewportExtent.Y * 0.5f * InvReferenceBufferSizeY,
					(ReferenceViewportExtent.X * 0.5f + ReferenceViewportOffset.X) * InvReferenceBufferSizeX,
					(ReferenceViewportExtent.Y * 0.5f + ReferenceViewportOffset.Y) * InvReferenceBufferSizeY);

				FIntPoint ViewportOffset = ReferenceViewportOffset / ResolutionDivisor;
				FIntPoint ViewportExtent = FIntPoint::DivideAndRoundUp(ReferenceViewportExtent, ResolutionDivisor);
				FIntPoint BufferSize = ReferenceBufferSize / ResolutionDivisor;

				CommonShaderParameters.ScreenPosAbsMax = FVector2D(1.0f - 1.0f / float(ViewportExtent.X), 1.0f - 1.0f / float(ViewportExtent.Y));

				float InvBufferSizeX = 1.f / float(BufferSize.X);
				float InvBufferSizeY = 1.f / float(BufferSize.Y);

				CommonShaderParameters.HistoryBufferUVMinMax = FVector4(
					(ViewportOffset.X + 0.5f) * InvBufferSizeX,
					(ViewportOffset.Y + 0.5f) * InvBufferSizeY,
					(ViewportOffset.X + ViewportExtent.X - 0.5f) * InvBufferSizeX,
					(ViewportOffset.Y + ViewportExtent.Y - 0.5f) * InvBufferSizeY);

				CommonShaderParameters.HistoryBufferSize = FVector4(BufferSize.X, BufferSize.Y, InvBufferSizeX, InvBufferSizeY);

				CommonShaderParameters.HistoryBuffer0 = GraphBuilder.RegisterExternalTexture(InputHistory.RT[0]);
				if (InputHistory.RT[1].IsValid())
					CommonShaderParameters.HistoryBuffer1 = GraphBuilder.RegisterExternalTexture(InputHistory.RT[1]);
			}

			CommonShaderParameters.HistoryBuffer0Sampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			CommonShaderParameters.HistoryBuffer1Sampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		}

		CommonShaderParameters.MaxViewportUVAndSvPositionToViewportUV = FVector4(
			(PracticableDestRect.Width() - 0.5f * ResDivisor) / float(PracticableDestRect.Width()),
			(PracticableDestRect.Height() - 0.5f * ResDivisor) / float(PracticableDestRect.Height()),
			ResDivisor / float(DestRect.Width()),
			ResDivisor / float(DestRect.Height()));

		CommonShaderParameters.HistoryPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;

		{
			float InvSizeX = 1.0f / float(SrcSize.X);
			float InvSizeY = 1.0f / float(SrcSize.Y);
			CommonShaderParameters.ViewportUVToInputBufferUV = FVector4(
				ResDivisorInv * InputViewRect.Width() * InvSizeX,
				ResDivisorInv * InputViewRect.Height() * InvSizeY,
				ResDivisorInv * InputViewRect.Min.X * InvSizeX,
				ResDivisorInv * InputViewRect.Min.Y * InvSizeY);
		}
		
		CommonShaderParameters.EyeAdaptation = GetEyeAdaptationTexture(GraphBuilder, View);
	}

	// Create outputs
	FTAAOutputs Outputs;
	{
		FRDGTextureDesc SceneColorDesc = FRDGTextureDesc::Create2DDesc(
			GetOutputExtent(),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | (bIsComputePass ? TexCreate_UAV : TexCreate_RenderTargetable),
			/* bInForceSeparateTargetAndShaderResource = */ false);

		Outputs.SceneColor = GraphBuilder.CreateTexture(
			SceneColorDesc,
			kTAAOutputNames[static_cast<int32>(Pass)],
			ERDGResourceFlags::MultiFrame);

		if (RenderTargetCount == 2)
		{
			Outputs.SceneMetadata = GraphBuilder.CreateTexture(
				SceneColorDesc,
				kTAAOutputNames[static_cast<int32>(Pass)],
				ERDGResourceFlags::MultiFrame);
		}

		if (bDownsample)
		{
			check(bIsComputePass);
			
			FRDGTextureDesc HalfResSceneColorDesc = FRDGTextureDesc::Create2DDesc(
				SceneColorDesc.Extent / 2,
				DownsampleOverrideFormat != PF_Unknown ? DownsampleOverrideFormat : SceneColorInput->Desc.Format,
				FClearValueBinding::Black,
				/* InFlags = */ TexCreate_None,
				/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_Transient | (bIsComputePass ? TexCreate_UAV : TexCreate_RenderTargetable),
				/* bInForceSeparateTargetAndShaderResource = */ false);

			Outputs.DownsampledSceneColor = GraphBuilder.CreateTexture(HalfResSceneColorDesc, TEXT("SceneColorHalfRes"));
		}
	}

	RDG_GPU_STAT_SCOPE(GraphBuilder, TAA);

	if (bIsComputePass)
	{
		FTemporalAACS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTAAPassConfigDim>(Pass);
		PermutationVector.Set<FTAAFastDim>(bUseFast);
		PermutationVector.Set<FTAADownsampleDim>(bDownsample);
		PermutationVector.Set<FTAAUpsampleFilteredDim>(true);

		if (IsTAAUpsamplingConfig(Pass))
		{
			const bool bUpsampleFiltered = CVarTemporalAAUpsampleFiltered.GetValueOnRenderThread() != 0 || Pass != ETAAPassConfig::MainUpsampling;
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
				Pass == ETAAPassConfig::MainSuperSampling)
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
		PassParameters->CommonParameters = CommonShaderParameters;

		// Temporal upsample specific shader parameters.
		{
			// Temporal AA upscale specific params.
			float InputViewSizeInvScale = ResolutionDivisor;
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
				PassParameters->OutComputeTex1 = GraphBuilder.CreateUAV(Outputs.SceneMetadata);
			if (Outputs.DownsampledSceneColor)
				PassParameters->OutComputeTexDownsampled = GraphBuilder.CreateUAV(Outputs.DownsampledSceneColor);
		}

		TShaderMapRef<FTemporalAACS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TAA %s CS%s %dx%d -> %dx%d",
				PassName, bUseFast ? TEXT(" Fast") : TEXT(""),
				PracticableSrcRect.Width(), PracticableSrcRect.Height(),
				PracticableDestRect.Width(), PracticableDestRect.Height()),
			*ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(PracticableDestRect.Size(), GTemporalAATileSizeX));
	}
	else
	{
		check(!IsTAAUpsamplingConfig(Pass));

		// Whether to use responsive stencil test.
		bool bUseResponsiveStencilTest = Pass == ETAAPassConfig::Main && !bIsComputePass && !bCameraCut;
	
		FTemporalAAPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTemporalAAPS::FParameters>();
		PassParameters->CommonParameters = CommonShaderParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(
			Outputs.SceneColor,
			ERenderTargetLoadAction::ENoAction,
			ERenderTargetStoreAction::EStore);
		
		if (Outputs.SceneMetadata)
		{
			PassParameters->RenderTargets[1] = FRenderTargetBinding(
				Outputs.SceneMetadata,
				ERenderTargetLoadAction::ENoAction,
				ERenderTargetStoreAction::EStore);
		}

		if (bUseResponsiveStencilTest)
		{
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
				SceneTextures.SceneDepthBuffer,
				ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction,
				ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore,
				FExclusiveDepthStencil::DepthRead_StencilRead);
		}
		
		FTemporalAAPS::FPermutationDomain BasePermutationVector;
		BasePermutationVector.Set<FTAAPassConfigDim>(Pass);
		BasePermutationVector.Set<FTAAFastDim>(bUseFast);

		TShaderMapRef<FTemporalAAPS> PixelShader(View.ShaderMap, BasePermutationVector);
		ClearUnusedGraphResources(*PixelShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("TAA %s PS%s %dx%d",
				PassName, bUseFast ? TEXT(" Fast") : TEXT(""),
				PracticableDestRect.Width(), PracticableDestRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, PracticableDestRect, bCameraCut, BasePermutationVector, bUseResponsiveStencilTest](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(PracticableDestRect.Min.X, PracticableDestRect.Min.Y, 0.0f, PracticableDestRect.Max.X, PracticableDestRect.Max.Y, 1.0f);

			FTemporalAAPS::FPermutationDomain PermutationVector = BasePermutationVector;

			// Lambda to draw pixel shader.
			auto DrawTAAPixelShader = [&](FRHIDepthStencilState* DepthStencilState)
			{
				TShaderMapRef<FTemporalAAPS> TAAPixelShader(View.ShaderMap, PermutationVector);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, *TAAPixelShader, GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = DepthStencilState;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, *TAAPixelShader, TAAPixelShader->GetPixelShader(), *PassParameters);
				
				FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
			};
	
			if (bUseResponsiveStencilTest)
			{
				// Normal temporal feedback
				// Draw to pixels where stencil == 0
				FRHIDepthStencilState* DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK>::GetRHI();

				DrawTAAPixelShader(DepthStencilState);

				// Responsive feedback for tagged pixels
				// Draw to pixels where stencil != 0
				DepthStencilState = TStaticDepthStencilState<
					false, CF_Always,
					true, CF_NotEqual, SO_Keep, SO_Keep, SO_Keep,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK>::GetRHI();

				PermutationVector.Set<FTAAResponsiveDim>(true);
				DrawTAAPixelShader(DepthStencilState);
			}
			else
			{
				DrawTAAPixelShader(TStaticDepthStencilState<false, CF_Always>::GetRHI());
			}
		});
	}
	
	if (!View.bViewStateIsReadOnly)
	{
		OutputHistory->SafeRelease();

		GraphBuilder.QueueTextureExtraction(Outputs.SceneColor, &OutputHistory->RT[0]);
		if (Outputs.SceneMetadata)
			GraphBuilder.QueueTextureExtraction(Outputs.SceneMetadata, &OutputHistory->RT[1]);

		OutputHistory->ViewportRect = DestRect;
		OutputHistory->ReferenceBufferSize = OutputExtent * ResolutionDivisor;
	}

	return Outputs;
} // AddTemporalAAPass()


FRCPassPostProcessTemporalAA::FRCPassPostProcessTemporalAA(
	const FPostprocessContext& Context,
	const FTAAPassParameters& InParameters,
	const FTemporalAAHistory& InInputHistory,
	FTemporalAAHistory* OutOutputHistory)
	: SavedParameters(InParameters)
	, InputHistory(InInputHistory)
	, OutputHistory(OutOutputHistory)
{
	check(SavedParameters.Validate());

	bIsComputePass = SavedParameters.bIsComputePass;
	bPreferAsyncCompute = false;
}

void FRCPassPostProcessTemporalAA::Process(FRenderingCompositePassContext& Context)
{
	WaitForInputPassComputeFences(Context.RHICmdList);

	AsyncEndFence = FComputeFenceRHIRef();
	
	FRDGBuilder GraphBuilder(Context.RHICmdList);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	// FPostProcessing::Process() does a AdjustGBufferRefCount(RHICmdList, -1), therefore need to pass down reference on velocity buffer manually.
	if (FRDGTextureRef SceneVelocityBuffer = CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input2, TEXT("SceneVelocity")))
	{
		SceneTextures.SceneVelocityBuffer = SceneVelocityBuffer;
	}

	FTAAPassParameters Parameters = SavedParameters;
	Parameters.SceneColorInput = CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
	Parameters.SceneMetadataInput = CreateRDGTextureForOptionalInput(GraphBuilder, ePId_Input1, TEXT("SceneColor"));

	FTAAOutputs Outputs = Parameters.AddTemporalAAPass(
		GraphBuilder,
		SceneTextures, Context.View,
		InputHistory, /* out */ OutputHistory);
		
	ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, Outputs.SceneColor);
	if (Outputs.SceneMetadata)
		ExtractRDGTextureForOutput(GraphBuilder, ePId_Output1, Outputs.SceneMetadata);
	if (Outputs.DownsampledSceneColor)
		ExtractRDGTextureForOutput(GraphBuilder, ePId_Output2, Outputs.DownsampledSceneColor);

	GraphBuilder.Execute();

	// Changes the view rectangle of the scene color and reference buffer size when doing temporal upsample for the
	// following passes to still work.
	if (Parameters.Pass == ETAAPassConfig::MainUpsampling || Parameters.Pass == ETAAPassConfig::MainSuperSampling)
	{
		Context.SceneColorViewRect = Parameters.OutputViewRect;
		Context.ReferenceBufferSize = Parameters.GetOutputExtent() * Parameters.ResolutionDivisor;
	}
}

FPooledRenderTargetDesc FRCPassPostProcessTemporalAA::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	// ExtractRDGTextureForOutput() is doing this work for us already.
	return FPooledRenderTargetDesc();
}
