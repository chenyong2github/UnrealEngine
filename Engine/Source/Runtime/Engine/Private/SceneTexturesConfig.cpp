// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneTexturesConfig.h"
#include "RenderGraphResources.h"
#include "ShaderCompiler.h"
#include "SceneInterface.h"

FSceneTexturesConfig FSceneTexturesConfig::GlobalInstance;

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FSceneTextureUniformParameters, "SceneTexturesStruct", SceneTextures);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FMobileSceneTextureUniformParameters, "MobileSceneTextures", SceneTextures);

FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.SceneTextures = UniformBuffer;
	return Parameters;
}

FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.MobileSceneTextures = UniformBuffer;
	return Parameters;
}

static EPixelFormat GetDefaultMobileSceneColorLowPrecisionFormat()
{
	return (GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStandaloneStereoOnlyDevice()) ? PF_R8G8B8A8 : PF_B8G8R8A8;
}

static EPixelFormat GetMobileSceneColorFormat(bool bRequiresAlphaChannel)
{
	EPixelFormat DefaultColorFormat;
	const bool bUseLowPrecisionFormat = !IsMobileHDR() || !GSupportsRenderTargetFormat_PF_FloatRGBA;
	if (bUseLowPrecisionFormat)
	{
		DefaultColorFormat = GetDefaultMobileSceneColorLowPrecisionFormat();
	}
	else
	{
		DefaultColorFormat = bRequiresAlphaChannel ? PF_FloatRGBA : PF_FloatR11G11B10;
	}

	check(GPixelFormats[DefaultColorFormat].Supported);

	EPixelFormat Format = DefaultColorFormat;
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.SceneColorFormat"));
	int32 MobileSceneColor = CVar->GetValueOnRenderThread();
	switch (MobileSceneColor)
	{
	case 1:
		Format = PF_FloatRGBA; break;
	case 2:
		Format = PF_FloatR11G11B10; break;
	case 3:
		if (bUseLowPrecisionFormat)
		{
			// if bUseLowPrecisionFormat, DefaultColorFormat already contains the value of GetDefaultMobileSceneColorLowPrecisionFormat
			checkSlow(DefaultColorFormat == GetDefaultMobileSceneColorLowPrecisionFormat());
			Format = DefaultColorFormat;
		}
		else
		{
			Format = GetDefaultMobileSceneColorLowPrecisionFormat();
		}
		break;
	default:
		break;
	}

	return GPixelFormats[Format].Supported ? Format : DefaultColorFormat;
}

static EPixelFormat GetSceneColorFormat(bool bRequiresAlphaChannel)
{
	EPixelFormat Format = PF_FloatRGBA;

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SceneColorFormat"));

	switch (CVar->GetValueOnAnyThread())
	{
	case 0:
		Format = PF_R8G8B8A8; break;
	case 1:
		Format = PF_A2B10G10R10; break;
	case 2:
		Format = PF_FloatR11G11B10; break;
	case 3:
		Format = PF_FloatRGB; break;
	case 4:
		// default
		break;
	case 5:
		Format = PF_A32B32G32R32F; break;
	}

	// Fallback in case the scene color selected isn't supported.
	if (!GPixelFormats[Format].Supported)
	{
		Format = PF_FloatRGBA;
	}

	if (bRequiresAlphaChannel)
	{
		Format = PF_FloatRGBA;
	}

	return Format;
}

