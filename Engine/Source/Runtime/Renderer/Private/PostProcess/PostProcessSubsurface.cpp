// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessSubsurface.cpp: Screenspace subsurface scattering implementation.
=============================================================================*/

#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/SceneRenderTargets.h"
#include "Engine/SubsurfaceProfile.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"

ENGINE_API IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);

namespace
{
	TAutoConsoleVariable<int32> CVarSubsurfaceScattering(
		TEXT("r.SubsurfaceScattering"),
		1,
		TEXT(" 0: disabled\n")
		TEXT(" 1: enabled (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarSSSScale(
		TEXT("r.SSS.Scale"),
		1.0f,
		TEXT("Affects the Screen space subsurface scattering pass")
		TEXT("(use shadingmodel SubsurfaceProfile, get near to the object as the default)\n")
		TEXT("is human skin which only scatters about 1.2cm)\n")
		TEXT(" 0: off (if there is no object on the screen using this pass it should automatically disable the post process pass)\n")
		TEXT("<1: scale scatter radius down (for testing)\n")
		TEXT(" 1: use given radius form the Subsurface scattering asset (default)\n")
		TEXT(">1: scale scatter radius up (for testing)"),
		ECVF_Scalability | ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarSSSHalfRes(
		TEXT("r.SSS.HalfRes"),
		1,
		TEXT(" 0: full quality (not optimized, as reference)\n")
		TEXT(" 1: parts of the algorithm runs in half resolution which is lower quality but faster (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSQuality(
		TEXT("r.SSS.Quality"),
		0,
		TEXT("Defines the quality of the recombine pass when using the SubsurfaceScatteringProfile shading model\n")
		TEXT(" 0: low (faster, default)\n")
		TEXT(" 1: high (sharper details but slower)\n")
		TEXT("-1: auto, 1 if TemporalAA is disabled (without TemporalAA the quality is more noticable)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSFilter(
		TEXT("r.SSS.Filter"),
		1,
		TEXT("Defines the filter method for Screenspace Subsurface Scattering feature.\n")
		TEXT(" 0: point filter (useful for testing, could be cleaner)\n")
		TEXT(" 1: bilinear filter"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSSampleSet(
		TEXT("r.SSS.SampleSet"),
		2,
		TEXT("Defines how many samples we use for Screenspace Subsurface Scattering feature.\n")
		TEXT(" 0: lowest quality (6*2+1)\n")
		TEXT(" 1: medium quality (9*2+1)\n")
		TEXT(" 2: high quality (13*2+1) (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSCheckerboard(
		TEXT("r.SSS.Checkerboard"),
		2,
		TEXT("Enables or disables checkerboard rendering for subsurface profile rendering.\n")
		TEXT("This is necessary if SceneColor does not include a floating point alpha channel (e.g 32-bit formats)\n")
		TEXT(" 0: Disabled (high quality) \n")
		TEXT(" 1: Enabled (low quality). Surface lighting will be at reduced resolution.\n")
		TEXT(" 2: Automatic. Non-checkerboard lighting will be applied if we have a suitable rendertarget format\n"),
		ECVF_RenderThreadSafe);
}

enum class ESubsurfaceMode : uint32
{
	// Performs a full resolution scattering filter.
	FullRes,

	// Performs a half resolution scattering filter.
	HalfRes,

	// Reconstructs lighting, but does not perform scattering.
	Bypass,

	MAX
};

// Returns the [0, N] clamped value of the 'r.SSS.Scale' CVar.
float GetSubsurfaceRadiusScale()
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SSS.Scale"));
	check(CVar);

	return FMath::Max(0.0f, CVar->GetValueOnRenderThread());
}

// Returns the current subsurface mode required by the current view.
ESubsurfaceMode GetSubsurfaceModeForView(const FViewInfo& View)
{
	const float Radius = GetSubsurfaceRadiusScale();
	const bool bShowSubsurfaceScattering = Radius > 0 && View.Family->EngineShowFlags.SubsurfaceScattering;

	if (bShowSubsurfaceScattering)
	{
		const bool bHalfRes = CVarSSSHalfRes.GetValueOnRenderThread() != 0;
		if (bHalfRes)
		{
			return ESubsurfaceMode::HalfRes;
		}
		else
		{
			return ESubsurfaceMode::FullRes;
		}
	}
	else
	{
		return ESubsurfaceMode::Bypass;
	}
}

// Returns the SS profile texture with a black fallback texture if none exists yet.
FTextureRHIRef GetSubsurfaceProfileTexture(FRHICommandListImmediate& RHICmdList)
{
	const IPooledRenderTarget* ProfileTextureTarget = GetSubsufaceProfileTexture_RT(RHICmdList);

	if (!ProfileTextureTarget)
	{
		// No subsurface profile was used yet
		ProfileTextureTarget = GSystemTextures.BlackDummy;
	}

	return ProfileTextureTarget->GetRenderTargetItem().ShaderResourceTexture;
}

// Set of common shader parameters shared by all subsurface shaders.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceParameters, )
	SHADER_PARAMETER(FVector4, SubsurfaceParams)
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassCommonParameters, ScreenPassCommonParameters)
	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceParameters GetSubsurfaceCommonParameters(FRHICommandListImmediate& RHICmdList, FScreenPassContextRef Context)
{
	const float DistanceToProjectionWindow = Context->View.ViewMatrices.GetProjectionMatrix().M[0][0];
	const float SSSScaleZ = DistanceToProjectionWindow * GetSubsurfaceRadiusScale();
	const float SSSScaleX = SSSScaleZ / SUBSURFACE_KERNEL_SIZE * 0.5f;

	FSubsurfaceParameters Parameters;
	Parameters.ScreenPassCommonParameters = Context->ScreenPassCommonParameters;
	Parameters.SubsurfaceParams = FVector4(SSSScaleX, SSSScaleZ, 0, 0);
	Parameters.SSProfilesTexture = GetSubsurfaceProfileTexture(RHICmdList);
	return Parameters;
}

// A shader parameter struct for a single subsurface input texture.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceInput GetSubsurfaceInput(const FScreenPassTexture& Texture)
{
	FSubsurfaceInput Input;
	Input.Texture = Texture.GetRDGTexture();
	Input.Viewport = Texture.GetViewportParameters();
	return Input;
}

// Base class for a subsurface shader.
class FSubsurfaceShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
	}

	FSubsurfaceShader() = default;
	FSubsurfaceShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

// Encapsulates the post processing subsurface scattering pixel shader.
class FSubsurfaceVisualizePS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceVisualizePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceVisualizePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceVisualizePS, "/Engine/Private/PostProcessSubsurface.usf", "VisualizePS", SF_Pixel);

// Encapsulates the post processing subsurface scattering pixel shader.
class FSubsurfaceSetupPS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceSetupPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceSetupPS, FSubsurfaceShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FDimensionHalfRes : SHADER_PERMUTATION_BOOL("SUBSURFACE_HALF_RES");
	class FDimensionCheckerboard : SHADER_PERMUTATION_BOOL("SUBSURFACE_PROFILE_CHECKERBOARD");
	using FPermutationDomain = TShaderPermutationDomain<FDimensionHalfRes, FDimensionCheckerboard>;
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceSetupPS, "/Engine/Private/PostProcessSubsurface.usf", "SetupPS", SF_Pixel);

// Shader for the SSS separable blur.
class FSubsurfacePS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfacePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfacePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	// Direction of the 1D separable filter.
	enum class EDirection : uint32
	{
		Horizontal,
		Vertical,
		MAX
	};

	// Controls the quality (number of samples) of the blur kernel.
	enum class EQuality : uint32
	{
		Low,
		Medium,
		High,
		MAX
	};

	class FDimensionDirection : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_DIRECTION", EDirection);
	class FDimensionQuality : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_QUALITY", EQuality);
	using FPermutationDomain = TShaderPermutationDomain<FDimensionDirection, FDimensionQuality>;

	// Returns the sampler state based on the requested SSS filter CVar setting.
	static FSamplerStateRHIParamRef GetSamplerState()
	{
		if (CVarSSSFilter.GetValueOnRenderThread())
		{
			return TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();
		}
		else
		{
			return TStaticSamplerState<SF_Point, AM_Border, AM_Border, AM_Border>::GetRHI();
		}
	}

	// Returns the SSS quality level requested by the SSS SampleSet CVar setting.
	static EQuality GetQuality()
	{
		return static_cast<FSubsurfacePS::EQuality>(
			FMath::Clamp(
				CVarSSSSampleSet.GetValueOnRenderThread(),
				static_cast<int32>(FSubsurfacePS::EQuality::Low),
				static_cast<int32>(FSubsurfacePS::EQuality::High)));
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfacePS, "/Engine/Private/PostProcessSubsurface.usf", "MainPS", SF_Pixel);

// Encapsulates the post processing subsurface recombine pixel shader.
class FSubsurfaceRecombinePS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceRecombinePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceRecombinePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput1)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler1)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT();

	// Controls the quality of lighting reconstruction.
	enum class EQuality : uint32
	{
		Low,
		High,
		MAX
	};

	class FDimensionMode : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_RECOMBINE_MODE", ESubsurfaceMode);
	class FDimensionQuality : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_RECOMBINE_QUALITY", EQuality);
	class FDimensionCheckerboard : SHADER_PERMUTATION_BOOL("SUBSURFACE_PROFILE_CHECKERBOARD");
	using FPermutationDomain = TShaderPermutationDomain<FDimensionMode, FDimensionQuality, FDimensionCheckerboard>;

	// Returns the Recombine quality level requested by the SSS Quality CVar setting.
	static EQuality GetQuality(const FViewInfo& View)
	{
		const uint32 QualityCVar = CVarSSSQuality.GetValueOnRenderThread();

		// Quality is forced to high when the CVar is set to 'auto' and TAA is NOT enabled.
		// TAA improves quality through temporal filtering, making it less necessary to use
		// high quality mode.
		const bool bUseHighQuality = (QualityCVar == -1 && View.AntiAliasingMethod != AAM_TemporalAA);

		if (QualityCVar == 1 || bUseHighQuality)
		{
			return EQuality::High;
		}
		else
		{
			return EQuality::Low;
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceRecombinePS, "/Engine/Private/PostProcessSubsurface.usf", "SubsurfaceRecombinePS", SF_Pixel);

// Encapsulates a simple copy pixel shader.
class FSubsurfaceViewportCopyPS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceViewportCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceViewportCopyPS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceViewportCopyPS, "/Engine/Private/PostProcessSubsurface.usf", "SubsurfaceViewportCopyPS", SF_Pixel);

