// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/TemporalAA.h"
#include "PostProcess/PostProcessTonemap.h"
#include "PostProcess/PostProcessing.h"
#include "ClearQuad.h"
#include "PostProcessing.h"
#include "SceneTextureParameters.h"
#include "PixelShaderUtils.h"
#include "ScenePrivate.h"
#include "RendererModule.h"

#define COMPILE_TSR_DEBUG_PASSES (!UE_BUILD_SHIPPING)

namespace
{

TAutoConsoleVariable<float> CVarTSRHistorySP(
	TEXT("r.TSR.History.ScreenPercentage"),
	100.0f,
	TEXT("Size of TSR's history."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRR11G11B10History(
	TEXT("r.TSR.History.R11G11B10"), 1,
	TEXT("Select the bitdepth of the history."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRHistoryUpdateQuality(
	TEXT("r.TSR.History.UpdateQuality"), 3,
	TEXT("Select the quality of the history update."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRHistorySeparateTranslucency(
	TEXT("r.TSR.History.SeparateTranslucency"), 1,
	TEXT("Whether separate translucency should be accumulated separatly."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRWaveOps(
	TEXT("r.TSR.WaveOps"), 1,
	TEXT("Whether to use wave ops in the shading rejection heuristics"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRFilterShadingRejection(
	TEXT("r.TSR.ShadingRejection.SpatialFilter"), 1,
	TEXT("Whether the shading rejection should have spatial statistical filtering pass to reduce flickering (default = 1).\n")
	TEXT(" 0: Disabled;\n")
	TEXT(" 1: Spatial filter pass is run at lower resolution than CompareHistory pass (default);\n")
	TEXT(" 2: Spatial filter pass is run CompareHistory pass resolution to improve stability."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRRejectionAntiAliasingQuality(
	TEXT("r.TSR.RejectionAntiAliasingQuality"), 3,
	TEXT("Controls the quality of spatial anti-aliasing on history rejection (default=1)."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRAsyncCompute(
	TEXT("r.TSR.AsyncCompute"), 0,
	TEXT("Whether to run TSR on async compute. Some TSR passes can overlap with previous passe.\n")
	TEXT(" 0: Disabled (default);\n")
	TEXT(" 1: Only ClearPrevTextures pass;\n")
	TEXT(" 2: Only ClearPrevTextures through DecimateHistory passes;\n")
	TEXT(" 3: All passes;"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTSRTranslucencyHighlightLuminance(
	TEXT("r.TSR.Translucency.HighlightLuminance"), -1.0f,
	TEXT("Sets the liminance at which translucency is considered an highlights (default=-1.0)."),
	ECVF_RenderThreadSafe);

//TAutoConsoleVariable<int32> CVarTSRTranslucencyPreviousFrameRejection(
//	TEXT("r.TSR.Translucency.PreviousFrameRejection"), 0,
//	TEXT("Enable heuristic to reject Separate translucency based on previous frame translucency."),
//	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSREnableResponiveAA(
	TEXT("r.TSR.Translucency.EnableResponiveAA"), 1,
	TEXT("Whether the responsive AA should keep history fully clamped."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTSRWeightClampingPixelSpeed(
	TEXT("r.TSR.Velocity.WeightClampingPixelSpeed"), 1.0f,
	TEXT("Defines the pixel velocity at which the the high frequencies of the history get's their contributing weight clamped. ")
	TEXT("Smallest reduce blur in movement (Default = 1.0f)."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTSRVelocityExtrapolation(
	TEXT("r.TSR.Velocity.Extrapolation"), 1.0f,
	TEXT("Defines how much the velocity should be extrapolated on geometric discontinuities (Default = 1.0f)."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

#if COMPILE_TSR_DEBUG_PASSES

TAutoConsoleVariable<int32> CVarTSRDebugArraySize(
	TEXT("r.TSR.Debug.ArraySize"), 1,
	TEXT("Size of array for the TSR.Debug.* RDG textures"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTSRSetupDebugPasses(
	TEXT("r.TSR.Debug.SetupExtraPasses"), 0,
	TEXT("Whether to enable the debug passes"),
	ECVF_RenderThreadSafe);

#endif

BEGIN_SHADER_PARAMETER_STRUCT(FTSRCommonParameters, )
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, InputInfo)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, HistoryInfo)

	SHADER_PARAMETER(FIntPoint, InputPixelPosMin)
	SHADER_PARAMETER(FIntPoint, InputPixelPosMax)
	SHADER_PARAMETER(FScreenTransform, InputPixelPosToScreenPos)

	SHADER_PARAMETER(FVector2f, InputJitter)
	SHADER_PARAMETER(int32, bCameraCut)
	SHADER_PARAMETER(FVector2f, ScreenVelocityToInputPixelVelocity)
	SHADER_PARAMETER(FVector2f, InputPixelVelocityToScreenVelocity)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTSRHistoryArrayIndices, )
	SHADER_PARAMETER(int32, HighFrequency)
	SHADER_PARAMETER(int32, Translucency)
	SHADER_PARAMETER(int32, Size)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTSRHistoryTextures, )
	SHADER_PARAMETER_STRUCT(FTSRHistoryArrayIndices, ArrayIndices)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, ColorArray)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Metadata)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SubpixelDetails)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TranslucencyAlpha)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Guide)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTSRHistorySRVs, )
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, HighFrequency)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, Metadata)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SubpixelDetails)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, Translucency)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, TranslucencyAlpha)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Guide)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTSRHistoryUAVs, )
	SHADER_PARAMETER_STRUCT(FTSRHistoryArrayIndices, ArrayIndices)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, ColorArray)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, Metadata)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SubpixelDetails)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, TranslucencyAlpha)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTSRPrevHistoryParameters, )
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PrevHistoryInfo)
	SHADER_PARAMETER(FScreenTransform, ScreenPosToPrevHistoryBufferUV)
	SHADER_PARAMETER(FScreenTransform, ScreenPosToPrevSubpixelDetails)
	SHADER_PARAMETER(FVector2f, PrevSubpixelDetailsExtent)
	SHADER_PARAMETER(float, HistoryPreExposureCorrection)
END_SHADER_PARAMETER_STRUCT()

enum class ETSRHistoryFormatBits : uint32
{
	None = 0,
	Translucency = 1 << 1,
};
ENUM_CLASS_FLAGS(ETSRHistoryFormatBits);

FTSRHistoryArrayIndices TranslateHistoryFormatBitsToArrayIndices(ETSRHistoryFormatBits HistoryFormatBits)
{
	FTSRHistoryArrayIndices ArrayIndices;
	ArrayIndices.Size = 1;
	ArrayIndices.HighFrequency = 0;
	ArrayIndices.Translucency = -1;

	if (EnumHasAnyFlags(HistoryFormatBits, ETSRHistoryFormatBits::Translucency))
	{
		ArrayIndices.Translucency = ArrayIndices.Size++;
	}

	return ArrayIndices;
}

FTSRHistorySRVs CreateSRVs(FRDGBuilder& GraphBuilder, const FTSRHistoryTextures& Textures)
{
	FTSRHistorySRVs SRVs;
	SRVs.HighFrequency = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForSlice(Textures.ColorArray, Textures.ArrayIndices.HighFrequency));
	SRVs.Metadata = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Textures.Metadata));
	SRVs.SubpixelDetails = Textures.SubpixelDetails;
	if (Textures.ArrayIndices.Translucency >= 0)
	{
		SRVs.Translucency = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForSlice(Textures.ColorArray, Textures.ArrayIndices.Translucency));
		SRVs.TranslucencyAlpha = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Textures.TranslucencyAlpha));
	}
	SRVs.Guide = Textures.Guide;
	return SRVs;
}

FTSRHistoryUAVs CreateUAVs(FRDGBuilder& GraphBuilder, const FTSRHistoryTextures& Textures)
{
	FTSRHistoryUAVs UAVs;
	UAVs.ArrayIndices = Textures.ArrayIndices;
	UAVs.ColorArray = GraphBuilder.CreateUAV(Textures.ColorArray);
	UAVs.Metadata = GraphBuilder.CreateUAV(Textures.Metadata);
	UAVs.SubpixelDetails = GraphBuilder.CreateUAV(Textures.SubpixelDetails);
	if (Textures.TranslucencyAlpha)
	{
		UAVs.TranslucencyAlpha = GraphBuilder.CreateUAV(Textures.TranslucencyAlpha);
	}
	return UAVs;
}

class FTSRShader : public FGlobalShader
{
public:
	FTSRShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{ }

