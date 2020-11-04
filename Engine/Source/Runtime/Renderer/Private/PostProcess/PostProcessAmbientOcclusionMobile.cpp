// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessAmbientOcclusionMobile.cpp
=============================================================================*/

#include "PostProcess/PostProcessAmbientOcclusionMobile.h"
#include "ShaderParameterStruct.h"
#include "SceneRendering.h"
#include "RenderTargetPool.h"
#include "SceneRenderTargets.h"
#include "SystemTextures.h"
#include "ScreenPass.h"
#include "ScenePrivate.h"

FAmbientOcclusionMobileOutputs GAmbientOcclusionMobileOutputs;

static TAutoConsoleVariable<int32> CVarMobileAmbientOcclusion(
	TEXT("r.Mobile.AmbientOcclusion"),
	0,
	TEXT("Causion: An extra sampler will be occupied in mobile base pass pixel shader after enable the mobile ambient occlusion.\n")
	TEXT("0: Disable Ambient Occlusion on mobile platform. [default]\n")
	TEXT("1: Enable Ambient Occlusion on mobile platform.\n"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMobileGTAOPreIntegratedTextureType(
	TEXT("r.Mobile.GTAOPreIntegratedTextureType"),
	2,
	TEXT("0: No Texture.\n")
	TEXT("1: Texture2D LUT.\n")
	TEXT("2: Volume LUT(Default)."),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarMobileAmbientOcclusionQuality(
	TEXT("r.Mobile.AmbientOcclusionQuality"),
	1,
	TEXT("The quality of screen space ambient occlusion on mobile platform.\n")
	TEXT("0: Disabled.\n")
	TEXT("1: Low.(Default)\n")
	TEXT("2: Medium.\n")
	TEXT("3: High.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarMobileAmbientOcclusionShaderType(
	TEXT("r.Mobile.AmbientOcclusionShaderType"),
	0,
	TEXT("0: ComputeShader.\n")
	TEXT("1: Seperate ComputeShader.\n")
	TEXT("2: PixelShader.\n"),
	ECVF_RenderThreadSafe
);

class FGTAOMobile_HorizonSearchIntegral : public FGlobalShader
{
public:
	class FLUTTextureTypeDim : SHADER_PERMUTATION_INT("PREINTEGRATED_LUT_TYPE", 3);
	class FShaderQualityDim : SHADER_PERMUTATION_INT("SHADER_QUALITY", 3);

	using FCommonPermutationDomain = TShaderPermutationDomain<
		FLUTTextureTypeDim,
		FShaderQualityDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_EX(FVector4, ViewRectMin, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_EX(FVector4, DepthBufferSizeAndInvSize, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_EX(FVector4, BufferSizeAndInvSize, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_EX(FVector4, ViewSizeAndInvSize, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER(FVector4, FadeRadiusMulAdd_FadeDistance_AttenFactor)
		SHADER_PARAMETER(FVector4, WorldRadiusAdj_SinDeltaAngle_CosDeltaAngle_Thickness)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthSampler)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NormalTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, NormalSampler)

		SHADER_PARAMETER_TEXTURE(Texture2D, GTAOPreIntegrated2D)
		SHADER_PARAMETER_TEXTURE(Texture3D, GTAOPreIntegrated3D)
		SHADER_PARAMETER_SAMPLER(SamplerState, GTAOPreIntegratedSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonPermutationDomain& CommonPermutationVector)
	{
		auto LUTTextureType = CommonPermutationVector.Get<FLUTTextureTypeDim>();

		int32 MobileGTAOPreIntegratedTextureType = CVarMobileGTAOPreIntegratedTextureType.GetValueOnAnyThread();
		return IsMobileAmbientOcclusionEnabled(Parameters.Platform) && (MobileGTAOPreIntegratedTextureType == LUTTextureType);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_NORMALBUFFER"), 0);
	}

	static FCommonPermutationDomain BuildPermutationVector(int32 LUTTextureType, int32 ShaderQuality)
	{
		FCommonPermutationDomain PermutationVector;
		PermutationVector.Set<FLUTTextureTypeDim>(LUTTextureType);
		PermutationVector.Set<FShaderQualityDim>(ShaderQuality);
		return PermutationVector;
	}

	static void SetupShaderParameters(FParameters& ShaderParameters, FRDGBuilder& GraphBuilder, const FViewInfo& View, const FIntRect& ViewRect, const FIntPoint& DepthBufferSize, const FIntPoint& BufferSize, const FVector4& FallOffStartEndScaleBias, const FVector4& WorldRadiusAdjSinCosDeltaAngleThickness, FRDGTextureRef SceneDepthTexture)
	{
		const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;

		float FadeRadius = FMath::Max(1.0f, Settings.AmbientOcclusionFadeRadius);
		float InvFadeRadius = 1.0f / FadeRadius;

		ShaderParameters.View = View.ViewUniformBuffer;
		ShaderParameters.ViewRectMin = FVector4(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, 0.0f);
		ShaderParameters.DepthBufferSizeAndInvSize = FVector4(DepthBufferSize.X, DepthBufferSize.Y, 1.0f / DepthBufferSize.X, 1.0f / DepthBufferSize.Y);
		ShaderParameters.BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, 1.0f / BufferSize.X, 1.0f / BufferSize.Y);
		ShaderParameters.ViewSizeAndInvSize = FVector4(ViewRect.Width(), ViewRect.Height(), 1.0f / ViewRect.Width(), 1.0f / ViewRect.Height());
		ShaderParameters.FadeRadiusMulAdd_FadeDistance_AttenFactor = FVector4(InvFadeRadius, -(Settings.AmbientOcclusionFadeDistance - FadeRadius) * InvFadeRadius, Settings.AmbientOcclusionFadeDistance, 2.0f / (FallOffStartEndScaleBias.Y * FallOffStartEndScaleBias.Y));
		ShaderParameters.WorldRadiusAdj_SinDeltaAngle_CosDeltaAngle_Thickness = WorldRadiusAdjSinCosDeltaAngleThickness;

		ShaderParameters.SceneDepthTexture = SceneDepthTexture;
		ShaderParameters.SceneDepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		if (GSystemTextures.GTAOPreIntegrated.IsValid())
		{
			ShaderParameters.GTAOPreIntegrated2D = GSystemTextures.GTAOPreIntegrated->GetRenderTargetItem().ShaderResourceTexture;
			ShaderParameters.GTAOPreIntegrated3D = GSystemTextures.GTAOPreIntegrated->GetRenderTargetItem().ShaderResourceTexture;
			ShaderParameters.GTAOPreIntegratedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		}
	}

	FGTAOMobile_HorizonSearchIntegral() = default;
	FGTAOMobile_HorizonSearchIntegral(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FGTAOMobile_HorizonSearchIntegralSpatialFilterCS : public FGTAOMobile_HorizonSearchIntegral
{
	using Super = FGTAOMobile_HorizonSearchIntegral;

public:
	// Changing these numbers requires PostProcessAmbientOcclusionMobile.usf to be recompiled.
	// The maximum thread group is 512 on IOS A9 and A10 and the shared memory is 16K
	static const uint32 ThreadGroupSizeX = 32;
	static const uint32 ThreadGroupSizeY = 32;

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_GLOBAL_SHADER(FGTAOMobile_HorizonSearchIntegralSpatialFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FGTAOMobile_HorizonSearchIntegralSpatialFilterCS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGTAOMobile_HorizonSearchIntegral::FParameters, Common)
		SHADER_PARAMETER_EX(FVector4, Power_Intensity_ScreenPixelsToSearch, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<half4>, OutTexture)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<
		Super::FCommonPermutationDomain>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		return Super::ShouldCompilePermutation(Parameters, PermutationVector.Get<Super::FCommonPermutationDomain>());
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("HORIZONSEARCH_INTEGRAL_SPATIALFILTER_COMPUTE_SHADER"), 1u);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
	}

	static FPermutationDomain BuildPermutationVector(int32 LUTTextureType, int32 ShaderQuality)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<Super::FCommonPermutationDomain>(Super::BuildPermutationVector(LUTTextureType, ShaderQuality));
		return PermutationVector;
	}
};

const FIntPoint FGTAOMobile_HorizonSearchIntegralSpatialFilterCS::TexelsPerThreadGroup(ThreadGroupSizeX, ThreadGroupSizeY);

IMPLEMENT_GLOBAL_SHADER(FGTAOMobile_HorizonSearchIntegralSpatialFilterCS, "/Engine/Private/PostProcessAmbientOcclusionMobile.usf", "GTAOHorizonSearchIntegralSpatialFilterCS", SF_Compute);

class FGTAOMobile_HorizonSearchIntegralCS : public FGTAOMobile_HorizonSearchIntegral
{
	using Super = FGTAOMobile_HorizonSearchIntegral;
public:
	// Changing these numbers requires PostProcessAmbientOcclusionMobile.usf to be recompiled.
	// Use smaller thread group for low end devices
	static const uint32 ThreadGroupSizeX = 16;
	static const uint32 ThreadGroupSizeY = 8;

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_GLOBAL_SHADER(FGTAOMobile_HorizonSearchIntegralCS);
	SHADER_USE_PARAMETER_STRUCT(FGTAOMobile_HorizonSearchIntegralCS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGTAOMobile_HorizonSearchIntegral::FParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<half4>, OutTexture)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<
		Super::FCommonPermutationDomain>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		return Super::ShouldCompilePermutation(Parameters, PermutationVector.Get<Super::FCommonPermutationDomain>());
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("HORIZONSEARCH_INTEGRAL_COMPUTE_SHADER"), 1u);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
	}

	static FPermutationDomain BuildPermutationVector(int32 LUTTextureType, int32 ShaderQuality)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<Super::FCommonPermutationDomain>(Super::BuildPermutationVector(LUTTextureType, ShaderQuality));
		return PermutationVector;
	}
};

const FIntPoint FGTAOMobile_HorizonSearchIntegralCS::TexelsPerThreadGroup(ThreadGroupSizeX, ThreadGroupSizeY);

IMPLEMENT_GLOBAL_SHADER(FGTAOMobile_HorizonSearchIntegralCS, "/Engine/Private/PostProcessAmbientOcclusionMobile.usf", "GTAOHorizonSearchIntegralCS", SF_Compute);

class FGTAOMobile_SpatialFilter : public FGlobalShader
{
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_EX(FVector4, ViewRectMin, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_EX(FVector4, BufferSizeAndInvSize, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_EX(FVector4, ViewSizeAndInvSize, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_EX(FVector4, Power_Intensity_ScreenPixelsToSearch, EShaderPrecisionModifier::Half)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AOInputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, AOInputSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		return IsMobileAmbientOcclusionEnabled(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static void SetupShaderParameters(FParameters& ShaderParameters, const FViewInfo& View, const FIntRect& ViewRect, const FIntPoint& BufferSize, FRDGTextureRef HorizonSearchIntegralTexture)
	{
		const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;

		ShaderParameters.ViewRectMin = FVector4(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, 0.0f);
		ShaderParameters.BufferSizeAndInvSize = FVector4(BufferSize.X, BufferSize.Y, 1.0f / BufferSize.X, 1.0f / BufferSize.Y);
		ShaderParameters.ViewSizeAndInvSize = FVector4(ViewRect.Width(), ViewRect.Height(), 1.0f / ViewRect.Width(), 1.0f / ViewRect.Height());
		ShaderParameters.Power_Intensity_ScreenPixelsToSearch = FVector4(Settings.AmbientOcclusionPower * 0.5f, Settings.AmbientOcclusionIntensity, 0.0f, 0.0f);

		ShaderParameters.AOInputTexture = HorizonSearchIntegralTexture;
		ShaderParameters.AOInputSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	FGTAOMobile_SpatialFilter() = default;
	FGTAOMobile_SpatialFilter(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FGTAOMobile_SpatialFilterCS : public FGTAOMobile_SpatialFilter
{
	using Super = FGTAOMobile_SpatialFilter;
public:
	// Changing these numbers requires PostProcessAmbientOcclusionMobile.usf to be recompiled.
	// Use smaller thread group for low end devices
	static const uint32 ThreadGroupSizeX = 16;
	static const uint32 ThreadGroupSizeY = 8;

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_GLOBAL_SHADER(FGTAOMobile_SpatialFilterCS);
	SHADER_USE_PARAMETER_STRUCT(FGTAOMobile_SpatialFilterCS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGTAOMobile_SpatialFilter::FParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<half4>, OutTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return Super::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SPATIALFILTER_COMPUTE_SHADER"), 1u);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
	}
};

const FIntPoint FGTAOMobile_SpatialFilterCS::TexelsPerThreadGroup(ThreadGroupSizeX, ThreadGroupSizeY);

IMPLEMENT_GLOBAL_SHADER(FGTAOMobile_SpatialFilterCS, "/Engine/Private/PostProcessAmbientOcclusionMobile.usf", "GTAOSpatialFilterCS", SF_Compute);

class FGTAOMobile_HorizonSearchIntegralPS : public FGTAOMobile_HorizonSearchIntegral
{
	using Super = FGTAOMobile_HorizonSearchIntegral;
public:

	DECLARE_GLOBAL_SHADER(FGTAOMobile_HorizonSearchIntegralPS);
	SHADER_USE_PARAMETER_STRUCT(FGTAOMobile_HorizonSearchIntegralPS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGTAOMobile_HorizonSearchIntegral::FParameters, Common)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<
		Super::FCommonPermutationDomain>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		return Super::ShouldCompilePermutation(Parameters, PermutationVector.Get<Super::FCommonPermutationDomain>());
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("HORIZONSEARCH_INTEGRAL_PIXEL_SHADER"), 1u);
	}

	static FPermutationDomain BuildPermutationVector(int32 LUTTextureType, int32 ShaderQuality)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<Super::FCommonPermutationDomain>(Super::BuildPermutationVector(LUTTextureType, ShaderQuality));
		return PermutationVector;
	}
};

IMPLEMENT_GLOBAL_SHADER(FGTAOMobile_HorizonSearchIntegralPS, "/Engine/Private/PostProcessAmbientOcclusionMobile.usf", "GTAOHorizonSearchIntegralPS", SF_Pixel);

class FGTAOMobile_SpatialFilterPS : public FGTAOMobile_SpatialFilter
{
	using Super = FGTAOMobile_SpatialFilter;
public:
	DECLARE_GLOBAL_SHADER(FGTAOMobile_SpatialFilterPS);
	SHADER_USE_PARAMETER_STRUCT(FGTAOMobile_SpatialFilterPS, Super);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGTAOMobile_SpatialFilter::FParameters, Common)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return Super::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SPATIALFILTER_PIXEL_SHADER"), 1u);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGTAOMobile_SpatialFilterPS, "/Engine/Private/PostProcessAmbientOcclusionMobile.usf", "GTAOSpatialFilterPS", SF_Pixel);

void FMobileSceneRenderer::InitAmbientOcclusionOutputs(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ)
{
	FPooledRenderTargetDesc SceneDepthZDesc = SceneDepthZ->GetDesc();

	const FIntPoint& BufferSize = SceneDepthZDesc.Extent;

	const uint32 DownsampleFactor = 2;

	FIntPoint Extent = FIntPoint::DivideAndRoundUp(BufferSize, DownsampleFactor);

	const bool bUsePixelShader = CVarMobileAmbientOcclusionShaderType.GetValueOnRenderThread() == 2;

	if (!GAmbientOcclusionMobileOutputs.IsValid() || GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture->GetDesc().Extent != Extent || (bUsePixelShader && GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture->GetDesc().Format != PF_G8) || (!bUsePixelShader && GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture->GetDesc().Format != PF_R8G8B8A8))
	{
		GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture.SafeRelease();

		GRenderTargetPool.FindFreeElement(RHICmdList, FPooledRenderTargetDesc::Create2DDesc(Extent, bUsePixelShader ? PF_G8 : PF_R8G8B8A8, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, false, 1, false), GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture, TEXT("AmbientOcclusionTexture"));
	}
}

void FMobileSceneRenderer::ReleaseAmbientOcclusionOutputs()
{
	GAmbientOcclusionMobileOutputs.Release();
}

void FMobileSceneRenderer::RenderAmbientOcclusion(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ)
{
	checkSlow(GAmbientOcclusionMobileOutputs.IsValid() && SceneDepthZ.IsValid());

	SCOPED_DRAW_EVENT(RHICmdList, AmbientOcclusion);

	FMemMark Mark(FMemStack::Get());
	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneDepthTexture = GraphBuilder.RegisterExternalTexture(SceneDepthZ, TEXT("SceneDepthTexture"));

	FRDGTextureRef AmbientOcclusionTexture = GraphBuilder.RegisterExternalTexture(GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture, TEXT("AmbientOcclusionTexture"));

	RenderAmbientOcclusion(GraphBuilder, SceneDepthTexture, AmbientOcclusionTexture);

	GraphBuilder.Execute();
}

void FMobileSceneRenderer::RenderAmbientOcclusion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, FRDGTextureRef AmbientOcclusionTexture)
{
	static const auto GTAOThicknessBlendCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.GTAO.ThicknessBlend"));
	static const auto GTAOFalloffStartRatioCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.GTAO.FalloffStartRatio"));
	static const auto GTAOFalloffEndCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.GTAO.FalloffEnd"));
	static const auto GTAONumAnglesCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.GTAO.NumAngles"));
	const uint32 DownsampleFactor = 2;

	const int32 MobileGTAOPreIntegratedTextureType = CVarMobileGTAOPreIntegratedTextureType.GetValueOnRenderThread();
	const int32 MobileAmbientOcclusionQuality = CVarMobileAmbientOcclusionQuality.GetValueOnRenderThread();

	FRDGTextureUAVRef AmbientOcclusionTextureUAV = GraphBuilder.CreateUAV(AmbientOcclusionTexture);

	const FIntPoint& DepthBufferSize = SceneDepthTexture->Desc.Extent;
	const FIntPoint& BufferSize = GAmbientOcclusionMobileOutputs.AmbientOcclusionTexture->GetDesc().Extent;

	float FallOffEnd = GTAOFalloffEndCVar ? GTAOFalloffEndCVar->GetValueOnRenderThread() : 200.0f;
	float FallOffStartRatio = GTAOFalloffStartRatioCVar ? FMath::Clamp(GTAOFalloffStartRatioCVar->GetValueOnRenderThread(), 0.0f, 0.999f) : 0.5f;
	float FallOffStart = FallOffEnd * FallOffStartRatio;
	float FallOffStartSq = FallOffStart * FallOffStart;
	float FallOffEndSq = FallOffEnd * FallOffEnd;

	float FallOffScale = 1.0f / (FallOffEndSq - FallOffStartSq);
	float FallOffBias = -FallOffStartSq * FallOffScale;

	FVector4 FallOffStartEndScaleBias(FallOffStart, FallOffEnd, FallOffScale, FallOffBias);

	float ThicknessBlend = GTAOThicknessBlendCVar ? GTAOThicknessBlendCVar->GetValueOnRenderThread() : 0.5f;
	ThicknessBlend = FMath::Clamp(1.0f - (ThicknessBlend*ThicknessBlend), 0.0f, 0.99f);

	float NumAngles = GTAONumAnglesCVar ? FMath::Clamp(GTAONumAnglesCVar->GetValueOnRenderThread(), 1.0f, 16.0f) : 2;
	float SinDeltaAngle, CosDeltaAngle;
	FMath::SinCos(&SinDeltaAngle, &CosDeltaAngle, PI / NumAngles);

	ETextureCreateFlags TextureCreateFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV;
	FRDGTextureRef HorizonSearchIntegralTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(BufferSize, PF_R8G8B8A8, FClearValueBinding::Black, TextureCreateFlags), TEXT("HorizonSearchIntegralTexture"));
	FRDGTextureUAVRef HorizonSearchIntegralTextureUAV = GraphBuilder.CreateUAV(HorizonSearchIntegralTexture);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;

		const FIntRect& ViewRect = FIntRect::DivideAndRoundUp(View.ViewRect, DownsampleFactor);

		FVector4 WorldRadiusAdjSinCosDeltaAngleThickness(FallOffStartEndScaleBias.Y * DepthBufferSize.Y * View.ViewMatrices.GetProjectionMatrix().M[0][0], SinDeltaAngle, CosDeltaAngle, ThicknessBlend);

		if (GetMaxWorkGroupInvocations() >= 1024 && CVarMobileAmbientOcclusionShaderType.GetValueOnRenderThread() == 0)
		{
			FGTAOMobile_HorizonSearchIntegralSpatialFilterCS::FParameters* HorizonSearchIntegralSpatialFilterParameters = GraphBuilder.AllocParameters<FGTAOMobile_HorizonSearchIntegralSpatialFilterCS::FParameters>();
			FGTAOMobile_HorizonSearchIntegral::SetupShaderParameters(HorizonSearchIntegralSpatialFilterParameters->Common, GraphBuilder, View, ViewRect, DepthBufferSize, BufferSize, FallOffStartEndScaleBias, WorldRadiusAdjSinCosDeltaAngleThickness, SceneDepthTexture);
			
			HorizonSearchIntegralSpatialFilterParameters->Power_Intensity_ScreenPixelsToSearch = FVector4(Settings.AmbientOcclusionPower * 0.5f, Settings.AmbientOcclusionIntensity, 0.0f, 0.0f);

			HorizonSearchIntegralSpatialFilterParameters->OutTexture = AmbientOcclusionTextureUAV;

			auto ComputeShaderPermutationVector = FGTAOMobile_HorizonSearchIntegralSpatialFilterCS::BuildPermutationVector(MobileGTAOPreIntegratedTextureType, MobileAmbientOcclusionQuality - 1);
			TShaderMapRef<FGTAOMobile_HorizonSearchIntegralSpatialFilterCS> ComputeShader(View.ShaderMap, ComputeShaderPermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("AmbientOcclusion_HorizonSearchIntegralSpatialFilter %dx%d (CS)", ViewRect.Width(), ViewRect.Height()),
				ComputeShader,
				HorizonSearchIntegralSpatialFilterParameters,
				FComputeShaderUtils::GetGroupCount(ViewRect.Size(), FGTAOMobile_HorizonSearchIntegralSpatialFilterCS::TexelsPerThreadGroup));
		}
		else if (CVarMobileAmbientOcclusionShaderType.GetValueOnRenderThread() != 1)
		{
			TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);

			FScreenPassRenderTarget HorizonSearchIntegralRT(HorizonSearchIntegralTexture, ViewRect, ERenderTargetLoadAction::EClear);

			FGTAOMobile_HorizonSearchIntegralPS::FParameters* HorizonSearchIntegralParameters = GraphBuilder.AllocParameters<FGTAOMobile_HorizonSearchIntegralPS::FParameters>();
			FGTAOMobile_HorizonSearchIntegral::SetupShaderParameters(HorizonSearchIntegralParameters->Common, GraphBuilder, View, ViewRect, DepthBufferSize, BufferSize, FallOffStartEndScaleBias, WorldRadiusAdjSinCosDeltaAngleThickness, SceneDepthTexture);

			HorizonSearchIntegralParameters->RenderTargets[0] = HorizonSearchIntegralRT.GetRenderTargetBinding();

			auto HorizonSearchIntegralShaderPermutationVector = FGTAOMobile_HorizonSearchIntegralPS::BuildPermutationVector(MobileGTAOPreIntegratedTextureType, MobileAmbientOcclusionQuality - 1);
			TShaderMapRef<FGTAOMobile_HorizonSearchIntegralPS> HorizonSearchIntegralShader(View.ShaderMap, HorizonSearchIntegralShaderPermutationVector);

			ClearUnusedGraphResources(HorizonSearchIntegralShader, HorizonSearchIntegralParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("AmbientOcclusion_HorizonSearchIntegral %dx%d (PS)", ViewRect.Width(), ViewRect.Height()),
				HorizonSearchIntegralParameters,
				ERDGPassFlags::Raster,
				[VertexShader, HorizonSearchIntegralShader, HorizonSearchIntegralParameters, ViewRect, BufferSize](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = HorizonSearchIntegralShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, HorizonSearchIntegralShader, HorizonSearchIntegralShader.GetPixelShader(), *HorizonSearchIntegralParameters);

				DrawRectangle(
					RHICmdList,
					0, 0,
					BufferSize.X, BufferSize.Y,
					ViewRect.Min.X, ViewRect.Min.Y,
					ViewRect.Width(), ViewRect.Height(),
					BufferSize,
					BufferSize,
					VertexShader,
					EDRF_UseTriangleOptimization);
			});

			FScreenPassRenderTarget AmbientOcclusionRT(AmbientOcclusionTexture, ViewRect, ERenderTargetLoadAction::EClear);

			FGTAOMobile_SpatialFilterPS::FParameters* SpatialFilterParameters = GraphBuilder.AllocParameters<FGTAOMobile_SpatialFilterPS::FParameters>();
			FGTAOMobile_SpatialFilter::SetupShaderParameters(SpatialFilterParameters->Common, View, ViewRect, BufferSize, HorizonSearchIntegralTexture);
			SpatialFilterParameters->RenderTargets[0] = AmbientOcclusionRT.GetRenderTargetBinding();

			TShaderMapRef<FGTAOMobile_SpatialFilterPS> SpatialFilterShader(View.ShaderMap);

			ClearUnusedGraphResources(SpatialFilterShader, SpatialFilterParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("AmbientOcclusion_SpatialFilter %dx%d (PS)", ViewRect.Width(), ViewRect.Height()),
				SpatialFilterParameters,
				ERDGPassFlags::Raster,
				[VertexShader, SpatialFilterShader, SpatialFilterParameters, ViewRect, BufferSize](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = SpatialFilterShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, SpatialFilterShader, SpatialFilterShader.GetPixelShader(), *SpatialFilterParameters);

				DrawRectangle(
					RHICmdList,
					0, 0,
					BufferSize.X, BufferSize.Y,
					ViewRect.Min.X, ViewRect.Min.Y,
					ViewRect.Width(), ViewRect.Height(),
					BufferSize,
					BufferSize,
					VertexShader,
					EDRF_UseTriangleOptimization);
			});
		}
		else
		{
			FGTAOMobile_HorizonSearchIntegralCS::FParameters* HorizonSearchIntegralParameters = GraphBuilder.AllocParameters<FGTAOMobile_HorizonSearchIntegralCS::FParameters>();
			FGTAOMobile_HorizonSearchIntegral::SetupShaderParameters(HorizonSearchIntegralParameters->Common, GraphBuilder, View, ViewRect, DepthBufferSize, BufferSize, FallOffStartEndScaleBias, WorldRadiusAdjSinCosDeltaAngleThickness, SceneDepthTexture);

			HorizonSearchIntegralParameters->OutTexture = HorizonSearchIntegralTextureUAV;

			auto HorizonSearchIntegralShaderPermutationVector = FGTAOMobile_HorizonSearchIntegralCS::BuildPermutationVector(MobileGTAOPreIntegratedTextureType, MobileAmbientOcclusionQuality - 1);
			TShaderMapRef<FGTAOMobile_HorizonSearchIntegralCS> HorizonSearchIntegralShader(View.ShaderMap, HorizonSearchIntegralShaderPermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("AmbientOcclusion_HorizonSearchIntegral %dx%d (CS)", ViewRect.Width(), ViewRect.Height()),
				HorizonSearchIntegralShader,
				HorizonSearchIntegralParameters,
				FComputeShaderUtils::GetGroupCount(ViewRect.Size(), FGTAOMobile_HorizonSearchIntegralCS::TexelsPerThreadGroup));

			FGTAOMobile_SpatialFilterCS::FParameters* SpatialFilterParameters = GraphBuilder.AllocParameters<FGTAOMobile_SpatialFilterCS::FParameters>();
			FGTAOMobile_SpatialFilter::SetupShaderParameters(SpatialFilterParameters->Common, View, ViewRect, BufferSize, HorizonSearchIntegralTexture);
			SpatialFilterParameters->OutTexture = AmbientOcclusionTextureUAV;

			TShaderMapRef<FGTAOMobile_SpatialFilterCS> SpatialFilterShader(View.ShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("AmbientOcclusion_SpatialFilter %dx%d (CS)", ViewRect.Width(), ViewRect.Height()),
				SpatialFilterShader,
				SpatialFilterParameters,
				FComputeShaderUtils::GetGroupCount(ViewRect.Size(), FGTAOMobile_SpatialFilterCS::TexelsPerThreadGroup));
		}
	}
}