bool IsSubsurfaceEnabled()
{
	const bool bEnabled = CVarSubsurfaceScattering.GetValueOnAnyThread() != 0;
	const bool bHasScale = CVarSSSScale.GetValueOnAnyThread() > 0.0f;
	return (bEnabled && bHasScale);
}

bool IsSubsurfaceRequiredForView(const FViewInfo& View)
{
	const bool bSimpleDynamicLighting = IsAnyForwardShadingEnabled(View.GetShaderPlatform());
	const bool bSubsurfaceEnabled = IsSubsurfaceEnabled();
	const bool bViewHasSubsurfaceMaterials = ((View.ShadingModelMaskInView & GetUseSubsurfaceProfileShadingModelMask()) != 0);
	return (bSubsurfaceEnabled && bViewHasSubsurfaceMaterials && !bSimpleDynamicLighting);
}

bool IsSubsurfaceCheckerboardFormat(EPixelFormat SceneColorFormat)
{
	int CVarValue = CVarSSSCheckerboard.GetValueOnRenderThread();
	if (CVarValue == 0)
	{
		return false;
	}
	else if (CVarValue == 1)
	{
		return true;
	}
	else if (CVarValue == 2)
	{
		switch (SceneColorFormat)
		{
		case PF_A32B32G32R32F:
		case PF_FloatRGBA:
			return false;
		default:
			return true;
		}
	}
	return true;
}