	FTSRShader()
	{ }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return SupportsTSR(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		if (FDataDrivenShaderPlatformInfo::GetSupportsRealTypes(Parameters.Platform) == ERHIFeatureSupport::RuntimeGuaranteed)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowRealTypes);
		}
		OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
}; // class FTemporalSuperResolutionShader

class FTSRClearPrevTexturesCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRClearPrevTexturesCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRClearPrevTexturesCS, FTSRShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PrevUseCountOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PrevClosestDepthOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRClearPrevTexturesCS

class FTSRDilateVelocityCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRDilateVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRDilateVelocityCS, FTSRShader);

	class FMotionBlurDirectionsDim : SHADER_PERMUTATION_INT("DIM_MOTION_BLUR_DIRECTIONS", 3);
	using FPermutationDomain = TShaderPermutationDomain<FMotionBlurDirectionsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVelocityFlattenParameters, VelocityFlattenParameters)

		SHADER_PARAMETER(FVector2f, PrevOutputBufferUVMin)
		SHADER_PARAMETER(FVector2f, PrevOutputBufferUVMax)
		SHADER_PARAMETER(float, WorldDepthToDepthError)
		SHADER_PARAMETER(float, VelocityExtrapolationMultiplier)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneVelocityTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DilatedVelocityOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ClosestDepthOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PrevUseCountOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PrevClosestDepthOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ParallaxFactorOutput)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, VelocityFlattenOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, VelocityTileOutput0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, VelocityTileOutput1)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRDilateVelocityCS

class FTSRDecimateHistoryCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRDecimateHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRDecimateHistoryCS, FTSRShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER(FMatrix44f, RotationalClipToPrevClip)
		SHADER_PARAMETER(float, WorldDepthToPixelWorldRadius)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DilatedVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevUseCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevClosestDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxFactorTexture)

		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRPrevHistoryParameters, PrevHistoryParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevHistorySubpixelDetails)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevHistoryGuide)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PrevGuideInfo)
		SHADER_PARAMETER(FScreenTransform, InputPixelPosToReprojectScreenPos)
		SHADER_PARAMETER(FScreenTransform, ScreenPosToPrevHistoryGuideBufferUV)
		SHADER_PARAMETER(FVector3f, HistoryGuideQuantizationError)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ReprojectedHistoryGuideOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HoleFilledVelocityOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HoleFilledVelocityMaskOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ParallaxRejectionMaskOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HistorySubpixelDetailsOutput)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRDecimateHistoryCS

class FTSRCompareTranslucencyCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRCompareTranslucencyCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRCompareTranslucencyCS, FTSRShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)

		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, TranslucencyInfo)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PrevTranslucencyInfo)
		SHADER_PARAMETER(float, PrevTranslucencyPreExposureCorrection)
		SHADER_PARAMETER(float, TranslucencyHighlightLuminance)

		SHADER_PARAMETER(FScreenTransform, ScreenPosToPrevTranslucencyTextureUV)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DilatedVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TranslucencyTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevTranslucencyTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, TranslucencyRejectionOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRCompareTranslucencyCS

class FTSRRejectShadingCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRRejectShadingCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRRejectShadingCS, FTSRShader);

	class FUseWaveOps : SHADER_PERMUTATION_BOOL("DIM_USE_WAVE_OPS");
	class FSeparateTranslucencyDim : SHADER_PERMUTATION_BOOL("DIM_SEPARATE_TRANSLUCENCY");

	using FPermutationDomain = TShaderPermutationDomain<FUseWaveOps, FSeparateTranslucencyDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER(FScreenTransform, InputPixelPosToTranslucencyTextureUV)
		SHADER_PARAMETER(FVector2f, TranslucencyTextureUVMin)
		SHADER_PARAMETER(FVector2f, TranslucencyTextureUVMax)
		SHADER_PARAMETER(FVector3f, HistoryGuideQuantizationError)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneTranslucencyTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReprojectedHistoryGuideTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxRejectionMaskTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HistoryGuideOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HistoryRejectionOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, InputSceneColorLdrLumaOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		ERHIFeatureSupport WaveOpsSupport = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform);
		if (PermutationVector.Get<FUseWaveOps>())
		{
			if (WaveOpsSupport == ERHIFeatureSupport::Unsupported)
			{
				return false;
			}
		}
		else
		{
			if (WaveOpsSupport == ERHIFeatureSupport::RuntimeGuaranteed)
			{
				return false;
			}
		}

		return FTSRShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		FTSRShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);

		if (PermutationVector.Get<FUseWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
}; // class FTSRRejectShadingCS

class FTSRPostfilterRejectionCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRPostfilterRejectionCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRPostfilterRejectionCS, FTSRShader);

	class FOutputHalfRes : SHADER_PERMUTATION_BOOL("DIM_OUTPUT_HALF_RES");
	using FPermutationDomain = TShaderPermutationDomain<FOutputHalfRes>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntRect, HistoryRejectionViewport)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryRejectionTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HistoryRejectionOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRPostfilterRejectionCS

class FTSRDilateRejectionCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRDilateRejectionCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRDilateRejectionCS, FTSRShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntRect, HistoryRejectionViewport)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryRejectionTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DilatedHistoryRejectionOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRDilateRejectionCS

class FTSRSpatialAntiAliasingCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRSpatialAntiAliasingCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRSpatialAntiAliasingCS, FTSRShader);

	class FQualityDim : SHADER_PERMUTATION_INT("DIM_QUALITY_PRESET", 3);

	using FPermutationDomain = TShaderPermutationDomain<FQualityDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorLdrLumaTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, AntiAliasingOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, NoiseFilteringOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// There is no Quality=0 because the pass doesn't get setup.
		if (PermutationVector.Get<FQualityDim>() == 0)
		{
			return false;
		}

		return FTSRShader::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTSRShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
}; // class FTSRSpatialAntiAliasingCS

class FTSRFilterAntiAliasingCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRFilterAntiAliasingCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRFilterAntiAliasingCS, FTSRShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AntiAliasingTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, AntiAliasingOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRFilterAntiAliasingCS

class FTSRUpdateHistoryCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRUpdateHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRUpdateHistoryCS, FTSRShader);

	enum class EQuality
	{
		Low,
		Medium,
		High,
		Epic,
		MAX
	};

	class FQualityDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_UPDATE_QUALITY", EQuality);
	class FSeparateTranslucencyDim : SHADER_PERMUTATION_BOOL("DIM_SEPARATE_TRANSLUCENCY");

	using FPermutationDomain = TShaderPermutationDomain<FQualityDim, FSeparateTranslucencyDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputSceneStencilTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneTranslucencyTexture)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryRejectionTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TranslucencyRejectionTexture)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DilatedVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxFactorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxRejectionMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AntiAliasingTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NoiseFilteringTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HoleFilledVelocityMaskTexture)

		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, TranslucencyInfo)
		SHADER_PARAMETER(FIntPoint, TranslucencyPixelPosMin)
		SHADER_PARAMETER(FIntPoint, TranslucencyPixelPosMax)

		SHADER_PARAMETER(FScreenTransform, HistoryPixelPosToScreenPos)
		SHADER_PARAMETER(FScreenTransform, HistoryPixelPosToInputPPCo)
		SHADER_PARAMETER(FScreenTransform, HistoryPixelPosToTranslucencyPPCo)

		SHADER_PARAMETER(FVector3f, HistoryQuantizationError)
		SHADER_PARAMETER(float, MinTranslucencyRejection)
		SHADER_PARAMETER(float, InvWeightClampingPixelSpeed)
		SHADER_PARAMETER(float, InputToHistoryFactor)
		SHADER_PARAMETER(int32, ResponsiveStencilMask)
		SHADER_PARAMETER(int32, bGenerateOutputMip1)
		SHADER_PARAMETER(int32, bHasSeparateTranslucency)

		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRPrevHistoryParameters, PrevHistoryParameters)
		SHADER_PARAMETER_STRUCT(FTSRHistorySRVs, PrevHistory)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SceneColorOutputMip0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SceneColorOutputMip1)
		SHADER_PARAMETER_STRUCT(FTSRHistoryUAVs, HistoryOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTSRShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
}; // class FTSRUpdateHistoryCS

class FTSRResolveHistoryCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRResolveHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRResolveHistoryCS, FTSRShader);

	using FPermutationDomain = TShaderPermutationDomain<FTSRUpdateHistoryCS::FSeparateTranslucencyDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER(FScreenTransform, DispatchThreadToHistoryPixelPos)
		SHADER_PARAMETER(FIntPoint, OutputViewRectMin)
		SHADER_PARAMETER(FIntPoint, OutputViewRectMax)
		SHADER_PARAMETER(int32, bGenerateOutputMip1)
		SHADER_PARAMETER(float, HistoryValidityMultiply)

		SHADER_PARAMETER_STRUCT(FTSRHistorySRVs, History)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SceneColorOutputMip0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SceneColorOutputMip1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTSRShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
	}
}; // class FTSRResolveHistoryCS

