// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScreenSpaceRayTracing.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"
#include "SceneTextureParameters.h"


static TAutoConsoleVariable<int32> CVarSSRQuality(
	TEXT("r.SSR.Quality"),
	3,
	TEXT("Whether to use screen space reflections and at what quality setting.\n")
	TEXT("(limits the setting in the post process settings which has a different scale)\n")
	TEXT("(costs performance, adds more visual realism but the technique has limits)\n")
	TEXT(" 0: off (default)\n")
	TEXT(" 1: low (no glossy)\n")
	TEXT(" 2: medium (no glossy)\n")
	TEXT(" 3: high (glossy/using roughness, few samples)\n")
	TEXT(" 4: very high (likely too slow for real-time)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

int32 GSSRHalfResSceneColor = 0;
FAutoConsoleVariableRef CVarSSRHalfResSceneColor(
	TEXT("r.SSR.HalfResSceneColor"),
	GSSRHalfResSceneColor,
	TEXT("Use half res scene color as input for SSR. Improves performance without much of a visual quality loss."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSSRTemporal(
	TEXT("r.SSR.Temporal"),
	0,
	TEXT("Defines if we use the temporal smoothing for the screen space reflection\n")
	TEXT(" 0 is off (for debugging), 1 is on (default)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSRStencil(
	TEXT("r.SSR.Stencil"),
	0,
	TEXT("Defines if we use the stencil prepass for the screen space reflection\n")
	TEXT(" 0 is off (default), 1 is on"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSGIEnable(
	TEXT("r.SSGI.Enable"), 0,
	TEXT("Whether to enable SSGI (defaults to 0).\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSGILeakFreeReprojection(
	TEXT("r.SSGI.LeakFreeReprojection"), 1,
	TEXT("Whether use a more expensive but leak free reprojection of previous frame's scene color.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSGIHalfResolution(
	TEXT("r.SSGI.HalfRes"), 0,
	TEXT("Whether to do SSGI at half resolution (defaults to 0).\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSSGIQuality(
	TEXT("r.SSGI.Quality"), 4,
	TEXT("Quality setting to control number of ray shot with SSGI, between 1 and 4 (defaults to 4).\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);


DECLARE_GPU_STAT_NAMED(ScreenSpaceReflections, TEXT("ScreenSpace Reflections"));
DECLARE_GPU_STAT_NAMED(ScreenSpaceDiffuseIndirect, TEXT("Screen Space Diffuse Indirect"));

bool IsSSGIHalfRes()
{
	return CVarSSGIHalfResolution.GetValueOnRenderThread() > 0;
}

static bool SupportScreenSpaceDiffuseIndirect(const FViewInfo& View)
{
	if (CVarSSGIEnable.GetValueOnRenderThread() <= 0)
	{
		return false;
	}

	int Quality = CVarSSGIQuality.GetValueOnRenderThread();

	if (Quality <= 0)
	{
		return false;
	}

	if (IsAnyForwardShadingEnabled(View.GetShaderPlatform()))
	{
		return false;
	}

	return View.ViewState != nullptr;
}

bool ShouldKeepBleedFreeSceneColor(const FViewInfo& View)
{
	// TODO(Guillaume): SSR as well.
	return SupportScreenSpaceDiffuseIndirect(View) && !View.bStatePrevViewInfoIsReadOnly && CVarSSGILeakFreeReprojection.GetValueOnRenderThread() != 0;
}

bool ShouldRenderScreenSpaceReflections(const FViewInfo& View)
{
	if(!View.Family->EngineShowFlags.ScreenSpaceReflections)
	{
		return false;
	}

	if(!View.State)
	{
		// not view state (e.g. thumbnail rendering?), no HZB (no screen space reflections or occlusion culling)
		return false;
	}

	int SSRQuality = CVarSSRQuality.GetValueOnRenderThread();

	if(SSRQuality <= 0)
	{
		return false;
	}

	if(View.FinalPostProcessSettings.ScreenSpaceReflectionIntensity < 1.0f)
	{
		return false;
	}

	if (IsAnyForwardShadingEnabled(View.GetShaderPlatform()))
	{
		return false;
	}

	return true;
}

bool ShouldRenderScreenSpaceDiffuseIndirect(const FViewInfo& View)
{
	if (!SupportScreenSpaceDiffuseIndirect(View))
	{
		return false;
	}

	return View.PrevViewInfo.ScreenSpaceRayTracingInput.IsValid() || View.PrevViewInfo.TemporalAAHistory.IsValid();
}

bool IsSSRTemporalPassRequired(const FViewInfo& View)
{
	check(ShouldRenderScreenSpaceReflections(View));

	if (!View.State)
	{
		return false;
	}
	return View.AntiAliasingMethod != AAM_TemporalAA || CVarSSRTemporal.GetValueOnRenderThread() != 0;
}

bool UseSingleLayerWaterIndirectDraw(EShaderPlatform ShaderPlatform);

namespace
{

float ComputeRoughnessMaskScale(const FViewInfo& View, ESSRQuality SSRQuality)
{
	float MaxRoughness = FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionMaxRoughness, 0.01f, 1.0f);

	// f(x) = x * Scale + Bias
	// f(MaxRoughness) = 0
	// f(MaxRoughness/2) = 1

	float RoughnessMaskScale = -2.0f / MaxRoughness;
	return RoughnessMaskScale * (int32(SSRQuality) < 3 ? 2.0f : 1.0f);
}

FLinearColor ComputeSSRParams(const FViewInfo& View, ESSRQuality SSRQuality, bool bEnableDiscard)
{
	float RoughnessMaskScale = ComputeRoughnessMaskScale(View, SSRQuality);

	float FrameRandom = 0;

	if(View.ViewState)
	{
		bool bTemporalAAIsOn = View.AntiAliasingMethod == AAM_TemporalAA;

		if(bTemporalAAIsOn)
		{
			// usually this number is in the 0..7 range but it depends on the TemporalAA quality
			FrameRandom = View.ViewState->GetCurrentTemporalAASampleIndex() * 1551;
		}
		else
		{
			// 8 aligns with the temporal smoothing, larger number will do more flickering (power of two for best performance)
			FrameRandom = View.ViewState->GetFrameIndex(8) * 1551;
		}
	}

	return FLinearColor(
		FMath::Clamp(View.FinalPostProcessSettings.ScreenSpaceReflectionIntensity * 0.01f, 0.0f, 1.0f), 
		RoughnessMaskScale,
		(float)bEnableDiscard,	// TODO 
		FrameRandom);
}



BEGIN_SHADER_PARAMETER_STRUCT(FSSRTTileClassificationParameters, )
	SHADER_PARAMETER(FIntPoint, TileBufferExtent)
	SHADER_PARAMETER(int32, ViewTileCount)
	SHADER_PARAMETER(int32, MaxTileCount)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSSRTTileClassificationResources, )
	SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer<float>, TileClassificationBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSSRTTileClassificationSRVs, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, TileClassificationBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSSRTTileClassificationUAVs, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, TileClassificationBufferOutput)
END_SHADER_PARAMETER_STRUCT()

FSSRTTileClassificationResources CreateTileClassificationResources(FRDGBuilder& GraphBuilder, const FViewInfo& View, FIntPoint MaxRenderTargetSize, FSSRTTileClassificationParameters* OutParameters)
{
	FIntPoint MaxTileBufferExtent = FIntPoint::DivideAndRoundUp(MaxRenderTargetSize, 8);
	int32 MaxTileCount = MaxTileBufferExtent.X * MaxTileBufferExtent.Y;

	OutParameters->TileBufferExtent = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), 8);
	OutParameters->ViewTileCount = OutParameters->TileBufferExtent.X * OutParameters->TileBufferExtent.Y;

	FSSRTTileClassificationResources Resources;
	Resources.TileClassificationBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), MaxTileCount * 8), TEXT("SSRTTileClassification"));
	return Resources;
}

FSSRTTileClassificationSRVs CreateSRVs(FRDGBuilder& GraphBuilder, const FSSRTTileClassificationResources& ClassificationResources)
{
	FSSRTTileClassificationSRVs SRVs;
	SRVs.TileClassificationBuffer = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ClassificationResources.TileClassificationBuffer, PF_R32_FLOAT));
	return SRVs;
}

FSSRTTileClassificationUAVs CreateUAVs(FRDGBuilder& GraphBuilder, const FSSRTTileClassificationResources& ClassificationResources)
{
	FSSRTTileClassificationUAVs UAVs;
	UAVs.TileClassificationBufferOutput = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ClassificationResources.TileClassificationBuffer, PF_R32_FLOAT));
	return UAVs;
}



BEGIN_SHADER_PARAMETER_STRUCT(FSSRCommonParameters, )
	SHADER_PARAMETER(FLinearColor, SSRParams)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSSRPassCommonParameters, )
	SHADER_PARAMETER(FVector4, HZBUvFactorAndInvFactor)
	SHADER_PARAMETER(FVector4, PrevScreenPositionScaleBias)
	SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZB)
	SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float4>, ScreenSpaceRayTracingDebugOutput)