FScreenPassTexture ComputeSubsurface(
	FRDGBuilder& GraphBuilder,
	FScreenPassContextRef Context,
	const FScreenPassTexture& SceneTexture)
{
	check(Context);
	check(SceneTexture.IsValid());

	const FRDGTextureDesc& SceneTextureDesc = SceneTexture.GetRDGTexture()->Desc;

	const ESubsurfaceMode SubsurfaceMode = GetSubsurfaceModeForView(Context->View);

	const bool bHalfRes = (SubsurfaceMode == ESubsurfaceMode::HalfRes);

	const bool bCheckerboard = IsSubsurfaceCheckerboardFormat(SceneTextureDesc.Format);

	/**
	 * All intermediate passes within the screen-space subsurface effect can operate at half or full resolution,
	 * depending on the subsurface mode. The values are precomputed and shared among all intermediate textures.
	 */
	FScreenPassTextureViewport IntermediateViewport = SceneTexture.GetViewport();

	if (bHalfRes)
	{
		IntermediateViewport = FScreenPassTextureViewport::CreateDownscaled(IntermediateViewport, 2);
	}

	const FRDGTextureDesc IntermediateTextureDescriptor = FRDGTextureDesc::Create2DDesc(
		IntermediateViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource,
		false);
	
	const FSubsurfaceParameters SubsurfaceCommonParameters = GetSubsurfaceCommonParameters(GraphBuilder.RHICmdList, Context);

	FScreenPassTexture SetupTexture = SceneTexture;
	FScreenPassTexture SubsurfaceTextureX;
	FScreenPassTexture SubsurfaceTextureY;
	FScreenPassTexture RecombineTexture;

	FSamplerStateRHIParamRef PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FSamplerStateRHIParamRef BilinearBorderSampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();

	/**
	 * When in bypass mode, the setup and convolution passes are skipped, but lighting
	 * reconstruction is still performed in the recombine pass.
	 */
	if (SubsurfaceMode != ESubsurfaceMode::Bypass)
	{
		SetupTexture = FScreenPassTexture::Create(
			GraphBuilder.CreateTexture(IntermediateTextureDescriptor, TEXT("SubsurfaceSetupTexture")),
			IntermediateViewport.Rect);

		// Setup pass outputs the diffuse scene color and depth in preparation for the scatter passes.
		{
			const FScreenPassTexture& TextureInput = SceneTexture;
			const FScreenPassTexture& TextureOutput = SetupTexture;

			FSubsurfaceSetupPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceSetupPS::FParameters>();
			PassParameters->Subsurface = SubsurfaceCommonParameters;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(TextureOutput.GetRDGTexture(), ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
			PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput);
			PassParameters->SubsurfaceSampler0 = PointClampSampler;

			FSubsurfaceSetupPS::FPermutationDomain PixelShaderPermutationVector;
			PixelShaderPermutationVector.Set<FSubsurfaceSetupPS::FDimensionHalfRes>(bHalfRes);
			PixelShaderPermutationVector.Set<FSubsurfaceSetupPS::FDimensionCheckerboard>(bCheckerboard);
			TShaderMapRef<FSubsurfaceSetupPS> PixelShader(Context->ShaderMap, PixelShaderPermutationVector);

			AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceSetup"), Context, TextureOutput.GetViewport(), TextureInput.GetViewport(), *PixelShader, PassParameters);
		}

		SubsurfaceTextureX = FScreenPassTexture::Create(
			GraphBuilder.CreateTexture(IntermediateTextureDescriptor, TEXT("SubsurfaceTextureX")),
			IntermediateViewport.Rect);

		SubsurfaceTextureY = FScreenPassTexture::Create(
			GraphBuilder.CreateTexture(IntermediateTextureDescriptor, TEXT("SubsurfaceTextureY")),
			IntermediateViewport.Rect);

		FSamplerStateRHIParamRef SubsurfaceSamplerState = FSubsurfacePS::GetSamplerState();
		const FSubsurfacePS::EQuality SubsurfaceQuality = FSubsurfacePS::GetQuality();

		struct FSubsurfacePassInfo
		{
			FSubsurfacePassInfo(const TCHAR* InName, const FScreenPassTexture& InInput, const FScreenPassTexture& InOutput)
				: Name(InName)
				, Input(InInput)
				, Output(InOutput)
			{}

			const TCHAR* Name;
			const FScreenPassTexture& Input;
			const FScreenPassTexture& Output;
		};

		const FSubsurfacePassInfo SubsurfacePassInfoByDirection[] =
		{
			{ TEXT("SubsurfaceX"), SetupTexture, SubsurfaceTextureX },
			{ TEXT("SubsurfaceY"), SubsurfaceTextureX, SubsurfaceTextureY },
		};

		// Horizontal / Vertical scattering passes using a separable filter.
		for (uint32 DirectionIndex = 0; DirectionIndex < static_cast<uint32>(FSubsurfacePS::EDirection::MAX); ++DirectionIndex)
		{
			const auto Direction = static_cast<FSubsurfacePS::EDirection>(DirectionIndex);

			const FSubsurfacePassInfo& PassInfo = SubsurfacePassInfoByDirection[DirectionIndex];
			const FScreenPassTexture& TextureInput = PassInfo.Input;
			const FScreenPassTexture& TextureOutput = PassInfo.Output;

			FSubsurfacePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfacePS::FParameters>();
			PassParameters->Subsurface = SubsurfaceCommonParameters;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(TextureOutput.GetRDGTexture(), ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
			PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput);
			PassParameters->SubsurfaceSampler0 = SubsurfaceSamplerState;

			FSubsurfacePS::FPermutationDomain PixelShaderPermutationVector;
			PixelShaderPermutationVector.Set<FSubsurfacePS::FDimensionDirection>(Direction);
			PixelShaderPermutationVector.Set<FSubsurfacePS::FDimensionQuality>(SubsurfaceQuality);
			TShaderMapRef<FSubsurfacePS> PixelShader(Context->ShaderMap, PixelShaderPermutationVector);

			AddDrawScreenPass(GraphBuilder, FRDGEventName(PassInfo.Name), Context, TextureOutput.GetViewport(), TextureInput.GetViewport(), *PixelShader, PassParameters);
		}
	}

	RecombineTexture = FScreenPassTexture::Create(
		GraphBuilder.CreateTexture(SceneTextureDesc, TEXT("SubsurfaceRecombine")),
		SceneTexture.GetViewport().Rect);

	// If multiple views exist (e.g. VR) we need to copy all other views from the scene texture
	// to the recombine target so we don't lose them.
	if (Context->ViewFamily.Views.Num())
	{
		const FScreenPassTexture& TextureInput = SceneTexture;
		const FScreenPassTexture& TextureOutput = RecombineTexture;

		FSubsurfaceViewportCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceViewportCopyPS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(TextureOutput.GetRDGTexture(), ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
		PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput);
		PassParameters->SubsurfaceSampler0 = PointClampSampler;

		TShaderMapRef<FSubsurfaceViewportCopyPS> PixelShader(Context->ShaderMap);

		const FIntPoint InputTextureSize = TextureInput.GetViewport().Extent;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SubsurfaceViewportCopy"),
			PassParameters,
			ERenderGraphPassFlags::None,
			[Context, PixelShader, InputTextureSize, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			const FSceneViewFamily& ViewFamily = Context->ViewFamily;

			for (uint32 ViewId = 0, ViewCount = ViewFamily.Views.Num(); ViewId < ViewCount; ++ViewId)
			{
				const FViewInfo* LocalView = static_cast<const FViewInfo*>(ViewFamily.Views[ViewId]);

				// Skip the view we are currently processing.
				if (LocalView != &Context->View)
				{
					const FIntRect Rect = LocalView->ViewRect;

					DrawScreenPass(RHICmdList, Context, Rect, Rect, InputTextureSize, *PixelShader, *PassParameters);
				}
			}
		});
	}

	// Recombines scattering result with scene color.
	{
		const FScreenPassTexture& TextureInput = SceneTexture;
		const FScreenPassTexture& TextureOutput = RecombineTexture;

		FSubsurfaceRecombinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceRecombinePS::FParameters>();
		PassParameters->Subsurface = SubsurfaceCommonParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(TextureOutput.GetRDGTexture(), ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore);
		PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput);
		PassParameters->SubsurfaceSampler0 = BilinearBorderSampler;

		// Scattering output target is only used when scattering is enabled.
		if (SubsurfaceMode != ESubsurfaceMode::Bypass)
		{
			PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(SubsurfaceTextureY);
			PassParameters->SubsurfaceSampler1 = BilinearBorderSampler;
		}

		const FSubsurfaceRecombinePS::EQuality RecombineQuality = FSubsurfaceRecombinePS::GetQuality(Context->View);

		FSubsurfaceRecombinePS::FPermutationDomain PixelShaderPermutationVector;
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionMode>(SubsurfaceMode);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionQuality>(RecombineQuality);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionCheckerboard>(bCheckerboard);
		TShaderMapRef<FSubsurfaceRecombinePS> PixelShader(Context->ShaderMap, PixelShaderPermutationVector);

		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceRecombine"), Context, TextureOutput.GetViewport(), TextureInput.GetViewport(), *PixelShader, PassParameters);
	}

	return RecombineTexture;
}