#if COMPILE_TSR_DEBUG_PASSES

class FTSRDebugHistoryCS : public FTSRShader
{
	DECLARE_GLOBAL_SHADER(FTSRDebugHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTSRDebugHistoryCS, FTSRShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTSRCommonParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT(FTSRHistorySRVs, History)
		SHADER_PARAMETER_STRUCT(FTSRHistorySRVs, PrevHistory)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTSRDebugHistoryCS

#endif

IMPLEMENT_GLOBAL_SHADER(FTSRClearPrevTexturesCS,     "/Engine/Private/TemporalSuperResolution/TSRClearPrevTextures.usf",     "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRDilateVelocityCS,        "/Engine/Private/TemporalSuperResolution/TSRDilateVelocity.usf",        "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRDecimateHistoryCS,       "/Engine/Private/TemporalSuperResolution/TSRDecimateHistory.usf",       "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRCompareTranslucencyCS,   "/Engine/Private/TemporalSuperResolution/TSRCompareTranslucency.usf",   "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRRejectShadingCS,         "/Engine/Private/TemporalSuperResolution/TSRRejectShading.usf",         "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRPostfilterRejectionCS,   "/Engine/Private/TemporalSuperResolution/TSRPostfilterRejection.usf",   "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRDilateRejectionCS,       "/Engine/Private/TemporalSuperResolution/TSRDilateRejection.usf",       "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRSpatialAntiAliasingCS,   "/Engine/Private/TemporalSuperResolution/TSRSpatialAntiAliasing.usf",   "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRFilterAntiAliasingCS,    "/Engine/Private/TemporalSuperResolution/TSRFilterAntiAliasing.usf",    "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRUpdateHistoryCS,         "/Engine/Private/TemporalSuperResolution/TSRUpdateHistory.usf",         "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTSRResolveHistoryCS,        "/Engine/Private/TemporalSuperResolution/TSRResolveHistory.usf",        "MainCS", SF_Compute);

#if COMPILE_TSR_DEBUG_PASSES
IMPLEMENT_GLOBAL_SHADER(FTSRDebugHistoryCS,          "/Engine/Private/TemporalSuperResolution/TSRDebugHistory.usf",          "MainCS", SF_Compute);
#endif

DECLARE_GPU_STAT(TemporalSuperResolution)

} //! namespace

FVector3f ComputePixelFormatQuantizationError(EPixelFormat PixelFormat);

bool ComposeSeparateTranslucencyInTSR(const FViewInfo& View)
{
	return true;
}

static FRDGTextureUAVRef CreateDummyUAV(FRDGBuilder& GraphBuilder, EPixelFormat PixelFormat)
{
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		FIntPoint(1, 1),
		PixelFormat,
		FClearValueBinding::None,
		/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef DummyTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.DummyOutput"));
	GraphBuilder.RemoveUnusedTextureWarning(DummyTexture);

	return GraphBuilder.CreateUAV(DummyTexture);
};

ITemporalUpscaler::FOutputs AddTemporalSuperResolutionPasses(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const ITemporalUpscaler::FPassInputs& PassInputs)
{
	const FTSRHistory& InputHistory = View.PrevViewInfo.TSRHistory;

#if COMPILE_TSR_DEBUG_PASSES
	const bool bSetupDebugPasses = CVarTSRSetupDebugPasses.GetValueOnRenderThread() != 0;
#endif

	// Whether to use wave ops optimizations.
	const ERHIFeatureSupport WaveOpsSupport = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(View.GetShaderPlatform());
	const bool bUseWaveOps = (CVarTSRWaveOps.GetValueOnRenderThread() != 0 && GRHISupportsWaveOperations && WaveOpsSupport == ERHIFeatureSupport::RuntimeDependent) || WaveOpsSupport == ERHIFeatureSupport::RuntimeGuaranteed;

	// Whether alpha channel is supported.
	const bool bSupportsAlpha = IsPostProcessingWithAlphaChannelSupported();

	// Whether separate translucency should be temporarily accumulated separatly.
	const bool bAccumulateTranslucencySeparately = CVarTSRHistorySeparateTranslucency.GetValueOnRenderThread() != 0;

	// whether TSR passes can run on async compute.
	int32 AsyncComputePasses = GSupportsEfficientAsyncCompute ? CVarTSRAsyncCompute.GetValueOnRenderThread() : 0;

	ETSRHistoryFormatBits HistoryFormatBits = ETSRHistoryFormatBits::None;
	{
		if (bAccumulateTranslucencySeparately)
		{
			HistoryFormatBits = ETSRHistoryFormatBits::Translucency;
		}
	}

	// Whether to use camera cut shader permutation or not.
	bool bCameraCut = !InputHistory.IsValid() || View.bCameraCut || ETSRHistoryFormatBits(InputHistory.FormatBit) != HistoryFormatBits;

	FTSRUpdateHistoryCS::EQuality UpdateHistoryQuality = FTSRUpdateHistoryCS::EQuality(FMath::Clamp(CVarTSRHistoryUpdateQuality.GetValueOnRenderThread(), 0, int32(FTSRUpdateHistoryCS::EQuality::MAX) - 1));

	bool bIsSeperateTranslucyTexturesValid = PassInputs.PostDOFTranslucencyResources.IsValid();

	const bool bRejectSeparateTranslucency = false; // bIsSeperateTranslucyTexturesValid && CVarTSRTranslucencyPreviousFrameRejection.GetValueOnRenderThread() != 0;

	EPixelFormat ColorFormat = bSupportsAlpha ? PF_FloatRGBA : PF_FloatR11G11B10;

	int32 RejectionAntiAliasingQuality = FMath::Clamp(CVarTSRRejectionAntiAliasingQuality.GetValueOnRenderThread(), 1, 2);
	if (UpdateHistoryQuality == FTSRUpdateHistoryCS::EQuality::Low)
	{
		RejectionAntiAliasingQuality = 0; 
	}

	enum class ERejectionPostFilter : uint8
	{
		Disabled,
		PostRejectionDownsample,
		PreRejectionDownsample,
	};

	ERejectionPostFilter PostFilter = ERejectionPostFilter(FMath::Clamp(CVarTSRFilterShadingRejection.GetValueOnRenderThread(), 2, 2)); // TODO(TSR)

	FIntPoint InputExtent = PassInputs.SceneColorTexture->Desc.Extent;
	FIntRect InputRect = View.ViewRect;

	FIntPoint RejectionExtent = InputExtent / 2;
	FIntRect RejectionRect = FIntRect(InputRect.Min / 2, InputRect.Min / 2 + FIntPoint::DivideAndRoundUp(InputRect.Size(), 2));

	FIntPoint OutputExtent;
	FIntRect OutputRect;
	if (View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale)
	{
		OutputRect.Min = FIntPoint(0, 0);
		OutputRect.Max = View.GetSecondaryViewRectSize();

		FIntPoint QuantizedPrimaryUpscaleViewSize;
		QuantizeSceneBufferSize(OutputRect.Max, QuantizedPrimaryUpscaleViewSize);

		OutputExtent = FIntPoint(
			FMath::Max(InputExtent.X, QuantizedPrimaryUpscaleViewSize.X),
			FMath::Max(InputExtent.Y, QuantizedPrimaryUpscaleViewSize.Y));
	}
	else
	{
		OutputRect.Min = FIntPoint(0, 0);
		OutputRect.Max = View.ViewRect.Size();
		OutputExtent = InputExtent;
	}

	FIntPoint HistoryExtent;
	FIntPoint HistorySize;
	{
		float MaxHistoryUpscaleFactor = FMath::Max(float(GMaxTextureDimensions) / float(FMath::Max(OutputRect.Width(), OutputRect.Height())), 1.0f);

		float HistoryUpscaleFactor = FMath::Clamp(CVarTSRHistorySP.GetValueOnRenderThread() / 100.0f, 1.0f, 2.0f);
		if (HistoryUpscaleFactor > MaxHistoryUpscaleFactor)
		{
			HistoryUpscaleFactor = 1.0f;
		}
		
		HistorySize = FIntPoint(
			FMath::CeilToInt(OutputRect.Width() * HistoryUpscaleFactor),
			FMath::CeilToInt(OutputRect.Height() * HistoryUpscaleFactor));

		FIntPoint QuantizedHistoryViewSize;
		QuantizeSceneBufferSize(HistorySize, QuantizedHistoryViewSize);

		HistoryExtent = FIntPoint(
			FMath::Max(InputExtent.X, QuantizedHistoryViewSize.X),
			FMath::Max(InputExtent.Y, QuantizedHistoryViewSize.Y));
	}

	float ScreenPercentage = float(InputRect.Width()) / float(OutputRect.Width());
	float InvScreenPercentage = float(OutputRect.Width()) / float(InputRect.Width());

	RDG_EVENT_SCOPE(GraphBuilder, "TemporalSuperResolution(%s) %dx%d -> %dx%d",
		bSupportsAlpha ? TEXT("Alpha") : TEXT(""),
		InputRect.Width(), InputRect.Height(),
		OutputRect.Width(), OutputRect.Height());
	RDG_GPU_STAT_SCOPE(GraphBuilder, TemporalSuperResolution);

	FRDGTextureRef BlackUintDummy = GSystemTextures.GetZeroUIntDummy(GraphBuilder);
	FRDGTextureRef BlackDummy = GSystemTextures.GetBlackDummy(GraphBuilder);
	FRDGTextureRef BlackAlphaOneDummy = GSystemTextures.GetBlackAlphaOneDummy(GraphBuilder);
	FRDGTextureRef WhiteDummy = GSystemTextures.GetWhiteDummy(GraphBuilder);

	FIntRect SeparateTranslucencyRect = FIntRect(0, 0, 1, 1);
	FRDGTextureRef SeparateTranslucencyTexture = BlackAlphaOneDummy;
	bool bHasSeparateTranslucency = PassInputs.PostDOFTranslucencyResources.IsValid();
#if WITH_EDITOR
	// Do not composite translucency if we are visualizing a buffer, unless it is the overview mode.
	static FName OverviewName = FName(TEXT("Overview"));
	bHasSeparateTranslucency &= 
		   (!View.Family->EngineShowFlags.VisualizeBuffer || (View.Family->EngineShowFlags.VisualizeBuffer && View.CurrentBufferVisualizationMode == OverviewName))
		&& (!View.Family->EngineShowFlags.VisualizeNanite || (View.Family->EngineShowFlags.VisualizeNanite && View.CurrentNaniteVisualizationMode == OverviewName))
		&& (!View.Family->EngineShowFlags.VisualizeLumen  || (View.Family->EngineShowFlags.VisualizeLumen  && View.CurrentLumenVisualizationMode  == OverviewName));
#endif
	if (bHasSeparateTranslucency)
	{
		SeparateTranslucencyTexture = PassInputs.PostDOFTranslucencyResources.ColorTexture.Resolve;
		SeparateTranslucencyRect = PassInputs.PostDOFTranslucencyResources.ViewRect;
	}

	FTSRCommonParameters CommonParameters;
	{
		CommonParameters.InputInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			InputExtent, InputRect));
		CommonParameters.InputPixelPosMin = CommonParameters.InputInfo.ViewportMin;
		CommonParameters.InputPixelPosMax = CommonParameters.InputInfo.ViewportMax - 1;
		CommonParameters.InputPixelPosToScreenPos = (FScreenTransform::Identity + 0.5f) * FScreenTransform::ChangeTextureBasisFromTo(FScreenPassTextureViewport(
			InputExtent, InputRect), FScreenTransform::ETextureBasis::TexelPosition, FScreenTransform::ETextureBasis::ScreenPosition);
		CommonParameters.ScreenVelocityToInputPixelVelocity = (FScreenTransform::Identity / CommonParameters.InputPixelPosToScreenPos).Scale;
		CommonParameters.InputPixelVelocityToScreenVelocity = CommonParameters.InputPixelPosToScreenPos.Scale.GetAbs();

