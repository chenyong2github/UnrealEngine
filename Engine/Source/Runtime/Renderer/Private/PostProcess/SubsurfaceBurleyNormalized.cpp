
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SubsurfaceBurleyNormalized.cpp: Screenspace Burley subsurface scattering implementation.
=============================================================================*/

#include "PostProcess/SubsurfaceBurleyNormalized.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/SubsurfaceCommon.h"
#include "Engine/SubsurfaceProfile.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSSSBurleyPassType(
		TEXT("r.SSS.Burley.PassType"),
		0,
		TEXT("Select pass type to use.\n")
		TEXT(" 0: Multipath for performance\n")
		TEXT(" 1: Single pass for quality"),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

	TAutoConsoleVariable<int32> CVarSSSBurleyUpdateParameter(
		TEXT("r.SSS.Burley.AlwaysUpdateParametersFromSeparable"),
		1,
		TEXT("0: Will not update parameters when the program loads.")
		TEXT("1: Always update from the separable when the program loads. (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);
}

enum class ESubsurfaceBurleyPassType : uint32
{
	// Performs setup -> subsurface -> recombine three pass
	Multiple,

	// Performs a single pass
	Single,

	MAX
};

// Return the current subsurface pass type
ESubsurfaceBurleyPassType GetSuburfaceBurleyPassType()
{
	const auto PassType = CVarSSSBurleyPassType.GetValueOnRenderThread();
	if (PassType == 0)
	{
		return ESubsurfaceBurleyPassType::Multiple;
	}
	else
	{
		return ESubsurfaceBurleyPassType::Single;
	}
}

// Set of common shader parameters shared by all subsurface shaders.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyParameters, )
	SHADER_PARAMETER(FVector4, SubsurfaceParams)
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_SAMPLER(SamplerState, BilinearTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceBurleyParameters GetSubsurfaceBurleyCommonParameters(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const float DistanceToProjectionWindow = View.ViewMatrices.GetProjectionMatrix().M[0][0];
	const float SSSScaleZ = DistanceToProjectionWindow * GetSubsurfaceRadiusScale();
	const float SSSScaleX = SSSScaleZ / SUBSURFACE_KERNEL_SIZE * 0.5f;

	FSubsurfaceBurleyParameters Parameters;
	Parameters.SubsurfaceParams = FVector4(SSSScaleX, SSSScaleZ, 0, 0);
	Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters.SceneUniformBuffer = CreateSceneTextureUniformBuffer(
		SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, EUniformBufferUsage::UniformBuffer_SingleFrame);
	Parameters.BilinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	Parameters.SSProfilesTexture = GetSubsurfaceProfileTexture(RHICmdList);
	return Parameters;
}

// A shader parameter struct for a single subsurface input texture.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceBurleyInput GetSubsurfaceBurleyInput(FRDGTextureRef Texture, const FScreenPassTextureViewportParameters& ViewportParameters)
{
	FSubsurfaceBurleyInput Input;
	Input.Texture = Texture;
	Input.Viewport = ViewportParameters;
	return Input;
}

// Encapsulates the post processing subsurface scattering pixel shader.
class FSubsurfaceBurleySetupPS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceBurleySetupPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceBurleySetupPS, FSubsurfaceShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceBurleyParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, SubsurfaceInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FDimensionHalfRes : SHADER_PERMUTATION_BOOL("SUBSURFACE_HALF_RES");
	class FDimensionCheckerboard : SHADER_PERMUTATION_BOOL("SUBSURFACE_PROFILE_CHECKERBOARD");
	using FPermutationDomain = TShaderPermutationDomain<FDimensionHalfRes, FDimensionCheckerboard>;
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceBurleySetupPS, "/Engine/Private/SubsurfaceBurleyNormalized.usf", "SetupPS", SF_Pixel);

// Shader for the SSS separable blur.
class FSubsurfaceBurleyPS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceBurleyPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceBurleyPS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceBurleyParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, SubsurfaceInput0)
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

	enum class ESubsurfacePass : uint32
	{
		BURLEY,		// Horizontal
		VARIANCE,	// Vertical
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

	class FSubsurfacePassFunction : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_PASS", ESubsurfacePass);
	class FDimensionQuality : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_QUALITY", EQuality);
	using FPermutationDomain = TShaderPermutationDomain<FSubsurfacePassFunction, FDimensionQuality>;

	// Returns the sampler state based on the requested SSS filter CVar setting.
	static FRHISamplerState* GetSamplerState()
	{
		if (GetSSSFilter())
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
		return static_cast<FSubsurfaceBurleyPS::EQuality>(
			FMath::Clamp(
				GetSSSSampleSet(),
				static_cast<int32>(FSubsurfaceBurleyPS::EQuality::Low),
				static_cast<int32>(FSubsurfaceBurleyPS::EQuality::High)));
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceBurleyPS, "/Engine/Private/SubsurfaceBurleyNormalized.usf", "MainPS", SF_Pixel);

// Encapsulates the post processing subsurface recombine pixel shader.
class FSubsurfaceBurleyRecombinePS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceBurleyRecombinePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceBurleyRecombinePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceBurleyParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, SubsurfaceInput0)
		SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, SubsurfaceInput1)
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
		const uint32 QualityCVar = GetSSSQuality();

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

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceBurleyRecombinePS, "/Engine/Private/SubsurfaceBurleyNormalized.usf", "SubsurfaceRecombinePS", SF_Pixel);