FScreenPassTexture VisualizeSubsurface(
	FRDGBuilder& GraphBuilder,
	FScreenPassContextRef Context,
	const FScreenPassTexture& SceneTexture)
{
	check(Context);
	check(SceneTexture.IsValid());

	const FScreenPassTexture& TextureInput = SceneTexture;
	const FScreenPassTexture TextureOutput = FScreenPassTexture::Create(
		GraphBuilder.CreateTexture(TextureInput.GetRDGTexture()->Desc, TEXT("SubsurfaceVisualize")),
		TextureInput.GetViewport().Rect);

	FSubsurfaceVisualizePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceVisualizePS::FParameters>();
	PassParameters->Subsurface = GetSubsurfaceCommonParameters(GraphBuilder.RHICmdList, Context);
	PassParameters->RenderTargets[0] = FRenderTargetBinding(TextureOutput.GetRDGTexture(), ERenderTargetLoadAction::EClear, ERenderTargetStoreAction::EStore);
	PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput);
	PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->MiniFontTexture = GetMiniFontTexture();

	TShaderMapRef<FSubsurfaceVisualizePS> PixelShader(Context->ShaderMap);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SubsurfaceVisualize"),
		PassParameters,
		ERenderGraphPassFlags::None,
		[Context, TextureInput, TextureOutput, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
	{
		DrawScreenPass(RHICmdList, Context, TextureOutput.GetViewport(), TextureInput.GetViewport(), *PixelShader, *PassParameters);

		// Draw debug text
		{
			const FSceneViewFamily& ViewFamily = Context->ViewFamily;
			FRenderTargetTemp TempRenderTarget(static_cast<FTexture2DRHIParamRef>(TextureOutput.GetRHITexture()), TextureOutput.GetViewport().Extent);
			FCanvas Canvas(&TempRenderTarget, nullptr, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, Context->View.GetFeatureLevel());

			float X = 30;
			float Y = 28;
			const float YStep = 14;

			FString Line = FString::Printf(TEXT("Visualize Screen Space Subsurface Scattering"));
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

			Y += YStep;

			uint32 Index = 0;
			while (GSubsurfaceProfileTextureObject.GetEntryString(Index++, Line))
			{
				Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
			}

			const bool bFlush = false;
			const bool bInsideRenderPass = true;
			Canvas.Flush_RenderThread(RHICmdList, bFlush, bInsideRenderPass);
		}
	});

	return TextureOutput;
}