END_SHADER_PARAMETER_STRUCT()

class FSSRQualityDim : SHADER_PERMUTATION_ENUM_CLASS("SSR_QUALITY", ESSRQuality);
class FSSROutputForDenoiser : SHADER_PERMUTATION_BOOL("SSR_OUTPUT_FOR_DENOISER");


class FSSRTPrevFrameReductionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSSRTPrevFrameReductionCS);
	SHADER_USE_PARAMETER_STRUCT(FSSRTPrevFrameReductionCS, FGlobalShader);

	class FLowerMips : SHADER_PERMUTATION_BOOL("DIM_LOWER_MIPS");
	class FLeakFree : SHADER_PERMUTATION_BOOL("DIM_LEAK_FREE");

	using FPermutationDomain = TShaderPermutationDomain<FLowerMips, FLeakFree>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, PrevScreenPositionScaleBias)
		SHADER_PARAMETER(FVector2D, ViewportUVToHZBBufferUV)
		SHADER_PARAMETER(FVector2D, ReducedSceneColorSize)
		SHADER_PARAMETER(FVector2D, ReducedSceneColorTexelSize)
		SHADER_PARAMETER(FVector2D, HigherMipBufferBilinearMax)
		SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
		SHADER_PARAMETER(float, MinimumLuminance)
		SHADER_PARAMETER(float, HigherMipDownScaleFactor)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevSceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, PrevSceneColorSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevSceneDepth)
		SHADER_PARAMETER_SAMPLER(SamplerState, PrevSceneDepthSampler)

		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, HigherMipTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, HigherAlphaMipTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, HigherMipTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, HigherAlphaMipTextureSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, FurthestHZBTextureSampler)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float4>, ReducedSceneColorOutput, [3])
		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float>, ReducedSceneAlphaOutput, [3])
	END_SHADER_PARAMETER_STRUCT()
};

class FSSRTDiffuseTileClassificationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSSRTDiffuseTileClassificationCS);
	SHADER_USE_PARAMETER_STRUCT(FSSRTDiffuseTileClassificationCS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return false; // Parameters.Platform == SP_PCD3D_SM5;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2D, SamplePixelToHZBUV)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestHZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ClosestHZBTextureSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, FurthestHZBTextureSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorTextureSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRTTileClassificationParameters, TileClassificationParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRTTileClassificationUAVs, TileClassificationUAVs)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
};

class FScreenSpaceReflectionsStencilPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsStencilPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSSROutputForDenoiser>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine( TEXT("SSR_QUALITY"), uint32(0) );
	}
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRCommonParameters, CommonParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

class FScreenSpaceReflectionsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSSRQualityDim, FSSROutputForDenoiser>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUPPORTS_ANISOTROPIC_MATERIALS"), FDataDrivenShaderPlatformInfo::GetSupportsAnisotropicMaterials(Parameters.Platform));
	}
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRCommonParameters, CommonParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRPassCommonParameters, SSRPassCommonParameter)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDrawParameter)			// FScreenSpaceReflectionsTileVS
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileListData)		// FScreenSpaceReflectionsTileVS
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// This is duplicated from FWaterTileVS because vertex shader should share Parameters structure for everything to be registered correctly in a RDG pass.
class FScreenSpaceReflectionsTileVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceReflectionsTileVS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceReflectionsTileVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;
		
	using FParameters = FScreenSpaceReflectionsPS::FParameters; // Sharing parameters for proper registration with RDG
		
	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}
		
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ::UseSingleLayerWaterIndirectDraw(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("TILE_VERTEX_SHADER"), 1.0f);
		OutEnvironment.SetDefine(TEXT("WORK_TILE_SIZE"), 8);
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

class FScreenSpaceDiffuseIndirectCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceDiffuseIndirectCS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceDiffuseIndirectCS, FGlobalShader)

	class FQualityDim : SHADER_PERMUTATION_RANGE_INT("QUALITY", 1, 4);
	using FPermutationDomain = TShaderPermutationDomain< FQualityDim >;
	
	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER(FVector4, HZBUvFactorAndInvFactor)
		SHADER_PARAMETER(FVector4, ColorBufferScaleBias)
		SHADER_PARAMETER(FVector2D, ReducedColorUVMax)
		SHADER_PARAMETER(FVector2D, FullResPixelOffset)

		SHADER_PARAMETER(float, PixelPositionToFullResPixel)
		
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZBTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, FurthestHZBTextureSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorTextureSampler)
		
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRTTileClassificationParameters, ClassificationParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSSRTTileClassificationSRVs, ClassificationSRVs)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float4>, IndirectDiffuseOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float>,  AmbientOcclusionOutput)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float4>, DebugOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture<float4>, ScreenSpaceRayTracingDebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};


IMPLEMENT_GLOBAL_SHADER(FSSRTPrevFrameReductionCS, "/Engine/Private/SSRT/SSRTPrevFrameReduction.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSSRTDiffuseTileClassificationCS, "/Engine/Private/SSRT/SSRTTileClassification.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsPS,        "/Engine/Private/SSRT/SSRTReflections.usf", "ScreenSpaceReflectionsPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsTileVS,    "/Engine/Private/SingleLayerWaterComposite.usf", "WaterTileVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceReflectionsStencilPS, "/Engine/Private/SSRT/SSRTReflections.usf", "ScreenSpaceReflectionsStencilPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceDiffuseIndirectCS, "/Engine/Private/SSRT/SSRTDiffuseIndirect.usf", "MainCS", SF_Compute);


void GetSSRShaderOptionsForQuality(ESSRQuality Quality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs)
{
	if (Quality == ESSRQuality::VisualizeSSR)
	{
		OutRayTracingConfigs->RayCountPerPixel = 12;
	}
	else if (Quality == ESSRQuality::Epic)
	{
		OutRayTracingConfigs->RayCountPerPixel = 12;
	}
	else if (Quality == ESSRQuality::High)
	{
		OutRayTracingConfigs->RayCountPerPixel = 4;
	}
	else if (Quality == ESSRQuality::Medium)
	{
		OutRayTracingConfigs->RayCountPerPixel = 1;
	}
	else if (Quality == ESSRQuality::Low)
	{
		OutRayTracingConfigs->RayCountPerPixel = 1;
	}
	else
	{
		check(0);
	}
}

void GetSSRTGIShaderOptionsForQuality(int32 Quality, FIntPoint* OutGroupSize, int32* OutRayCountPerPixel)
{
	if (Quality == 1)
	{
		OutGroupSize->X = 8;
		OutGroupSize->Y = 8;
		*OutRayCountPerPixel = 4;
	}
	else if (Quality == 2)
	{
		OutGroupSize->X = 8;
		OutGroupSize->Y = 4;
		*OutRayCountPerPixel = 8;
	}
	else if (Quality == 3)
	{
		OutGroupSize->X = 4;
		OutGroupSize->Y = 4;
		*OutRayCountPerPixel = 16;
	}
	else if (Quality == 4)
	{
		OutGroupSize->X = 4;
		OutGroupSize->Y = 2;
		*OutRayCountPerPixel = 32;
	}
	else
	{
		check(0);
	}

	check(OutGroupSize->X * OutGroupSize->Y * (*OutRayCountPerPixel) == 256);
}

FRDGTextureUAV* CreateScreenSpaceRayTracingDebugUAV(FRDGBuilder& GraphBuilder, const FRDGTextureDesc& Desc, const TCHAR* Name, bool bClear = false)
#if 0
{
	FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2D(
		Desc.Extent,
		PF_FloatRGBA,
		FClearValueBinding::None,
		/* InFlags = */ TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTexture* DebugTexture = GraphBuilder.CreateTexture(DebugDesc, Name);
	FRDGTextureUAVRef DebugOutput = GraphBuilder.CreateUAV(DebugTexture);
	if (bClear)
		AddClearUAVPass(GraphBuilder, DebugOutput, FLinearColor::Transparent);
	return DebugOutput;
}
#else
{
	return nullptr;
}
#endif

} // namespace

void GetSSRQualityForView(const FViewInfo& View, ESSRQuality* OutQuality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs)
{
	check(ShouldRenderScreenSpaceReflections(View));
	
	int32 SSRQualityCVar = FMath::Clamp(CVarSSRQuality.GetValueOnRenderThread(), 0, int32(ESSRQuality::MAX) - 1);
	
	if (View.Family->EngineShowFlags.VisualizeSSR)
	{
		*OutQuality = ESSRQuality::VisualizeSSR;
		return;
	}
	else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 80.0f && SSRQualityCVar >= 4)
	{
		*OutQuality = ESSRQuality::Epic;
	}
	else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 60.0f && SSRQualityCVar >= 3)
	{
		*OutQuality = ESSRQuality::High;
	}
	else if (View.FinalPostProcessSettings.ScreenSpaceReflectionQuality >= 40.0f && SSRQualityCVar >= 2)
	{
		*OutQuality = ESSRQuality::Medium;
	}
	else
	{
		*OutQuality = ESSRQuality::Low;
	}

	GetSSRShaderOptionsForQuality(*OutQuality, OutRayTracingConfigs);
}

void RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	ESSRQuality SSRQuality,
	bool bDenoiser,
	IScreenSpaceDenoiser::FReflectionsInputs* DenoiserInputs,
	FTiledScreenSpaceReflection* TiledScreenSpaceReflection)
{
	FRDGTextureRef InputColor = CurrentSceneColor;
	if (SSRQuality != ESSRQuality::VisualizeSSR)
	{
		if (View.PrevViewInfo.CustomSSRInput.IsValid())
		{
			InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.CustomSSRInput);
		}
		else if (GSSRHalfResSceneColor && View.PrevViewInfo.HalfResTemporalAAHistory.IsValid())
		{
			InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.HalfResTemporalAAHistory);
		}
		else if (View.PrevViewInfo.TemporalAAHistory.IsValid())
		{
			InputColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.TemporalAAHistory.RT[0]);
		}
	}

	const bool SSRStencilPrePass = CVarSSRStencil.GetValueOnRenderThread() != 0 && SSRQuality != ESSRQuality::VisualizeSSR && TiledScreenSpaceReflection == nullptr;
	
	// Alloc inputs for denoising.
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			FSceneRenderTargets::Get_FrameConstantsOnly().GetBufferSizeXY(),
			PF_FloatRGBA, FClearValueBinding(FLinearColor(0, 0, 0, 0)),
			TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

		Desc.Flags |= GFastVRamConfig.SSR;

		DenoiserInputs->Color = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceReflections"));

		if (bDenoiser)
		{
			Desc.Format = PF_R16F;
			DenoiserInputs->RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("ScreenSpaceReflectionsHitDistance"));
		}
	}

	IScreenSpaceDenoiser::FReflectionsRayTracingConfig RayTracingConfigs;
	GetSSRShaderOptionsForQuality(SSRQuality, &RayTracingConfigs);
		
	FSSRCommonParameters CommonParameters;
	CommonParameters.SSRParams = ComputeSSRParams(View, SSRQuality, false);
	CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;
	CommonParameters.SceneTextures = SceneTextures;
	// Pipe down a mid grey texture when not using TAA's history to avoid wrongly reprojecting current scene color as if previous frame's TAA history.
	if (InputColor == CurrentSceneColor || !CommonParameters.SceneTextures.GBufferVelocityTexture)
	{
		// Technically should be 32767.0f / 65535.0f to perfectly null out DecodeVelocityFromTexture(), but 0.5f is good enough.
		CommonParameters.SceneTextures.GBufferVelocityTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.MidGreyDummy);
	}
	
	FRenderTargetBindingSlots RenderTargets;
	RenderTargets[0] = FRenderTargetBinding(DenoiserInputs->Color, ERenderTargetLoadAction::ENoAction);

	if (bDenoiser)
	{
		RenderTargets[1] = FRenderTargetBinding(DenoiserInputs->RayHitDistance, ERenderTargetLoadAction::ENoAction);
	}

	// Do a pre pass that output 0, or set a stencil mask to run the more expensive pixel shader.
	if (SSRStencilPrePass)
	{
		// Also bind the depth buffer
		RenderTargets.DepthStencil = FDepthStencilBinding(
			SceneTextures.SceneDepthTexture,
			ERenderTargetLoadAction::ENoAction,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthNop_StencilWrite);

		FScreenSpaceReflectionsStencilPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FSSROutputForDenoiser>(bDenoiser);

		FScreenSpaceReflectionsStencilPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceReflectionsStencilPS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->RenderTargets = RenderTargets;
		
		TShaderMapRef<FScreenSpaceReflectionsStencilPS> PixelShader(View.ShaderMap, PermutationVector);
		ClearUnusedGraphResources(PixelShader, PassParameters);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR StencilSetup %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, PixelShader](FRHICommandList& RHICmdList)
		{
			SCOPED_GPU_STAT(RHICmdList, ScreenSpaceReflections);
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, PixelShader, /* out */ GraphicsPSOInit);
			// Clobers the stencil to pixel that should not compute SSR
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always, true, CF_Always, SO_Replace, SO_Replace, SO_Replace>::GetRHI();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			RHICmdList.SetStencilRef(0x80);

			FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
		});
	}

	// Adds SSR pass.
	auto SetSSRParameters = [&](auto* PassParameters)
	{
		{
			const FVector2D HZBUvFactor(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));
			PassParameters->HZBUvFactorAndInvFactor = FVector4(
				HZBUvFactor.X,
				HZBUvFactor.Y,
				1.0f / HZBUvFactor.X,
				1.0f / HZBUvFactor.Y);
		}
		{
			FIntPoint ViewportOffset = View.ViewRect.Min;
			FIntPoint ViewportExtent = View.ViewRect.Size();
			FIntPoint BufferSize = SceneTextures.SceneDepthTexture->Desc.Extent;

			if (View.PrevViewInfo.TemporalAAHistory.IsValid())
			{
				ViewportOffset = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Min;
				ViewportExtent = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Size();
				BufferSize = View.PrevViewInfo.TemporalAAHistory.ReferenceBufferSize;
				ensure(ViewportExtent.X > 0 && ViewportExtent.Y > 0);
				ensure(BufferSize.X > 0 && BufferSize.Y > 0);
			}

			FVector2D InvBufferSize(1.0f / float(BufferSize.X), 1.0f / float(BufferSize.Y));

			PassParameters->PrevScreenPositionScaleBias = FVector4(
				ViewportExtent.X * 0.5f * InvBufferSize.X,
				-ViewportExtent.Y * 0.5f * InvBufferSize.Y,
				(ViewportExtent.X * 0.5f + ViewportOffset.X) * InvBufferSize.X,
				(ViewportExtent.Y * 0.5f + ViewportOffset.Y) * InvBufferSize.Y);

			PassParameters->ScreenSpaceRayTracingDebugOutput = CreateScreenSpaceRayTracingDebugUAV(GraphBuilder, DenoiserInputs->Color->Desc, TEXT("DebugSSR"), true);
		}
		PassParameters->PrevSceneColorPreExposureCorrection = InputColor != CurrentSceneColor ? View.PreExposure / View.PrevViewInfo.SceneColorPreExposure : 1.0f;
		
		PassParameters->SceneColor = InputColor;
		PassParameters->SceneColorSampler = GSSRHalfResSceneColor ? TStaticSamplerState<SF_Bilinear>::GetRHI() : TStaticSamplerState<SF_Point>::GetRHI();
		
		PassParameters->HZB = GraphBuilder.RegisterExternalTexture(View.HZB);
		PassParameters->HZBSampler = TStaticSamplerState<SF_Point>::GetRHI();
	};

	FScreenSpaceReflectionsPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FSSRQualityDim>(SSRQuality);
	PermutationVector.Set<FSSROutputForDenoiser>(bDenoiser);
		
	FScreenSpaceReflectionsPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceReflectionsPS::FParameters>();
	PassParameters->CommonParameters = CommonParameters;
	SetSSRParameters(&PassParameters->SSRPassCommonParameter);
	PassParameters->RenderTargets = RenderTargets;

	TShaderMapRef<FScreenSpaceReflectionsPS> PixelShader(View.ShaderMap, PermutationVector);

	if (TiledScreenSpaceReflection == nullptr)
	{
		ClearUnusedGraphResources(PixelShader, PassParameters);
		
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR RayMarch(Quality=%d RayPerPixel=%d%s) %dx%d",
				SSRQuality, RayTracingConfigs.RayCountPerPixel, bDenoiser ? TEXT(" DenoiserOutput") : TEXT(""),
				View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, PixelShader, SSRStencilPrePass](FRHICommandList& RHICmdList)
		{
			SCOPED_GPU_STAT(RHICmdList, ScreenSpaceReflections);
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, PixelShader, /* out */ GraphicsPSOInit);
			if (SSRStencilPrePass)
			{
				// Clobers the stencil to pixel that should not compute SSR
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always, true, CF_Equal, SO_Keep, SO_Keep, SO_Keep>::GetRHI();
			}

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			RHICmdList.SetStencilRef(0x80);

			FPixelShaderUtils::DrawFullscreenTriangle(RHICmdList);
		});
	}
	else
	{
		check(TiledScreenSpaceReflection->TileSize == 8); // WORK_TILE_SIZE

		FScreenSpaceReflectionsTileVS::FPermutationDomain VsPermutationVector;
		TShaderMapRef<FScreenSpaceReflectionsTileVS> VertexShader(View.ShaderMap, VsPermutationVector);

		PassParameters->TileListData = TiledScreenSpaceReflection->TileListStructureBufferSRV;
		PassParameters->IndirectDrawParameter = TiledScreenSpaceReflection->DispatchIndirectParametersBuffer;

		ClearUnusedGraphResources(VertexShader, PixelShader, PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SSR RayMarch(Quality=%d RayPerPixel=%d%s) %dx%d",
				SSRQuality, RayTracingConfigs.RayCountPerPixel, bDenoiser ? TEXT(" DenoiserOutput") : TEXT(""),
				View.ViewRect.Width(), View.ViewRect.Height()),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, &View, VertexShader, PixelShader, SSRStencilPrePass](FRHICommandList& RHICmdList)
		{
			SCOPED_GPU_STAT(RHICmdList, ScreenSpaceReflections);
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			FPixelShaderUtils::InitFullscreenPipelineState(RHICmdList, View.ShaderMap, PixelShader, /* out */ GraphicsPSOInit);
			if (SSRStencilPrePass)
			{
				// Clobers the stencil to pixel that should not compute SSR
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always, true, CF_Equal, SO_Keep, SO_Keep, SO_Keep>::GetRHI();
			}
			GraphicsPSOInit.PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			RHICmdList.SetStencilRef(0x80);

			PassParameters->IndirectDrawParameter->MarkResourceAsUsed();

			RHICmdList.DrawPrimitiveIndirect(PassParameters->IndirectDrawParameter->GetIndirectRHICallBuffer(), 0);
		});
	}
} // RenderScreenSpaceReflections()