class FSubsurfaceBurleySinglePassPS : public FSubsurfaceShader
{
public:
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_SINGLE_PASS"),1);
	}

	DECLARE_GLOBAL_SHADER(FSubsurfaceBurleySinglePassPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceBurleySinglePassPS, FSubsurfaceShader)
    
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceBurleyParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, SubsurfaceInput0)
		//SHADER_PARAMETER_STRUCT(FSubsurfaceBurleyInput, SubsurfaceInput1)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		//SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler1)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	
	class FDimensionMode : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_RECOMBINE_MODE", ESubsurfaceMode);
	class FDimensionHalfRes : SHADER_PERMUTATION_BOOL("SUBSURFACE_HALF_RES");
	class FDimensionCheckerboard : SHADER_PERMUTATION_BOOL("SUBSURFACE_PROFILE_CHECKERBOARD");

	using FPermutationDomain = TShaderPermutationDomain<FDimensionMode,FDimensionHalfRes, FDimensionCheckerboard>;
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceBurleySinglePassPS, "/Engine/Private/SubsurfaceBurleyNormalized.usf", "SubsurfaceSinglePassPS", SF_Pixel);

void ComputeBurleySubsurfaceForView(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTextureRef SceneTexture,
	FRDGTextureRef SceneTextureOutput,
	ERenderTargetLoadAction SceneTextureLoadAction)
{
	check(SceneTexture);
	check(SceneTextureOutput);
	check(SceneViewport.Extent == SceneTexture->Desc.Extent);

	const FViewInfo& View = ScreenPassView.View;

	const FSceneViewFamily* ViewFamily = View.Family;

	const FRDGTextureDesc& SceneTextureDesc = SceneTexture->Desc;

	const ESubsurfaceMode SubsurfaceMode = GetSubsurfaceModeForView(View);

	const bool bHalfRes = (SubsurfaceMode == ESubsurfaceMode::HalfRes);

	const bool bCheckerboard = false;// IsSubsurfaceBurleyNormalizedCheckerboardFormat(SceneTextureDesc.Format);

	const uint32 ScaleFactor = bHalfRes ? 2 : 1;

	const ESubsurfaceBurleyPassType PassType = GetSuburfaceBurleyPassType();

	/**
	 * All subsurface passes within the screen-space subsurface effect can operate at half or full resolution,
	 * depending on the subsurface mode. The values are precomputed and shared among all Subsurface textures.
	 */
	const FScreenPassTextureViewport SubsurfaceViewport = FScreenPassTextureViewport::CreateDownscaled(SceneViewport, ScaleFactor);

	const FRDGTextureDesc SubsurfaceTextureDescriptor = FRDGTextureDesc::Create2DDesc(
		SubsurfaceViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource,
		false);
	
	const FSubsurfaceBurleyParameters SubsurfaceCommonParameters = GetSubsurfaceBurleyCommonParameters(GraphBuilder.RHICmdList, View);
	const FScreenPassTextureViewportParameters SubsurfaceViewportParameters = GetScreenPassTextureViewportParameters(SubsurfaceViewport);
	const FScreenPassTextureViewportParameters SceneViewportParameters = GetScreenPassTextureViewportParameters(SceneViewport);

	FRDGTextureRef SetupTexture = SceneTexture;
	FRDGTextureRef SubsurfaceSubpassOneTex = nullptr;
	FRDGTextureRef SubsurfaceSubpassTwoTex = nullptr;

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRHISamplerState* BilinearBorderSampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();

	if(PassType == ESubsurfaceBurleyPassType::Single)
	{
		FRDGTextureRef SubsurfaceSinglePassTexture = SceneTexture;
		FRHISamplerState* SubsurfaceSamplerState = FSubsurfaceBurleyPS::GetSamplerState();
		const FSubsurfaceBurleyPS::EQuality SubsurfaceQuality = FSubsurfaceBurleyPS::GetQuality();
		
		//single pass
		FSubsurfaceBurleySinglePassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceBurleySinglePassPS::FParameters>();
		PassParameters->Subsurface = SubsurfaceCommonParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, SceneTextureLoadAction, ERenderTargetStoreAction::EStore);
		PassParameters->SubsurfaceInput0 = GetSubsurfaceBurleyInput(SceneTexture, SceneViewportParameters);
		PassParameters->SubsurfaceSampler0 = SubsurfaceSamplerState;

		FSubsurfaceBurleySinglePassPS::FPermutationDomain PixelShaderPermutationVector;
		PixelShaderPermutationVector.Set<FSubsurfaceBurleySinglePassPS::FDimensionMode>(ESubsurfaceMode::FullRes/*SubsurfaceMode*/);
		PixelShaderPermutationVector.Set<FSubsurfaceBurleySinglePassPS::FDimensionHalfRes>(bHalfRes);
		PixelShaderPermutationVector.Set<FSubsurfaceBurleySinglePassPS::FDimensionCheckerboard>(bCheckerboard);
		TShaderMapRef<FSubsurfaceBurleySinglePassPS> PixelShader(View.ShaderMap, PixelShaderPermutationVector);

		//AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceBurley"), ScreenPassView, SubsurfaceViewport, SubsurfaceViewport, *PixelShader, PassParameters);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceBurleySinglePass"), ScreenPassView, SceneViewport, SceneViewport, *PixelShader, PassParameters);
		return;
	}

	//-----------------------------------------------------------------------------------------------------------------------------------
	//
	/**
	 * When in bypass mode, the setup and convolution passes are skipped, but lighting
	 * reconstruction is still performed in the recombine pass.
	 */
	if (SubsurfaceMode != ESubsurfaceMode::Bypass)
	{
		SetupTexture = GraphBuilder.CreateTexture(SubsurfaceTextureDescriptor, TEXT("SubsurfaceSetupTexture"));

		// Setup pass outputs the diffuse scene color and depth in preparation for the scatter passes.
		{
			FSubsurfaceBurleySetupPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceBurleySetupPS::FParameters>();
			PassParameters->Subsurface = SubsurfaceCommonParameters;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SetupTexture, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
			PassParameters->SubsurfaceInput0 = GetSubsurfaceBurleyInput(SceneTexture, SceneViewportParameters);
			PassParameters->SubsurfaceSampler0 = PointClampSampler;

			FSubsurfaceBurleySetupPS::FPermutationDomain PixelShaderPermutationVector;
			PixelShaderPermutationVector.Set<FSubsurfaceBurleySetupPS::FDimensionHalfRes>(bHalfRes);
			PixelShaderPermutationVector.Set<FSubsurfaceBurleySetupPS::FDimensionCheckerboard>(bCheckerboard);
			TShaderMapRef<FSubsurfaceBurleySetupPS> PixelShader(View.ShaderMap, PixelShaderPermutationVector);

			/**
			 * The subsurface viewport is intentionally used as both the target and texture viewport, even though the texture
			 * is potentially double the size. This is to ensure that the source UVs map 1-to-1 with pixel centers of the target,
			 * in order to ensure that the checkerboard pattern selects the correct pixels from the scene texture. This still works
			 * because the texture viewport is normalized into UV space, so it doesn't matter that the dimensions are twice as large.
			 */
			AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceSetup"), ScreenPassView, SubsurfaceViewport, SubsurfaceViewport, *PixelShader, PassParameters);
		}

		SubsurfaceSubpassOneTex = GraphBuilder.CreateTexture(SubsurfaceTextureDescriptor, TEXT("SubsurfaceSubpassOneTex"));
		SubsurfaceSubpassTwoTex = GraphBuilder.CreateTexture(SubsurfaceTextureDescriptor, TEXT("SubsurfaceSubpassTwoTex"));

		FRHISamplerState* SubsurfaceSamplerState = FSubsurfaceBurleyPS::GetSamplerState();
		const FSubsurfaceBurleyPS::EQuality SubsurfaceQuality = FSubsurfaceBurleyPS::GetQuality();

		struct FSubsurfacePassInfo
		{
			FSubsurfacePassInfo(const TCHAR* InName, FRDGTextureRef InInput, FRDGTextureRef InOutput)
				: Name(InName)
				, Input(InInput)
				, Output(InOutput)
			{}

			const TCHAR* Name;
			FRDGTextureRef Input;
			FRDGTextureRef Output;
		};

		//we will determine burley or SSSS at runtime.
		const FSubsurfacePassInfo SubsurfacePassInfos[] =
		{
			{ TEXT("SubsurfacePassOneTex"), SetupTexture, SubsurfaceSubpassOneTex },
			{ TEXT("SubsurfacePassTwoTex"), SubsurfaceSubpassOneTex, SubsurfaceSubpassTwoTex },
		};

		const uint32 NumOfPasses = static_cast<uint32>(FSubsurfaceBurleyPS::ESubsurfacePass::MAX);

		// Horizontal / Vertical scattering passes using a separable filter.
		for (uint32 PassIndex = 0; PassIndex < NumOfPasses; ++PassIndex)
		{
			const auto SubsurfacePassFunction = static_cast<FSubsurfaceBurleyPS::ESubsurfacePass>(PassIndex);

			const FSubsurfacePassInfo& PassInfo = SubsurfacePassInfos[PassIndex];
			FRDGTextureRef TextureInput = PassInfo.Input;
			FRDGTextureRef TextureOutput = PassInfo.Output;

			FSubsurfaceBurleyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceBurleyPS::FParameters>();
			PassParameters->Subsurface = SubsurfaceCommonParameters;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(TextureOutput, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore);
			PassParameters->SubsurfaceInput0 = GetSubsurfaceBurleyInput(TextureInput, SubsurfaceViewportParameters);
			PassParameters->SubsurfaceSampler0 = SubsurfaceSamplerState;

			FSubsurfaceBurleyPS::FPermutationDomain PixelShaderPermutationVector;
			PixelShaderPermutationVector.Set<FSubsurfaceBurleyPS::FSubsurfacePassFunction>(SubsurfacePassFunction);
			PixelShaderPermutationVector.Set<FSubsurfaceBurleyPS::FDimensionQuality>(SubsurfaceQuality);
			TShaderMapRef<FSubsurfaceBurleyPS> PixelShader(View.ShaderMap, PixelShaderPermutationVector);

			AddDrawScreenPass(GraphBuilder, FRDGEventName(PassInfo.Name), ScreenPassView, SubsurfaceViewport, SubsurfaceViewport, *PixelShader, PassParameters);
		}
	}

	// Recombines scattering result with scene color.
	{
		FSubsurfaceBurleyRecombinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceBurleyRecombinePS::FParameters>();
		PassParameters->Subsurface = SubsurfaceCommonParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, SceneTextureLoadAction, ERenderTargetStoreAction::EStore);
		PassParameters->SubsurfaceInput0 = GetSubsurfaceBurleyInput(SceneTexture, SceneViewportParameters);
		PassParameters->SubsurfaceSampler0 = BilinearBorderSampler;

		// Scattering output target is only used when scattering is enabled.
		if (SubsurfaceMode != ESubsurfaceMode::Bypass)
		{
			PassParameters->SubsurfaceInput1 = GetSubsurfaceBurleyInput(SubsurfaceSubpassTwoTex, SubsurfaceViewportParameters);
			PassParameters->SubsurfaceSampler1 = BilinearBorderSampler;
		}

		const FSubsurfaceBurleyRecombinePS::EQuality RecombineQuality = FSubsurfaceBurleyRecombinePS::GetQuality(View);

		FSubsurfaceBurleyRecombinePS::FPermutationDomain PixelShaderPermutationVector;
		PixelShaderPermutationVector.Set<FSubsurfaceBurleyRecombinePS::FDimensionMode>(SubsurfaceMode);
		PixelShaderPermutationVector.Set<FSubsurfaceBurleyRecombinePS::FDimensionQuality>(RecombineQuality);
		PixelShaderPermutationVector.Set<FSubsurfaceBurleyRecombinePS::FDimensionCheckerboard>(bCheckerboard);
		TShaderMapRef<FSubsurfaceBurleyRecombinePS> PixelShader(View.ShaderMap, PixelShaderPermutationVector);

		/**
		 * See the related comment above in the prepare pass. The scene viewport is used as both the target and
		 * texture viewport in order to ensure that the correct pixel is sampled for checkerboard rendering.
		 */
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceRecombine"), ScreenPassView, SceneViewport, SceneViewport, *PixelShader, PassParameters);
	}
}