static void GetSceneColorFormatAndCreateFlags(ERHIFeatureLevel::Type FeatureLevel, bool bRequiresAlphaChannel, ETextureCreateFlags ExtraSceneColorCreateFlags, uint32 NumSamples, bool bMemorylessMSAA, EPixelFormat& SceneColorFormat, ETextureCreateFlags& SceneColorCreateFlags)
{
	EShadingPath ShadingPath = FSceneInterface::GetShadingPath(FeatureLevel);
	switch (ShadingPath)
	{
	case EShadingPath::Deferred:
	{
		SceneColorFormat = GetSceneColorFormat(bRequiresAlphaChannel);
		break;
	}

	case EShadingPath::Mobile:
	{
		SceneColorFormat = GetMobileSceneColorFormat(bRequiresAlphaChannel);
		break;
	}

	default:
		checkNoEntry();
	}

	const bool bIsMobilePlatform = ShadingPath == EShadingPath::Mobile;

	SceneColorCreateFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource | ExtraSceneColorCreateFlags;
	const ETextureCreateFlags sRGBFlag = (bIsMobilePlatform && IsMobileColorsRGB()) ? TexCreate_SRGB : TexCreate_None;
	if (FeatureLevel >= ERHIFeatureLevel::SM5 && NumSamples == 1)
	{
		SceneColorCreateFlags |= TexCreate_UAV;
	}
	if (NumSamples > 1 && bMemorylessMSAA)
	{
		SceneColorCreateFlags |= TexCreate_Memoryless;
	}
	SceneColorCreateFlags |= sRGBFlag;
}

static ETextureCreateFlags GetSceneDepthStencilCreateFlags(uint32 NumSamples, bool bKeepDepthContent, bool bMemorylessMSAA, ETextureCreateFlags ExtraSceneDepthCreateFlags)
{
	ETextureCreateFlags DepthCreateFlags = TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | ExtraSceneDepthCreateFlags;
	if (!bKeepDepthContent || (NumSamples > 1 && bMemorylessMSAA))
	{
		DepthCreateFlags = TexCreate_Memoryless;
	}
	if (NumSamples == 1 && GRHISupportsDepthUAV)
	{
		DepthCreateFlags |= TexCreate_UAV;
	}
	return DepthCreateFlags;
}

static uint32 GetEditorPrimitiveNumSamples(ERHIFeatureLevel::Type FeatureLevel)
{
	uint32 SampleCount = 1;

	if (FeatureLevel >= ERHIFeatureLevel::SM5 && GRHISupportsMSAADepthSampleAccess)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MSAA.CompositingSampleCount"));

		SampleCount = CVar->GetValueOnAnyThread();

		if (SampleCount <= 1)
		{
			SampleCount = 1;
		}
		else if (SampleCount <= 2)
		{
			SampleCount = 2;
		}
		else if (SampleCount <= 4)
		{
			SampleCount = 4;
		}
		else
		{
			SampleCount = 8;
		}
	}

	return SampleCount;
}

void FSceneTexturesConfig::Init(const FSceneTexturesConfigInitSettings& InitSettings)
{
	FeatureLevel			= InitSettings.FeatureLevel;
	ShadingPath				= FSceneInterface::GetShadingPath(FeatureLevel);
	ShaderPlatform			= GetFeatureLevelShaderPlatform(FeatureLevel);
	Extent					= InitSettings.Extent;
	NumSamples				= GetDefaultMSAACount(FeatureLevel, GDynamicRHI->RHIGetPlatformTextureMaxSampleCount());
	EditorPrimitiveNumSamples = GetEditorPrimitiveNumSamples(FeatureLevel);
	ColorFormat				= PF_Unknown;
	ColorClearValue			= FClearValueBinding::Black;
	DepthClearValue			= FClearValueBinding::DepthFar;
	bRequireMultiView		= InitSettings.bRequireMultiView;
	bIsUsingGBuffers		= IsUsingGBuffers(ShaderPlatform);
	bSupportsXRTargetManagerDepthAlloc = InitSettings.bSupportsXRTargetManagerDepthAlloc;

	GetSceneColorFormatAndCreateFlags(FeatureLevel, InitSettings.bRequiresAlphaChannel, InitSettings.ExtraSceneColorCreateFlags, NumSamples, bMemorylessMSAA, ColorFormat, ColorCreateFlags);
	DepthCreateFlags = GetSceneDepthStencilCreateFlags(NumSamples, bKeepDepthContent, bMemorylessMSAA, InitSettings.ExtraSceneDepthCreateFlags);

	if (bIsUsingGBuffers)
	{
		GBufferParams = FShaderCompileUtilities::FetchGBufferParamsRuntime(ShaderPlatform);

		// GBuffer configuration information is expensive to compute, the results are cached between runs.
		struct FGBufferBindingCache
		{
			FGBufferParams GBufferParams;
			FGBufferBinding GBufferA;
			FGBufferBinding GBufferB;
			FGBufferBinding GBufferC;
			FGBufferBinding GBufferD;
			FGBufferBinding GBufferE;
			FGBufferBinding GBufferVelocity;
			bool bInitialized = false;
		};
		static FGBufferBindingCache BindingCache;

		if (!BindingCache.bInitialized || BindingCache.GBufferParams != GBufferParams)
		{
			const FGBufferInfo GBufferInfo = FetchFullGBufferInfo(GBufferParams);

			BindingCache.GBufferParams = GBufferParams;
			BindingCache.GBufferA = FindGBufferBindingByName(GBufferInfo, TEXT("GBufferA"));
			BindingCache.GBufferB = FindGBufferBindingByName(GBufferInfo, TEXT("GBufferB"));
			BindingCache.GBufferC = FindGBufferBindingByName(GBufferInfo, TEXT("GBufferC"));
			BindingCache.GBufferD = FindGBufferBindingByName(GBufferInfo, TEXT("GBufferD"));
			BindingCache.GBufferE = FindGBufferBindingByName(GBufferInfo, TEXT("GBufferE"));
			BindingCache.GBufferVelocity = FindGBufferBindingByName(GBufferInfo, TEXT("Velocity"));
			BindingCache.bInitialized = true;
		}

		GBufferA = BindingCache.GBufferA;
		GBufferB = BindingCache.GBufferB;
		GBufferC = BindingCache.GBufferC;
		GBufferD = BindingCache.GBufferD;
		GBufferE = BindingCache.GBufferE;
		GBufferVelocity = BindingCache.GBufferVelocity;
	}
}