void RenderScreenSpaceDiffuseIndirect(
	FRDGBuilder& GraphBuilder, 
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig* OutRayTracingConfig,
	IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs)
{
	check(ShouldRenderScreenSpaceDiffuseIndirect(View));

	const int32 Quality = FMath::Clamp( CVarSSGIQuality.GetValueOnRenderThread(), 1, 4 );

	bool bHalfResolution = IsSSGIHalfRes();

	FIntPoint GroupSize;
	int32 RayCountPerPixel;
	GetSSRTGIShaderOptionsForQuality(Quality, &GroupSize, &RayCountPerPixel);

	FIntRect Viewport = View.ViewRect;
	if (bHalfResolution)
	{
		Viewport = FIntRect::DivideAndRoundUp(Viewport, 2);
	}

	RDG_EVENT_SCOPE(GraphBuilder, "SSGI %dx%d", Viewport.Width(), Viewport.Height());

	const FVector2D ViewportUVToHZBBufferUV(
		float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
		float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y)
	);

	FRDGTexture* FurthestHZBTexture = GraphBuilder.RegisterExternalTexture(View.HZB);
	FRDGTexture* ClosestHZBTexture = GraphBuilder.RegisterExternalTexture(View.ClosestHZB);

	// Reproject and reduce previous frame color.
	FRDGTexture* ReducedSceneColor;
	FRDGTexture* ReducedSceneAlpha = nullptr;
	{
		// Number of mip skipped at the begining of the mip chain.
		const int32 DownSamplingMip = 1;

		// Number of mip in the mip chain
		const int32 kNumMips = 5;

		bool bUseLeakFree = View.PrevViewInfo.ScreenSpaceRayTracingInput != nullptr;

		// Allocate ReducedSceneColor.
		{
			FIntPoint RequiredSize = SceneTextures.SceneDepthTexture->Desc.Extent / (1 << DownSamplingMip);

			int32 QuantizeMultiple = 1 << (kNumMips - 1);
			FIntPoint QuantizedSize = FIntPoint::DivideAndRoundUp(RequiredSize, QuantizeMultiple);

			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				FIntPoint(QuantizeMultiple * QuantizedSize.X, QuantizeMultiple * QuantizedSize.Y),
				PF_FloatR11G11B10,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			Desc.NumMips = kNumMips;

			ReducedSceneColor = GraphBuilder.CreateTexture(Desc, TEXT("SSRTReducedSceneColor"));

			if (bUseLeakFree)
			{
				Desc.Format = PF_A8;
				ReducedSceneAlpha = GraphBuilder.CreateTexture(Desc, TEXT("SSRTReducedSceneAlpha"));
			}
		}

		FSSRTPrevFrameReductionCS::FParameters DefaultPassParameters;
		{
			DefaultPassParameters.SceneTextures = SceneTextures;
			DefaultPassParameters.View = View.ViewUniformBuffer;

			DefaultPassParameters.ReducedSceneColorSize = FVector2D(
				ReducedSceneColor->Desc.Extent.X, ReducedSceneColor->Desc.Extent.Y);
			DefaultPassParameters.ReducedSceneColorTexelSize = FVector2D(
				1.0f / float(ReducedSceneColor->Desc.Extent.X), 1.0f / float(ReducedSceneColor->Desc.Extent.Y));
		}

		{
			FSSRTPrevFrameReductionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSSRTPrevFrameReductionCS::FParameters>();
			*PassParameters = DefaultPassParameters;

			FIntPoint ViewportOffset;
			FIntPoint ViewportExtent;
			FIntPoint BufferSize;

			if (bUseLeakFree)
			{
				BufferSize = View.PrevViewInfo.ScreenSpaceRayTracingInput->GetDesc().Extent;
				ViewportOffset = View.ViewRect.Min; // TODO
				ViewportExtent = View.ViewRect.Size();

				PassParameters->PrevSceneColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.ScreenSpaceRayTracingInput);
				PassParameters->PrevSceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();

				PassParameters->PrevSceneDepth = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.DepthBuffer);
				PassParameters->PrevSceneDepthSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			}
			else
			{
				BufferSize = View.PrevViewInfo.TemporalAAHistory.ReferenceBufferSize;
				ViewportOffset = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Min;
				ViewportExtent = View.PrevViewInfo.TemporalAAHistory.ViewportRect.Size();

				PassParameters->PrevSceneColor = GraphBuilder.RegisterExternalTexture(View.PrevViewInfo.TemporalAAHistory.RT[0]);
				PassParameters->PrevSceneColorSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			}

			PassParameters->PrevSceneColorPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;

			PassParameters->PrevScreenPositionScaleBias = FVector4(
				ViewportExtent.X * 0.5f / BufferSize.X,
				-ViewportExtent.Y * 0.5f / BufferSize.Y,
				(ViewportExtent.X * 0.5f + ViewportOffset.X) / BufferSize.X,
				(ViewportExtent.Y * 0.5f + ViewportOffset.Y) / BufferSize.Y);

			for (int32 MipLevel = 0; MipLevel < (PassParameters->ReducedSceneColorOutput.Num() - DownSamplingMip); MipLevel++)
			{
				PassParameters->ReducedSceneColorOutput[DownSamplingMip + MipLevel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReducedSceneColor, MipLevel));
				if (ReducedSceneAlpha)
					PassParameters->ReducedSceneAlphaOutput[DownSamplingMip + MipLevel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReducedSceneAlpha, MipLevel));
			}

			FSSRTPrevFrameReductionCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSSRTPrevFrameReductionCS::FLowerMips>(false); 
			PermutationVector.Set<FSSRTPrevFrameReductionCS::FLeakFree>(bUseLeakFree);

			TShaderMapRef<FSSRTPrevFrameReductionCS> ComputeShader(View.ShaderMap, PermutationVector);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PrevFrameReduction(LeakFree=%i) %dx%d",
					bUseLeakFree ? 1 : 0,
					View.ViewRect.Width(), View.ViewRect.Height()),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), 8));
		}

		for (int32 i = 0; i < 1; i++)
		{
			int32 SrcMip = i * 3 + 2 - DownSamplingMip;
			int32 StartDestMip = SrcMip + 1;
			int32 Divisor = 1 << (StartDestMip + DownSamplingMip);

			FSSRTPrevFrameReductionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSSRTPrevFrameReductionCS::FParameters>();
			*PassParameters = DefaultPassParameters;

			PassParameters->HigherMipTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(ReducedSceneColor, SrcMip));
			if (bUseLeakFree)
			{
				check(ReducedSceneAlpha);
				PassParameters->HigherMipTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
				PassParameters->HigherAlphaMipTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(ReducedSceneAlpha, SrcMip));
				PassParameters->HigherAlphaMipTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
			}
			else
			{
				PassParameters->HigherMipTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
			}

			PassParameters->HigherMipDownScaleFactor = 1 << (DownSamplingMip + SrcMip);

			PassParameters->HigherMipBufferBilinearMax = FVector2D(
				(0.5f * View.ViewRect.Width() - 0.5f) / float(ReducedSceneColor->Desc.Extent.X),
				(0.5f * View.ViewRect.Height() - 0.5f) / float(ReducedSceneColor->Desc.Extent.Y));

			PassParameters->ViewportUVToHZBBufferUV = ViewportUVToHZBBufferUV;
			PassParameters->FurthestHZBTexture = FurthestHZBTexture;
			PassParameters->FurthestHZBTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

			for (int32 MipLevel = 0; MipLevel < PassParameters->ReducedSceneColorOutput.Num(); MipLevel++)
			{
				PassParameters->ReducedSceneColorOutput[MipLevel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReducedSceneColor, StartDestMip + MipLevel));
				if (ReducedSceneAlpha)
					PassParameters->ReducedSceneAlphaOutput[MipLevel] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReducedSceneAlpha, StartDestMip + MipLevel));
			}

			FSSRTPrevFrameReductionCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSSRTPrevFrameReductionCS::FLowerMips>(true);
			PermutationVector.Set<FSSRTPrevFrameReductionCS::FLeakFree>(bUseLeakFree);

			TShaderMapRef<FSSRTPrevFrameReductionCS> ComputeShader(View.ShaderMap, PermutationVector);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PrevFrameReduction(LeakFree=%i) %dx%d",
					bUseLeakFree ? 1 : 0,
					View.ViewRect.Width() / Divisor, View.ViewRect.Height() / Divisor),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), 8 * Divisor));
		}
	}

	// Tile classify.
	FSSRTTileClassificationParameters ClassificationParameters;
	FSSRTTileClassificationResources ClassificationResources;
	#if 0
	{
		ClassificationResources = CreateTileClassificationResources(GraphBuilder, View, SceneTextures.SceneDepthTexture->Desc.Extent, &ClassificationParameters);

		FIntPoint ThreadCount = ClassificationParameters.TileBufferExtent;

		FSSRTDiffuseTileClassificationCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSSRTDiffuseTileClassificationCS::FParameters>();
		PassParameters->SamplePixelToHZBUV = FVector2D(
			0.5f / float(FurthestHZBTexture->Desc.Extent.X),
			0.5f / float(FurthestHZBTexture->Desc.Extent.Y));

		PassParameters->FurthestHZBTexture = FurthestHZBTexture;
		PassParameters->FurthestHZBTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->ColorTexture = ReducedSceneColor;
		PassParameters->ColorTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

		PassParameters->ClosestHZBTexture = ClosestHZBTexture;
		PassParameters->ClosestHZBTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

		PassParameters->View = View.ViewUniformBuffer;
			
		PassParameters->TileClassificationParameters = ClassificationParameters;
		PassParameters->TileClassificationUAVs = CreateUAVs(GraphBuilder, ClassificationResources);

		{
			FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2D(
				SceneTextures.SceneDepthTexture->Desc.Extent / 8,
				PF_FloatRGBA,
				FClearValueBinding::Transparent,
				TexCreate_ShaderResource | TexCreate_UAV);

			PassParameters->DebugOutput = GraphBuilder.CreateUAV(GraphBuilder.CreateTexture(DebugDesc, TEXT("DebugSSRTTiles")));
		}

		TShaderMapRef<FSSRTDiffuseTileClassificationCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ScreenSpaceDiffuseClassification %dx%d", ThreadCount.X, ThreadCount.Y),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ThreadCount, 8));
	}
	#endif

	{
		// Allocate outputs.
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				SceneTextures.SceneDepthTexture->Desc.Extent / (bHalfResolution ? 2 : 1),
				PF_FloatRGBA,
				FClearValueBinding::Transparent,
				TexCreate_ShaderResource | TexCreate_UAV);

			OutDenoiserInputs->Color = GraphBuilder.CreateTexture(Desc, TEXT("SSRTDiffuseIndirect"));

			Desc.Format = PF_R16F;
			Desc.Flags |= TexCreate_RenderTargetable;
			OutDenoiserInputs->AmbientOcclusionMask = GraphBuilder.CreateTexture(Desc, TEXT("SSRTAmbientOcclusion"));
		}
	
		FScreenSpaceDiffuseIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceDiffuseIndirectCS::FParameters>();

		if (bHalfResolution)
		{
			PassParameters->PixelPositionToFullResPixel = 2.0f;
			PassParameters->FullResPixelOffset = FVector2D(0.5f, 0.5f); // TODO.
		}
		else
		{
			PassParameters->PixelPositionToFullResPixel = 1.0f;
			PassParameters->FullResPixelOffset = FVector2D(0.5f, 0.5f);
		}

		{
			// float2 SceneBufferUV;
			// float2 PixelPos = SceneBufferUV * View.BufferSizeAndInvSize.xy - View.ViewRect.Min;
			// PixelPos *= 0.5 // ReducedSceneColor is half resolution.
			// float2 ReducedSceneColorUV = PixelPos / ReducedSceneColor->Extent;

			PassParameters->ColorBufferScaleBias = FVector4(
				0.5f * SceneTextures.SceneDepthTexture->Desc.Extent.X / float(ReducedSceneColor->Desc.Extent.X),
				0.5f * SceneTextures.SceneDepthTexture->Desc.Extent.Y / float(ReducedSceneColor->Desc.Extent.Y),
				-0.5f * View.ViewRect.Min.X / float(ReducedSceneColor->Desc.Extent.X),
				-0.5f * View.ViewRect.Min.Y / float(ReducedSceneColor->Desc.Extent.Y));

			PassParameters->ReducedColorUVMax = FVector2D(
				(0.5f * View.ViewRect.Width() - 0.5f) / float(ReducedSceneColor->Desc.Extent.X),
				(0.5f * View.ViewRect.Height() - 0.5f) / float(ReducedSceneColor->Desc.Extent.Y));
		}

		PassParameters->FurthestHZBTexture = FurthestHZBTexture;
		PassParameters->FurthestHZBTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->ColorTexture = ReducedSceneColor;
		PassParameters->ColorTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

		PassParameters->HZBUvFactorAndInvFactor = FVector4(
			ViewportUVToHZBBufferUV.X,
			ViewportUVToHZBBufferUV.Y,
			1.0f / ViewportUVToHZBBufferUV.X,
			1.0f / ViewportUVToHZBBufferUV.Y );

		PassParameters->SceneTextures = SceneTextures;
		PassParameters->View = View.ViewUniformBuffer;
	
		//PassParameters->ClassificationParameters = ClassificationParameters;
		//PassParameters->ClassificationSRVs = CreateSRVs(GraphBuilder, ClassificationResources);

		PassParameters->IndirectDiffuseOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
		PassParameters->AmbientOcclusionOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->AmbientOcclusionMask);
		PassParameters->DebugOutput = CreateScreenSpaceRayTracingDebugUAV(GraphBuilder, OutDenoiserInputs->Color->Desc, TEXT("DebugSSGI"));
		PassParameters->ScreenSpaceRayTracingDebugOutput = CreateScreenSpaceRayTracingDebugUAV(GraphBuilder, OutDenoiserInputs->Color->Desc, TEXT("DebugSSGIMarshing"), true);

		FScreenSpaceDiffuseIndirectCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FScreenSpaceDiffuseIndirectCS::FQualityDim>(Quality);

		TShaderMapRef<FScreenSpaceDiffuseIndirectCS> ComputeShader(View.ShaderMap, PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ScreenSpaceDiffuseIndirect(Quality=%d RayPerPixel=%d) %dx%d",
				Quality, RayCountPerPixel, Viewport.Width(), Viewport.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(Viewport.Size(), GroupSize));
	}

	OutRayTracingConfig->ResolutionFraction = bHalfResolution ? 0.5f : 1.0f;
	OutRayTracingConfig->RayCountPerPixel = RayCountPerPixel;
} // RenderScreenSpaceDiffuseIndirect()