FSubsurfaceVisualizeCompositePass::FSubsurfaceVisualizeCompositePass(FRHICommandList& RHICmdList)
{
	// we need the GBuffer, we release it Process()
	FSceneRenderTargets::Get(RHICmdList).AdjustGBufferRefCount(RHICmdList, 1);
}

void FSubsurfaceVisualizeCompositePass::Process(FRenderingCompositePassContext& CompositePassContext)
{
	FRHICommandListImmediate& RHICmdList = CompositePassContext.RHICmdList;

	const FViewInfo& View = CompositePassContext.View;

	FRDGBuilder GraphBuilder(RHICmdList);

	FScreenPassContext* Context = GraphBuilder.AllocObject<FScreenPassContext>(RHICmdList, CompositePassContext.View);

	const FScreenPassTexture TextureInput = FScreenPassTexture::Create(
		CreateRDGTextureForInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"), eFC_0000),
		View.ViewRect);

	const FScreenPassTexture TextureOutput = VisualizeSubsurface(GraphBuilder, Context, TextureInput);

	ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, TextureOutput.GetRDGTexture());

	TRefCountPtr<IPooledRenderTarget> OutputTarget;
	GraphBuilder.QueueTextureExtraction(TextureOutput.GetRDGTexture(), &OutputTarget);

	GraphBuilder.Execute();

	check(OutputTarget);

	RHICmdList.CopyToResolveTarget(
		OutputTarget->GetRenderTargetItem().TargetableTexture,
		OutputTarget->GetRenderTargetItem().ShaderResourceTexture,
		FResolveParams());

	FSceneRenderTargets::Get(RHICmdList).AdjustGBufferRefCount(RHICmdList, -1);
}

FPooledRenderTargetDesc FSubsurfaceVisualizeCompositePass::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = FSceneRenderTargets::Get_FrameConstantsOnly().GetSceneColor()->GetDesc();
	Ret.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
	Ret.Reset();
	Ret.DebugName = TEXT("SubsurfaceVisualize");
	Ret.Format = PF_FloatRGBA;
	return Ret;
}