uint32 FSceneTexturesConfig::GetGBufferRenderTargetsInfo(FGraphicsPipelineRenderTargetsInfo& RenderTargetsInfo) const 
{
	// Assume 1 sample for now
	RenderTargetsInfo.NumSamples = 1;

	uint32 RenderTargetCount = 0;

	// All configurations use scene color in the first slot.
	RenderTargetsInfo.RenderTargetFormats[RenderTargetCount] = ColorFormat;
	RenderTargetsInfo.RenderTargetFlags[RenderTargetCount++] = ColorCreateFlags;

	// Setup the other render targets
	if (bIsUsingGBuffers)
	{
		const auto CheckGBufferBinding = [&RenderTargetsInfo, &RenderTargetCount](const FGBufferBinding& GBufferBinding)
		{
			if (GBufferBinding.Index > 0)
			{
				RenderTargetsInfo.RenderTargetFormats[GBufferBinding.Index] = GBufferBinding.Format;
				RenderTargetsInfo.RenderTargetFlags[GBufferBinding.Index] = GBufferBinding.Flags;
				RenderTargetCount = FMath::Max(RenderTargetCount, (uint32)GBufferBinding.Index + 1);
			}
		};
		CheckGBufferBinding(GBufferA);
		CheckGBufferBinding(GBufferB);
		CheckGBufferBinding(GBufferC);
		CheckGBufferBinding(GBufferD);
		CheckGBufferBinding(GBufferE);
		CheckGBufferBinding(GBufferVelocity);
	}
	// Forward shading path. Simple forward shading does not use velocity.
	else if (IsUsingBasePassVelocity(ShaderPlatform))
	{
		RenderTargetsInfo.RenderTargetFormats[RenderTargetCount] = GBufferVelocity.Format;
		RenderTargetsInfo.RenderTargetFlags[RenderTargetCount++] = GBufferVelocity.Flags;
	}

	// Store final number of render targets
	RenderTargetsInfo.RenderTargetsEnabled = RenderTargetCount;

	// Precache TODO: other flags
	RenderTargetsInfo.MultiViewCount = 0;// RenderTargets.MultiViewCount;
	RenderTargetsInfo.bHasFragmentDensityAttachment = false; // RenderTargets.ShadingRateTexture != nullptr;

	return RenderTargetCount;
}