		CommonParameters.HistoryInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			HistoryExtent, FIntRect(FIntPoint(0, 0), HistorySize)));

		CommonParameters.InputJitter = FVector2f(View.TemporalJitterPixels);
		CommonParameters.bCameraCut = bCameraCut;
		CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;
	}

	auto CreateDebugUAV = [&](const FIntPoint& Extent, const TCHAR* DebugName)
	{
#if COMPILE_TSR_DEBUG_PASSES
		uint16 ArraySize = uint16(FMath::Clamp(CVarTSRDebugArraySize.GetValueOnRenderThread(), 1, GMaxTextureArrayLayers));
#else
		const uint16 ArraySize = 1;
#endif

		FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2DArray(
			Extent,
			PF_FloatRGBA,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
			ArraySize);

		FRDGTextureRef DebugTexture = GraphBuilder.CreateTexture(DebugDesc, DebugName);

		return GraphBuilder.CreateUAV(DebugTexture);
	};

	// Clear atomic scattered texture.
	FRDGTextureRef PrevUseCountTexture;
	FRDGTextureRef PrevClosestDepthTexture;
	{
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PF_R32_UINT,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV | TexCreate_AtomicCompatible);

			PrevUseCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.PrevUseCountTexture"));
			PrevClosestDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.PrevClosestDepthTexture"));
		}

		FTSRClearPrevTexturesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRClearPrevTexturesCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->PrevUseCountOutput = GraphBuilder.CreateUAV(PrevUseCountTexture);
		PassParameters->PrevClosestDepthOutput = GraphBuilder.CreateUAV(PrevClosestDepthTexture);

		TShaderMapRef<FTSRClearPrevTexturesCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR ClearPrevTextures %dx%d", InputRect.Width(), InputRect.Height()),
			AsyncComputePasses >= 1 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8 * 2));
	}

	// Dilate the velocity texture & scatter reprojection into previous frame
	FRDGTextureRef DilatedVelocityTexture;
	FRDGTextureRef ClosestDepthTexture;
	FRDGTextureRef ParallaxFactorTexture;
	FVelocityFlattenTextures VelocityFlattenTextures;
	{
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PF_G16R16,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			DilatedVelocityTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.Velocity.Dilated"));

			Desc.Format = PF_R16F;
			ClosestDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.ClosestDepthTexture"));

			Desc.Format = PF_R8_UINT;
			ParallaxFactorTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.ParallaxFactor"));
		}

		int32 TileSize = 8;
		FTSRDilateVelocityCS::FPermutationDomain PermutationVector;
		FTSRDilateVelocityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRDilateVelocityCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->PrevOutputBufferUVMin = CommonParameters.InputInfo.UVViewportBilinearMin - CommonParameters.InputInfo.ExtentInverse;
		PassParameters->PrevOutputBufferUVMax = CommonParameters.InputInfo.UVViewportBilinearMax + CommonParameters.InputInfo.ExtentInverse;
		{
			float TanHalfFieldOfView = View.ViewMatrices.GetInvProjectionMatrix().M[0][0];

			// Should be multiplied 0.5* for the diameter to radius, and by 2.0 because GetTanHalfFieldOfView() cover only half of the pixels.
			float WorldDepthToPixelWorldRadius = TanHalfFieldOfView / float(View.ViewRect.Width());

			PassParameters->WorldDepthToDepthError = WorldDepthToPixelWorldRadius * 2.0f;
		}
		PassParameters->VelocityExtrapolationMultiplier = FMath::Clamp(CVarTSRVelocityExtrapolation.GetValueOnRenderThread(), 0.0f, 1.0f);

		PassParameters->SceneDepthTexture = PassInputs.SceneDepthTexture;
		PassParameters->SceneVelocityTexture = PassInputs.SceneVelocityTexture;

		PassParameters->DilatedVelocityOutput = GraphBuilder.CreateUAV(DilatedVelocityTexture);
		PassParameters->ClosestDepthOutput = GraphBuilder.CreateUAV(ClosestDepthTexture);
		PassParameters->PrevUseCountOutput = GraphBuilder.CreateUAV(PrevUseCountTexture);
		PassParameters->PrevClosestDepthOutput = GraphBuilder.CreateUAV(PrevClosestDepthTexture);
		PassParameters->ParallaxFactorOutput = GraphBuilder.CreateUAV(ParallaxFactorTexture);

		// Setup up the motion blur's velocity flatten pass.
		if (PassInputs.bGenerateVelocityFlattenTextures)
		{
			const int32 MotionBlurDirections = GetMotionBlurDirections();
			PermutationVector.Set<FTSRDilateVelocityCS::FMotionBlurDirectionsDim>(MotionBlurDirections);
			TileSize = FVelocityFlattenTextures::kTileSize;

			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
					InputExtent,
					PF_FloatR11G11B10,
					FClearValueBinding::None,
					GFastVRamConfig.VelocityFlat | TexCreate_ShaderResource | TexCreate_UAV);

				VelocityFlattenTextures.VelocityFlatten.Texture = GraphBuilder.CreateTexture(Desc, TEXT("MotionBlur.VelocityTile"));
				VelocityFlattenTextures.VelocityFlatten.ViewRect = InputRect;
			}

			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
					FIntPoint::DivideAndRoundUp(InputRect.Size(), FVelocityFlattenTextures::kTileSize),
					PF_FloatRGBA,
					FClearValueBinding::None,
					GFastVRamConfig.MotionBlur | TexCreate_ShaderResource | TexCreate_UAV);

				VelocityFlattenTextures.VelocityTile[0].Texture = GraphBuilder.CreateTexture(Desc, TEXT("MotionBlur.VelocityTile"));
				VelocityFlattenTextures.VelocityTile[0].ViewRect = FIntRect(FIntPoint::ZeroValue, Desc.Extent);

				Desc.Format = PF_G16R16F;
				VelocityFlattenTextures.VelocityTile[1].Texture = GraphBuilder.CreateTexture(Desc, TEXT("MotionBlur.VelocityTile"));
				VelocityFlattenTextures.VelocityTile[1].ViewRect = FIntRect(FIntPoint::ZeroValue, Desc.Extent);
			}

			PassParameters->VelocityFlattenParameters = GetVelocityFlattenParameters(View);
			PassParameters->VelocityFlattenOutput = GraphBuilder.CreateUAV(VelocityFlattenTextures.VelocityFlatten.Texture);
			PassParameters->VelocityTileOutput0 = GraphBuilder.CreateUAV(VelocityFlattenTextures.VelocityTile[0].Texture);
			PassParameters->VelocityTileOutput1 = GraphBuilder.CreateUAV(VelocityFlattenTextures.VelocityTile[1].Texture);
		}

		PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TSR.DilateVelocity"));

		TShaderMapRef<FTSRDilateVelocityCS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR DilateVelocity(MotionBlurDirections=%d) %dx%d",
				int32(PermutationVector.Get<FTSRDilateVelocityCS::FMotionBlurDirectionsDim>()),
				InputRect.Width(), InputRect.Height()),
			AsyncComputePasses >= 2 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(InputRect.Size(), TileSize));
	}

	// Create new history.
	FTSRHistoryTextures History;
	{
		History.ArrayIndices = TranslateHistoryFormatBitsToArrayIndices(HistoryFormatBits);

		FRDGTextureDesc ArrayDesc = FRDGTextureDesc::Create2DArray(
			HistoryExtent,
			(CVarTSRR11G11B10History.GetValueOnRenderThread() != 0 && !bSupportsAlpha) ? PF_FloatR11G11B10 : PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV,
			History.ArrayIndices.Size);

		History.ColorArray = GraphBuilder.CreateTexture(ArrayDesc, TEXT("TSR.History.ColorArray"));
	}
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			HistoryExtent,
			(CVarTSRR11G11B10History.GetValueOnRenderThread() != 0 && !bSupportsAlpha) ? PF_FloatR11G11B10 : PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		Desc.Format = PF_R8G8;
		History.Metadata = GraphBuilder.CreateTexture(Desc, TEXT("TSR.History.Metadata"));

		Desc.Format = PF_R8;
		if (bAccumulateTranslucencySeparately)
		{
			History.TranslucencyAlpha = GraphBuilder.CreateTexture(Desc, TEXT("TSR.History.TranslucencyAlpha"));
		}
	}
	{
		FRDGTextureDesc SubpixelDetailsDesc = FRDGTextureDesc::Create2D(
			InputExtent,
			PF_R16_UINT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		History.SubpixelDetails = GraphBuilder.CreateTexture(SubpixelDetailsDesc, TEXT("TSR.History.SubpixelInfo"));
	}
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			InputExtent,
			bSupportsAlpha ? PF_FloatRGBA : PF_A2B10G10R10,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		History.Guide = GraphBuilder.CreateTexture(Desc, TEXT("TSR.History.Guide"));
	}

	// Setup a dummy history
	FTSRHistorySRVs DummyHistorySRVs;
	{
		DummyHistorySRVs.HighFrequency = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(BlackDummy));
		DummyHistorySRVs.Metadata = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(BlackDummy));
		DummyHistorySRVs.Translucency = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(BlackDummy));
		DummyHistorySRVs.TranslucencyAlpha = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(WhiteDummy));
		DummyHistorySRVs.Guide = BlackDummy;

		DummyHistorySRVs.SubpixelDetails = BlackUintDummy;
	}

	// Setup the previous frame history
	FTSRHistorySRVs PrevHistorySRVs;
	if (!bCameraCut)
	{
		FTSRHistoryTextures PrevHistory;
		PrevHistory.ArrayIndices = History.ArrayIndices;

		// Register filterable history
		PrevHistory.ColorArray = GraphBuilder.RegisterExternalTexture(InputHistory.ColorArray);
		PrevHistory.Metadata = GraphBuilder.RegisterExternalTexture(InputHistory.Metadata);
		PrevHistory.TranslucencyAlpha = InputHistory.TranslucencyAlpha.IsValid() ? GraphBuilder.RegisterExternalTexture(InputHistory.TranslucencyAlpha) : DummyHistorySRVs.TranslucencyAlpha->Desc.Texture;
		PrevHistory.Guide = GraphBuilder.RegisterExternalTexture(InputHistory.Guide);

		// Register non-filterable history
		PrevHistory.SubpixelDetails = GraphBuilder.RegisterExternalTexture(InputHistory.SubpixelDetails);

		PrevHistorySRVs = CreateSRVs(GraphBuilder, PrevHistory);
	}
	else
	{
		PrevHistorySRVs = DummyHistorySRVs;
	}

	// Setup the shader parameters for previous frame history
	FTSRPrevHistoryParameters PrevHistoryParameters;
	{
		// Setup prev history parameters.
		FScreenPassTextureViewport PrevHistoryViewport(PrevHistorySRVs.HighFrequency->Desc.Texture->Desc.Extent, InputHistory.OutputViewportRect);
		FScreenPassTextureViewport PrevSubpixelDetailsViewport(PrevHistorySRVs.SubpixelDetails->Desc.Extent, InputHistory.InputViewportRect);

		if (bCameraCut)
		{
			PrevHistoryViewport.Extent = FIntPoint(1, 1);
			PrevHistoryViewport.Rect = FIntRect(FIntPoint(0, 0), FIntPoint(1, 1));
			PrevSubpixelDetailsViewport.Extent = FIntPoint(1, 1);
			PrevSubpixelDetailsViewport.Rect = FIntRect(FIntPoint(0, 0), FIntPoint(1, 1));
		}

		PrevHistoryParameters.PrevHistoryInfo = GetScreenPassTextureViewportParameters(PrevHistoryViewport);
		PrevHistoryParameters.ScreenPosToPrevHistoryBufferUV = FScreenTransform::ChangeTextureBasisFromTo(
			PrevHistoryViewport, FScreenTransform::ETextureBasis::ScreenPosition, FScreenTransform::ETextureBasis::TextureUV);
		PrevHistoryParameters.ScreenPosToPrevSubpixelDetails = FScreenTransform::ChangeTextureBasisFromTo(
			PrevSubpixelDetailsViewport, FScreenTransform::ETextureBasis::ScreenPosition, FScreenTransform::ETextureBasis::TextureUV);
		PrevHistoryParameters.PrevSubpixelDetailsExtent = PrevSubpixelDetailsViewport.Extent;
		PrevHistoryParameters.HistoryPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;
	}

	// Decimate input to flicker at same frequency as input.
	FRDGTextureRef ReprojectedHistoryGuideTexture = nullptr;
	FRDGTextureRef ParallaxRejectionMaskTexture = nullptr;
	FRDGTextureRef HoleFilledVelocityMaskTexture = nullptr;
	{
		FRDGTextureRef HoleFilledVelocityTexture;
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PF_R8,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			ParallaxRejectionMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.ParallaxRejectionMask"));

			Desc.Format = PF_G16R16;
			HoleFilledVelocityTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.Velocity.HoleFilled"));

			Desc.Format = PF_R8G8;
			HoleFilledVelocityMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.velocity.HoleFillMask"));
		}

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PrevHistorySRVs.Guide->Desc.Format,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			ReprojectedHistoryGuideTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.ReprojectedHistoryGuide"));
		}

		FTSRDecimateHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRDecimateHistoryCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		{
			const FViewMatrices& ViewMatrices = View.ViewMatrices;
			const FViewMatrices& PrevViewMatrices = View.PrevViewInfo.ViewMatrices;

			FMatrix RotationalInvViewProj = ViewMatrices.ComputeInvProjectionNoAAMatrix() * (ViewMatrices.GetTranslatedViewMatrix().RemoveTranslation().GetTransposed());
			FMatrix RotationalPrevViewProj = (PrevViewMatrices.GetTranslatedViewMatrix().RemoveTranslation()) * PrevViewMatrices.ComputeProjectionNoAAMatrix();

			PassParameters->RotationalClipToPrevClip = FMatrix44f(RotationalInvViewProj * RotationalPrevViewProj);		// LWC_TODO: Precision loss?
		}
		{
			float TanHalfFieldOfView = View.ViewMatrices.GetInvProjectionMatrix().M[0][0];

			// Should be multiplied 0.5* for the diameter to radius, and by 2.0 because GetTanHalfFieldOfView() cover only half of the pixels.
			PassParameters->WorldDepthToPixelWorldRadius = TanHalfFieldOfView / float(View.ViewRect.Width());
		}

		PassParameters->InputSceneColorTexture = PassInputs.SceneColorTexture;
		PassParameters->DilatedVelocityTexture = DilatedVelocityTexture;
		PassParameters->ClosestDepthTexture = ClosestDepthTexture;
		PassParameters->PrevUseCountTexture = PrevUseCountTexture;
		PassParameters->PrevClosestDepthTexture = PrevClosestDepthTexture;
		PassParameters->ParallaxFactorTexture = ParallaxFactorTexture;

		PassParameters->PrevHistoryParameters = PrevHistoryParameters;
		PassParameters->PrevHistorySubpixelDetails = PrevHistorySRVs.SubpixelDetails;

		{
			FScreenPassTextureViewport PrevHistoryColorViewport(PrevHistorySRVs.Guide->Desc.Extent, InputHistory.InputViewportRect);
			PassParameters->PrevHistoryGuide = PrevHistorySRVs.Guide;
			PassParameters->PrevGuideInfo = GetScreenPassTextureViewportParameters(PrevHistoryColorViewport);
			PassParameters->InputPixelPosToReprojectScreenPos = ((FScreenTransform::Identity - InputRect.Min + 0.5f) / InputRect.Size()) * FScreenTransform::ViewportUVToScreenPos;
			PassParameters->ScreenPosToPrevHistoryGuideBufferUV = FScreenTransform::ChangeTextureBasisFromTo(
				PrevHistoryColorViewport,
				FScreenTransform::ETextureBasis::ScreenPosition,
				FScreenTransform::ETextureBasis::TextureUV);
			PassParameters->HistoryGuideQuantizationError = ComputePixelFormatQuantizationError(PrevHistorySRVs.Guide->Desc.Format);
		}

		PassParameters->ReprojectedHistoryGuideOutput = GraphBuilder.CreateUAV(ReprojectedHistoryGuideTexture);
		PassParameters->HoleFilledVelocityOutput = GraphBuilder.CreateUAV(HoleFilledVelocityTexture);
		PassParameters->HoleFilledVelocityMaskOutput = GraphBuilder.CreateUAV(HoleFilledVelocityMaskTexture);
		PassParameters->ParallaxRejectionMaskOutput = GraphBuilder.CreateUAV(ParallaxRejectionMaskTexture);
		PassParameters->HistorySubpixelDetailsOutput = GraphBuilder.CreateUAV(History.SubpixelDetails);
		PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TSR.DecimateHistory"));

		TShaderMapRef<FTSRDecimateHistoryCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR DecimateHistory %dx%d", InputRect.Width(), InputRect.Height()),
			AsyncComputePasses >= 2 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));

		DilatedVelocityTexture = HoleFilledVelocityTexture;
	}

	FRDGTextureRef TranslucencyRejectionTexture = nullptr;
	if (bRejectSeparateTranslucency && View.PrevViewInfo.SeparateTranslucency != nullptr)
	{
		FRDGTextureRef PrevTranslucencyTexture;
		FScreenPassTextureViewport PrevTranslucencyViewport;

		if (View.PrevViewInfo.SeparateTranslucency)
		{

			PrevTranslucencyTexture = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.SeparateTranslucency);
			PrevTranslucencyViewport = FScreenPassTextureViewport(PrevTranslucencyTexture->Desc.Extent, View.PrevViewInfo.SeparateTranslucencyViewRect);

		}
		else
		{
			PrevTranslucencyTexture = BlackAlphaOneDummy;
			PrevTranslucencyViewport = FScreenPassTextureViewport(FIntPoint(1, 1), FIntRect(FIntPoint(0, 0), FIntPoint(1, 1)));
		}

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PF_R8,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			TranslucencyRejectionTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.TranslucencyRejection"));
		}

		FTSRCompareTranslucencyCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRCompareTranslucencyCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;

		PassParameters->TranslucencyInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			SeparateTranslucencyTexture->Desc.Extent, SeparateTranslucencyRect));
		PassParameters->PrevTranslucencyInfo = GetScreenPassTextureViewportParameters(PrevTranslucencyViewport);
		PassParameters->PrevTranslucencyPreExposureCorrection = PrevHistoryParameters.HistoryPreExposureCorrection;
		PassParameters->TranslucencyHighlightLuminance = CVarTSRTranslucencyHighlightLuminance.GetValueOnRenderThread();

		PassParameters->ScreenPosToPrevTranslucencyTextureUV = FScreenTransform::ChangeTextureBasisFromTo(
			PrevTranslucencyViewport, FScreenTransform::ETextureBasis::ScreenPosition, FScreenTransform::ETextureBasis::TextureUV);

		PassParameters->DilatedVelocityTexture = DilatedVelocityTexture;
		PassParameters->TranslucencyTexture = SeparateTranslucencyTexture;
		PassParameters->PrevTranslucencyTexture = PrevTranslucencyTexture;

		PassParameters->TranslucencyRejectionOutput = GraphBuilder.CreateUAV(TranslucencyRejectionTexture);
		PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TSR.CompareTranslucency"));

		TShaderMapRef<FTSRCompareTranslucencyCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR CompareTranslucency %dx%d", InputRect.Width(), InputRect.Height()),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));
	}

	// Reject the history with frequency decomposition.
	FRDGTextureRef HistoryRejectionTexture;
	FRDGTextureRef InputSceneColorLdrLumaTexture = nullptr;
	{
		bool bOutputHalfRes = PostFilter != ERejectionPostFilter::PreRejectionDownsample;
		check(!bOutputHalfRes); // TODO(TSR)

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PF_R8,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			InputSceneColorLdrLumaTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.SceneColorLdrLuma"));
		}

		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				bOutputHalfRes ? RejectionExtent : InputExtent,
				PF_R8,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			HistoryRejectionTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.HistoryRejection"));
		}

		FScreenPassTextureViewport TranslucencyViewport(
			SeparateTranslucencyTexture->Desc.Extent, SeparateTranslucencyRect);

		FTSRRejectShadingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRRejectShadingCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->InputPixelPosToTranslucencyTextureUV =
			((FScreenTransform::Identity + 0.5f - InputRect.Min) / InputRect.Size()) *
			FScreenTransform::ChangeTextureBasisFromTo(TranslucencyViewport, FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV);
		PassParameters->TranslucencyTextureUVMin = GetScreenPassTextureViewportParameters(TranslucencyViewport).UVViewportBilinearMin;
		PassParameters->TranslucencyTextureUVMax = GetScreenPassTextureViewportParameters(TranslucencyViewport).UVViewportBilinearMax;
		PassParameters->HistoryGuideQuantizationError = ComputePixelFormatQuantizationError(History.Guide->Desc.Format);

		PassParameters->InputTexture = PassInputs.SceneColorTexture;
		PassParameters->InputSceneTranslucencyTexture = SeparateTranslucencyTexture;
		PassParameters->ReprojectedHistoryGuideTexture = ReprojectedHistoryGuideTexture;
		PassParameters->ParallaxRejectionMaskTexture = ParallaxRejectionMaskTexture;

		PassParameters->HistoryGuideOutput = GraphBuilder.CreateUAV(History.Guide);
		PassParameters->HistoryRejectionOutput = GraphBuilder.CreateUAV(HistoryRejectionTexture);
		PassParameters->InputSceneColorLdrLumaOutput = GraphBuilder.CreateUAV(InputSceneColorLdrLumaTexture);
		PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TSR.RejectShading"));

		FTSRRejectShadingCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTSRRejectShadingCS::FUseWaveOps>(bUseWaveOps&& GRHIMinimumWaveSize <= 32 && GRHIMaximumWaveSize >= 32);
		PermutationVector.Set<FTSRRejectShadingCS::FSeparateTranslucencyDim>(bAccumulateTranslucencySeparately);

		TShaderMapRef<FTSRRejectShadingCS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR RejectShading(%s%s) %dx%d",
				PermutationVector.Get<FTSRRejectShadingCS::FUseWaveOps>() ? TEXT("WaveOps") : TEXT(""),
				PermutationVector.Get<FTSRRejectShadingCS::FSeparateTranslucencyDim>() ? TEXT(" SeparateTranslucency") : TEXT(""),
				InputRect.Width(), InputRect.Height()),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(InputRect.Size(), 12));
	}

	// Spatial anti-aliasing when doing history rejection.
	FRDGTextureRef AntiAliasingTexture = nullptr;
	FRDGTextureRef NoiseFilteringTexture = nullptr;
	if (RejectionAntiAliasingQuality > 0)
	{
		FRDGTextureRef RawAntiAliasingTexture;
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				InputExtent,
				PF_R8_UINT,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			RawAntiAliasingTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.AntiAliasing.Raw"));
			AntiAliasingTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.AntiAliasing.Filtered"));

			Desc.Format = PF_R8;
			NoiseFilteringTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.AntiAliasing.Noise"));
		}

		{
			FTSRSpatialAntiAliasingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRSpatialAntiAliasingCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->InputSceneColorTexture = PassInputs.SceneColorTexture;
			PassParameters->InputSceneColorLdrLumaTexture = InputSceneColorLdrLumaTexture;
			PassParameters->AntiAliasingOutput = GraphBuilder.CreateUAV(RawAntiAliasingTexture);
			PassParameters->NoiseFilteringOutput = GraphBuilder.CreateUAV(NoiseFilteringTexture);
			PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TSR.SpatialAntiAliasing"));

			FTSRSpatialAntiAliasingCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FTSRSpatialAntiAliasingCS::FQualityDim>(RejectionAntiAliasingQuality);

			TShaderMapRef<FTSRSpatialAntiAliasingCS> ComputeShader(View.ShaderMap, PermutationVector);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TSR SpatialAntiAliasing(Quality=%d) %dx%d",
					RejectionAntiAliasingQuality,
					InputRect.Width(), InputRect.Height()),
				AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));
		}

		{
			FTSRFilterAntiAliasingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRFilterAntiAliasingCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->AntiAliasingTexture = RawAntiAliasingTexture;
			PassParameters->AntiAliasingOutput = GraphBuilder.CreateUAV(AntiAliasingTexture);
			PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TSR.FilterAntiAliasing"));

			TShaderMapRef<FTSRFilterAntiAliasingCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TSR FilterAntiAliasing %dx%d", InputRect.Width(), InputRect.Height()),
				AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));
		}
	}

	// Post filter the rejection.
	if (PostFilter != ERejectionPostFilter::Disabled)
	{
		bool bOutputHalfRes = PostFilter == ERejectionPostFilter::PreRejectionDownsample;
		FIntRect Rect = bOutputHalfRes ? InputRect : RejectionRect;

		FRDGTextureRef FilteredHistoryRejectionTexture;
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				RejectionExtent,
				PF_R8,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);

			FilteredHistoryRejectionTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.HistoryRejection"));
		}

		FTSRPostfilterRejectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRPostfilterRejectionCS::FParameters>();
		PassParameters->HistoryRejectionViewport = Rect;
		PassParameters->HistoryRejectionTexture = HistoryRejectionTexture;
		PassParameters->HistoryRejectionOutput = GraphBuilder.CreateUAV(FilteredHistoryRejectionTexture);
		PassParameters->DebugOutput = CreateDebugUAV(bOutputHalfRes ? InputExtent : RejectionExtent, TEXT("Debug.TSR.PostfilterRejection"));

		FTSRPostfilterRejectionCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTSRPostfilterRejectionCS::FOutputHalfRes>(PostFilter == ERejectionPostFilter::PreRejectionDownsample);

		TShaderMapRef<FTSRPostfilterRejectionCS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR PostfilterRejection %dx%d", Rect.Width(), Rect.Height()),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(Rect.Size(), 8));

		HistoryRejectionTexture = FilteredHistoryRejectionTexture;
	}

	// Dilate the rejection.
	FRDGTextureRef DilatedHistoryRejectionTexture;
	{
		DilatedHistoryRejectionTexture = GraphBuilder.CreateTexture(HistoryRejectionTexture->Desc, TEXT("TSR.DilatedHistoryRejection"));

		FTSRDilateRejectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRDilateRejectionCS::FParameters>();
		PassParameters->HistoryRejectionViewport = RejectionRect;
		PassParameters->HistoryRejectionTexture = HistoryRejectionTexture;
		PassParameters->DilatedHistoryRejectionOutput = GraphBuilder.CreateUAV(DilatedHistoryRejectionTexture);
		PassParameters->DebugOutput = CreateDebugUAV(RejectionExtent, TEXT("Debug.TSR.DilateRejection"));

		TShaderMapRef<FTSRDilateRejectionCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR DilateRejection %dx%d", RejectionRect.Width(), RejectionRect.Height()),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(RejectionRect.Size(), 8));
	}

	// Allocate output
	FRDGTextureRef SceneColorOutputTexture;
	FRDGTextureRef SceneColorOutputHalfResTexture = nullptr;
	{
		check(!(PassInputs.bGenerateOutputMip1 && PassInputs.bAllowDownsampleSceneColor));
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			OutputExtent,
			ColorFormat,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
			/* NumMips = */ PassInputs.bGenerateOutputMip1 ? 2 : 1);

		SceneColorOutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.Output"));

		if (PassInputs.bAllowDownsampleSceneColor)
		{
			Desc.Extent /= 2;
			SceneColorOutputHalfResTexture = GraphBuilder.CreateTexture(Desc, TEXT("TSR.HalfResOutput"));
		}
	}

	// Update temporal history.
	{
		static const TCHAR* const kUpdateQualityNames[] = {
			TEXT("Low"),
			TEXT("Medium"),
			TEXT("High"),
			TEXT("Epic"),
		};
		static_assert(UE_ARRAY_COUNT(kUpdateQualityNames) == int32(FTSRUpdateHistoryCS::EQuality::MAX), "Fix me!");

		FTSRUpdateHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRUpdateHistoryCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->InputSceneColorTexture = PassInputs.SceneColorTexture;
		PassParameters->InputSceneStencilTexture = GraphBuilder.CreateSRV(
			FRDGTextureSRVDesc::CreateWithPixelFormat(PassInputs.SceneDepthTexture, PF_X24_G8));
		PassParameters->InputSceneTranslucencyTexture = SeparateTranslucencyTexture;
		PassParameters->HistoryRejectionTexture = DilatedHistoryRejectionTexture;
		PassParameters->TranslucencyRejectionTexture = TranslucencyRejectionTexture ? TranslucencyRejectionTexture : BlackDummy;

		PassParameters->DilatedVelocityTexture = DilatedVelocityTexture;
		PassParameters->ParallaxFactorTexture = ParallaxFactorTexture;
		PassParameters->ParallaxRejectionMaskTexture = ParallaxRejectionMaskTexture;
		PassParameters->AntiAliasingTexture = AntiAliasingTexture;
		PassParameters->NoiseFilteringTexture = NoiseFilteringTexture;
		PassParameters->HoleFilledVelocityMaskTexture = HoleFilledVelocityMaskTexture;

		PassParameters->TranslucencyInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			PassParameters->TranslucencyRejectionTexture->Desc.Extent, SeparateTranslucencyRect));
		PassParameters->TranslucencyPixelPosMin = PassParameters->TranslucencyInfo.ViewportMin;
		PassParameters->TranslucencyPixelPosMax = PassParameters->TranslucencyInfo.ViewportMax - 1;

		FScreenTransform HistoryPixelPosToViewportUV = (FScreenTransform::Identity + 0.5f) * CommonParameters.HistoryInfo.ViewportSizeInverse;
		PassParameters->HistoryPixelPosToScreenPos = HistoryPixelPosToViewportUV * FScreenTransform::ViewportUVToScreenPos;
		PassParameters->HistoryPixelPosToInputPPCo = HistoryPixelPosToViewportUV * CommonParameters.InputInfo.ViewportSize + CommonParameters.InputJitter + CommonParameters.InputPixelPosMin;
		PassParameters->HistoryPixelPosToTranslucencyPPCo = HistoryPixelPosToViewportUV * PassParameters->TranslucencyInfo.ViewportSize + CommonParameters.InputJitter * PassParameters->TranslucencyInfo.ViewportSize / CommonParameters.InputInfo.ViewportSize + SeparateTranslucencyRect.Min;
		PassParameters->HistoryQuantizationError = ComputePixelFormatQuantizationError(History.ColorArray->Desc.Format);
		PassParameters->MinTranslucencyRejection = TranslucencyRejectionTexture == nullptr ? 1.0 : 0.0;
		PassParameters->InvWeightClampingPixelSpeed = 1.0f / CVarTSRWeightClampingPixelSpeed.GetValueOnRenderThread();
		PassParameters->InputToHistoryFactor = float(HistorySize.X) / float(InputRect.Width());
		PassParameters->ResponsiveStencilMask = CVarTSREnableResponiveAA.GetValueOnRenderThread() ? (STENCIL_TEMPORAL_RESPONSIVE_AA_MASK) : 0;
		PassParameters->bGenerateOutputMip1 = ((PassInputs.bGenerateOutputMip1 || PassInputs.bAllowDownsampleSceneColor) && HistorySize == OutputRect.Size()) ? 1 : 0;
		PassParameters->bHasSeparateTranslucency = bHasSeparateTranslucency;

		PassParameters->PrevHistoryParameters = PrevHistoryParameters;
		PassParameters->PrevHistory = PrevHistorySRVs;

		PassParameters->HistoryOutput = CreateUAVs(GraphBuilder, History);
		if (HistorySize != OutputRect.Size())
		{
			PassParameters->SceneColorOutputMip0 = CreateDummyUAV(GraphBuilder, PF_FloatR11G11B10);
		}
		else
		{
			PassParameters->SceneColorOutputMip0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColorOutputTexture, /* InMipLevel = */ 0));
		}
		
		if (!PassParameters->bGenerateOutputMip1)
		{
			PassParameters->SceneColorOutputMip1 = CreateDummyUAV(GraphBuilder, PF_FloatR11G11B10);
		}
		else if (PassInputs.bGenerateOutputMip1)
		{
			PassParameters->SceneColorOutputMip1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColorOutputTexture, /* InMipLevel = */ 1));
		}
		else if (PassInputs.bAllowDownsampleSceneColor)
		{
			PassParameters->SceneColorOutputMip1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColorOutputHalfResTexture));
		}
		else
		{
			unimplemented();
		}
		PassParameters->DebugOutput = CreateDebugUAV(HistoryExtent, TEXT("Debug.TSR.UpdateHistory"));

		FTSRUpdateHistoryCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTSRUpdateHistoryCS::FQualityDim>(UpdateHistoryQuality);
		PermutationVector.Set<FTSRUpdateHistoryCS::FSeparateTranslucencyDim>(bAccumulateTranslucencySeparately);

		TShaderMapRef<FTSRUpdateHistoryCS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR UpdateHistory(Quality=%s%s%s%s) %dx%d",
				kUpdateQualityNames[int32(PermutationVector.Get<FTSRUpdateHistoryCS::FQualityDim>())],
				PermutationVector.Get<FTSRUpdateHistoryCS::FSeparateTranslucencyDim>() ? TEXT(" SeparateTranslucency") : TEXT(""),
				History.ColorArray->Desc.Format == PF_FloatR11G11B10 ? TEXT(" R11G11B10") : TEXT(""),
				PassParameters->bGenerateOutputMip1 ? TEXT(" OutputMip1") : TEXT(""),
				HistorySize.X, HistorySize.Y),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(HistorySize, 8));
	}

	// Debug the history.
	#if COMPILE_TSR_DEBUG_PASSES
	if (bSetupDebugPasses)
	{
		const int32 kHistoryUpscalingFactor = 2;

		FTSRDebugHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRDebugHistoryCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->History = CreateSRVs(GraphBuilder, History);
		PassParameters->PrevHistory = PrevHistorySRVs;
		PassParameters->DebugOutput = CreateDebugUAV(HistoryExtent * kHistoryUpscalingFactor, TEXT("Debug.TSR.History"));

		TShaderMapRef<FTSRDebugHistoryCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR DebugHistory %dx%d", HistorySize.X, HistorySize.Y),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(HistorySize * kHistoryUpscalingFactor, 8));
	}
	#endif

	// If we upscaled the history buffer, downsize back to the secondary screen percentage size.
	if (HistorySize != OutputRect.Size())
	{
		FTSRResolveHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTSRResolveHistoryCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->DispatchThreadToHistoryPixelPos = (
			FScreenTransform::DispatchThreadIdToViewportUV(OutputRect) *
			FScreenTransform::ChangeTextureBasisFromTo(
				HistoryExtent, FIntRect(FIntPoint(0, 0), HistorySize),
				FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TexelPosition));
		PassParameters->OutputViewRectMin = OutputRect.Min;
		PassParameters->OutputViewRectMax = OutputRect.Max;
		PassParameters->bGenerateOutputMip1 = (PassInputs.bGenerateOutputMip1 || PassInputs.bAllowDownsampleSceneColor) ? 1 : 0;
		PassParameters->HistoryValidityMultiply = float(HistorySize.X * HistorySize.Y) / float(OutputRect.Width() * OutputRect.Height());

		PassParameters->History = CreateSRVs(GraphBuilder, History);
		
		PassParameters->SceneColorOutputMip0 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColorOutputTexture, /* InMipLevel = */ 0));
		if (PassInputs.bGenerateOutputMip1)
		{
			PassParameters->SceneColorOutputMip1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColorOutputTexture, /* InMipLevel = */ 1));
		}
		else if (PassInputs.bAllowDownsampleSceneColor)
		{
			PassParameters->SceneColorOutputMip1 = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColorOutputHalfResTexture));
		}
		else
		{
			PassParameters->SceneColorOutputMip1 = CreateDummyUAV(GraphBuilder, PF_FloatR11G11B10);
		}
		PassParameters->DebugOutput = CreateDebugUAV(HistoryExtent, TEXT("Debug.TSR.ResolveHistory"));

		FTSRResolveHistoryCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTSRUpdateHistoryCS::FSeparateTranslucencyDim>(bAccumulateTranslucencySeparately);

		TShaderMapRef<FTSRResolveHistoryCS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TSR ResolveHistory %dx%d", OutputRect.Width(), OutputRect.Height()),
			AsyncComputePasses >= 3 ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(OutputRect.Size(), 8));
	}

	// Extract all resources for next frame.
	if (!View.bStatePrevViewInfoIsReadOnly)
	{
		FTSRHistory& OutputHistory = View.ViewState->PrevFrameViewInfo.TSRHistory;
		OutputHistory.InputViewportRect = InputRect;
		OutputHistory.OutputViewportRect = FIntRect(FIntPoint(0, 0), HistorySize);
		OutputHistory.FormatBit = uint32(HistoryFormatBits);

		// Extract filterable history
		GraphBuilder.QueueTextureExtraction(History.ColorArray, &OutputHistory.ColorArray);
		GraphBuilder.QueueTextureExtraction(History.Metadata, &OutputHistory.Metadata);
		if (bAccumulateTranslucencySeparately)
		{
			GraphBuilder.QueueTextureExtraction(History.TranslucencyAlpha, &OutputHistory.TranslucencyAlpha);
		}

		// Extract non-filterable history
		GraphBuilder.QueueTextureExtraction(History.SubpixelDetails, &OutputHistory.SubpixelDetails);

		// Extract history guide
		GraphBuilder.QueueTextureExtraction(History.Guide, &OutputHistory.Guide);

		// Extract the translucency buffer to compare it with next frame
		if (bRejectSeparateTranslucency)
		{
			GraphBuilder.QueueTextureExtraction(
				SeparateTranslucencyTexture, &View.ViewState->PrevFrameViewInfo.SeparateTranslucency);
			View.ViewState->PrevFrameViewInfo.SeparateTranslucencyViewRect = InputRect;
		}

		// Extract the output for next frame SSR so that separate translucency shows up in SSR.
		{
			GraphBuilder.QueueTextureExtraction(
				SceneColorOutputTexture, &View.ViewState->PrevFrameViewInfo.CustomSSRInput.RT[0]);

			View.ViewState->PrevFrameViewInfo.CustomSSRInput.ViewportRect = OutputRect;
			View.ViewState->PrevFrameViewInfo.CustomSSRInput.ReferenceBufferSize = OutputExtent;
		}
	}


	ITemporalUpscaler::FOutputs Outputs;
	Outputs.FullRes.Texture = SceneColorOutputTexture;
	Outputs.FullRes.ViewRect = OutputRect;
	if (PassInputs.bAllowDownsampleSceneColor)
	{
		Outputs.HalfRes.Texture = SceneColorOutputHalfResTexture;
		Outputs.HalfRes.ViewRect.Min = OutputRect.Min / 2;
		Outputs.HalfRes.ViewRect.Max = Outputs.HalfRes.ViewRect.Min + FIntPoint::DivideAndRoundUp(OutputRect.Size(), 2);
	}
	Outputs.VelocityFlattenTextures = VelocityFlattenTextures;
	return Outputs;
} // AddTemporalSuperResolutionPasses()
