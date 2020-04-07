// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMobile.cpp: Uber post for mobile implementation.
=============================================================================*/

#include "PostProcess/PostProcessMobile.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "PostProcess/PostProcessing.h"

static TAutoConsoleVariable<int32> CVarMobileSupportBloomSetupRareCases(
	TEXT("r.Mobile.MobileSupportBloomSetupRareCases"),
	0,
	TEXT("0: Don't generate permutations for BloomSetup rare cases. (default, like Sun+MetalMSAAHDRDecode, Dof+MetalMSAAHDRDecode, EyeAdaptaion+MetalMSAAHDRDecode, and any of their combinations)\n")
	TEXT("1: Generate permutations for BloomSetup rare cases. "),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarMobileEyeAdaptation(
	TEXT("r.Mobile.EyeAdaptation"),
	1,
	TEXT("EyeAdaptation for mobile platform.\n")
	TEXT(" 0: Disable\n")
	TEXT(" 1: Enabled (Default)"),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER(FMSAADecodeAndCopyRectPS_ES2, "/Engine/Private/PostProcessMobile.usf", "MSAADecodeAndCopyRectPS", SF_Pixel);

static EPixelFormat GetHDRPixelFormat()
{
	return PF_FloatRGBA;
}

// return Depth of Field Scale if Gaussian DoF mode is active. 0.0f otherwise.
float GetMobileDepthOfFieldScale(const FViewInfo& View)
{
	return View.FinalPostProcessSettings.DepthOfFieldScale;
}

static const FRenderingCompositePass* GMobilePassShouldFlipVerticalAxis = nullptr;
void SetMobilePassFlipVerticalAxis(const FRenderingCompositePass* FlipPass)
{
	GMobilePassShouldFlipVerticalAxis = FlipPass;
}

bool ShouldMobilePassFlipVerticalAxis(const FRenderingCompositePassContext& Context, const FRenderingCompositePass* ShouldFlipPass)
{
	return RHINeedsToSwitchVerticalAxis(Context.GetShaderPlatform()) && (GMobilePassShouldFlipVerticalAxis == ShouldFlipPass);
}

bool IsMobileEyeAdaptationEnabled(const FViewInfo& View)
{
	return IsMobileHDR() && View.ViewState != nullptr && View.Family->EngineShowFlags.EyeAdaptation && CVarMobileEyeAdaptation.GetValueOnRenderThread() == 1;
}

//Following variations are always generated
// 1 = Bloom
// 3 = Bloom + SunShaft
// 5 = Bloom + Dof
// 7 = Bloom + Dof + SunShaft
// 9 = Bloom + EyeAdaptation
// 11 = Bloom + SunShaft + EyeAdaptation
// 13 = Bloom + Dof + EyeAdaptation
// 15 = Bloom + SunShaft + Dof + EyeAdaptation
// 8 = EyeAdaptation

//Following variations should only be generated on IOS, only IOS has to do PreTonemapMSAA if MSAA is enabled.
// 17 = Bloom + MetalMSAAHDRDecode
// 21 = Bloom + Dof + MetalMSAAHDRDecode
// 25 = Bloom + EyeAdaptation + MetalMSAAHDRDecode
// 29 = Bloom + Dof + EyeAdaptation + MetalMSAAHDRDecode

//Following variations are rare cases, depends on CVarMobileSupportBloomSetupRareCases
// 2 = SunShaft
// 4 = Dof
// 6 = SunShaft + Dof

// 10 = SunShaft + EyeAdaptation
// 12 = Dof + EyeAdaptation
// 14 = SunShaft + Dof + EyeAdaptation

// 20 = Dof + MetalMSAAHDRDecode
// 24 = EyeAdaptation + MetalMSAAHDRDecode
// 28 = Dof + EyeAdaptation + MetalMSAAHDRDecode

//Any variations with SunShaft + MetalMSAAHDRDecode should be not generated, because SceneColor has been decoded at SunMask pass
// 19 = Bloom + SunShaft + MetalMSAAHDRDecode
// 23 = Bloom + Dof + SunShaft + MetalMSAAHDRDecode
// 27 = Bloom + SunShaft + EyeAdaptation + MetalMSAAHDRDecode
// 31 = Bloom + SunShaft + Dof + EyeAdaptation + MetalMSAAHDRDecode

// 18 = SunShaft + MetalMSAAHDRDecode
// 22 = Dof + SunShaft + MetalMSAAHDRDecode
// 26 = SunShaft + EyeAdaptation + MetalMSAAHDRDecode
// 30 = SunShaft + Dof + EyeAdaptation + MetalMSAAHDRDecode


// Remove the variation from this list if it should not be a rare case or enable the CVarMobileSupportBloomSetupRareCases for full cases.
bool IsValidBloomSetupVariation(uint32 Variation)
{
	bool bIsRareCases =
		Variation == 2 ||
		Variation == 4 ||
		Variation == 6 ||

		Variation == 10 ||
		Variation == 12 ||
		Variation == 14 ||
		
		Variation == 20 || 
		Variation == 24 || 
		Variation == 28;

	return !bIsRareCases || CVarMobileSupportBloomSetupRareCases.GetValueOnAnyThread() != 0;
}

bool IsValidBloomSetupVariation(bool bUseBloom, bool bUseSun, bool bUseDof, bool bUseEyeAdaptation)
{
	uint32 Variation = bUseBloom	? 1 << 0 : 0;
	Variation |= bUseSun			? 1 << 1 : 0;
	Variation |= bUseDof			? 1 << 2 : 0;
	Variation |= bUseEyeAdaptation	? 1 << 3 : 0;
	return IsValidBloomSetupVariation(Variation);
}

uint32 GetBloomSetupOutputNum(bool bUseBloom, bool bUseSun, bool bUseDof, bool bUseEyeAdaptation)
{
	bool bValidVariation = IsValidBloomSetupVariation(bUseBloom, bUseSun, bUseDof, bUseEyeAdaptation);

	//if the variation is invalid, always use bloom permutation
	return ((!bValidVariation || bUseBloom) ? 1 : 0) + ((bUseSun || bUseDof) ? 1 : 0) + (bUseEyeAdaptation ? 1 : 0);
}

//
// BLOOM SETUP
//

class FPostProcessBloomSetupVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBloomSetupVS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	/** Default constructor. */
	FPostProcessBloomSetupVS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	/** Initialization constructor. */
	FPostProcessBloomSetupVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(, FPostProcessBloomSetupVS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("BloomVS_ES2"), SF_Vertex);


class FPostProcessBloomSetupPS_ES2 : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPostProcessBloomSetupPS_ES2);
	SHADER_USE_PARAMETER_STRUCT(FPostProcessBloomSetupPS_ES2, FGlobalShader);

	class FUseBloomDim :				SHADER_PERMUTATION_BOOL("ES2_USE_BLOOM");
	class FUseSunDim :					SHADER_PERMUTATION_BOOL("ES2_USE_SUN");
	class FUseDofDim :					SHADER_PERMUTATION_BOOL("ES2_USE_DOF");
	class FUseEyeAdaptationDim :		SHADER_PERMUTATION_BOOL("ES2_USE_EYEADAPTATION");
	class FUseMetalMSAAHDRDecodeDim :	SHADER_PERMUTATION_BOOL("METAL_MSAA_HDR_DECODE");

	using FPermutationDomain = TShaderPermutationDomain<
		FUseBloomDim,
		FUseSunDim,
		FUseDofDim,
		FUseEyeAdaptationDim,
		FUseMetalMSAAHDRDecodeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, BloomThreshold)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_TEXTURE(Texture2D, PostprocessInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, PostprocessInput0Sampler)
		SHADER_PARAMETER_TEXTURE(Texture2D, PostprocessInput1)
		SHADER_PARAMETER_SAMPLER(SamplerState, PostprocessInput1Sampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		auto bUseBloomDim = PermutationVector.Get<FUseBloomDim>();

		auto bUseSunDim = PermutationVector.Get<FUseSunDim>();

		auto bUseDofDim = PermutationVector.Get<FUseDofDim>();

		auto bUseEyeAdaptationDim = PermutationVector.Get<FUseEyeAdaptationDim>();

		auto bUseMetalMSAAHDRDecodeDim = PermutationVector.Get<FUseMetalMSAAHDRDecodeDim>();

		bool bValidVariation = IsValidBloomSetupVariation(bUseBloomDim, bUseSunDim, bUseDofDim, bUseEyeAdaptationDim);

		return IsMobilePlatform(Parameters.Platform) && 
			// Exclude rare cases if CVarMobileSupportBloomSetupRareCases is 0
			(bValidVariation) && 
			// IOS should generate all valid variations except SunShaft + MetalMSAAHDRDecode, other mobile platform should exclude MetalMSAAHDRDecode permutation
			(!bUseMetalMSAAHDRDecodeDim || (IsMetalMobilePlatform(Parameters.Platform) && !bUseSunDim));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static FPermutationDomain RemapPermutationVector(FPermutationDomain PermutationVector, bool bValidVariation)
	{
		if (!bValidVariation)
		{
			//Use the permutation with Bloom
			PermutationVector.Set<FUseBloomDim>(true);
		}
		return PermutationVector;
	}

	static FPermutationDomain BuildPermutationVector(bool bInUseBloom, bool bInUseSun, bool bInUseDof, bool bInUseEyeAdaptation, bool bInUseMetalMSAAHDRDecode)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FUseBloomDim>(bInUseBloom);
		PermutationVector.Set<FUseSunDim>(bInUseSun);
		PermutationVector.Set<FUseDofDim>(bInUseDof);
		PermutationVector.Set<FUseEyeAdaptationDim>(bInUseEyeAdaptation);
		PermutationVector.Set<FUseMetalMSAAHDRDecodeDim>(bInUseMetalMSAAHDRDecode);
		return RemapPermutationVector(PermutationVector, IsValidBloomSetupVariation(bInUseBloom, bInUseSun, bInUseDof, bInUseEyeAdaptation));
	}
public:

	void SetPS(const FRenderingCompositePassContext& Context, const TShaderRef<FPostProcessBloomSetupPS_ES2>& Shader, FRHITexture* PostprocessInput0, FRHITexture* PostprocessInput1)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();

		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;

		FParameters ShaderParameters;

		ShaderParameters.EyeAdaptation = GetEyeAdaptationParameters(Context.View, ERHIFeatureLevel::ES3_1);
		ShaderParameters.BloomThreshold = Settings.BloomThreshold;
		ShaderParameters.View = Context.View.ViewUniformBuffer;

		ShaderParameters.PostprocessInput0 = PostprocessInput0;
		ShaderParameters.PostprocessInput0Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		ShaderParameters.PostprocessInput1 = PostprocessInput1;
		ShaderParameters.PostprocessInput1Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		SetShaderParameters(Context.RHICmdList, Shader, ShaderRHI, ShaderParameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPostProcessBloomSetupPS_ES2, "/Engine/Private/PostProcessMobile.usf", "BloomPS_ES2", SF_Pixel);

void FRCPassPostProcessBloomSetupES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessBloomSetup);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);
	FIntPoint PrePostSourceViewportSize = PrePostSourceViewportRect.Size();

	uint32 DstX = FMath::DivideAndRoundUp(PrePostSourceViewportSize.X, 4);
	uint32 DstY = FMath::DivideAndRoundUp(PrePostSourceViewportSize.Y, 4);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	FIntPoint DstSize(DstX, DstY);

	FIntPoint SrcSize;
	FIntRect SrcRect;
	if (bUseViewRectSource)
	{
		// Mobile with framebuffer fetch uses view rect as source.
		const FViewInfo& View = Context.View;
		SrcSize = InputDesc->Extent;
		//	uint32 ScaleFactor = View.ViewRect.Width() / SrcSize.X;
		//	SrcRect = View.ViewRect / ScaleFactor;
		// TODO: This won't work with scaled views.
		SrcRect = PrePostSourceViewportRect;
	}
	else
	{
		// Otherwise using exact size texture.
		SrcSize = DstSize;
		SrcRect = DstRect;
	}

	const FSceneRenderTargetItem& DestRenderTarget0 = PassOutputs[0].RequestSurface(Context);
	const FSceneRenderTargetItem* DestRenderTarget1 = nullptr;
	const FSceneRenderTargetItem* DestRenderTarget2 = nullptr;

	uint32 OutputNum = GetBloomSetupOutputNum(bUseBloom, bUseSun, bUseDof, bUseEyeAdaptation);
	if (OutputNum > 1)
	{
		DestRenderTarget1 = &PassOutputs[1].RequestSurface(Context);
	}
	if (OutputNum > 2)
	{
		DestRenderTarget2 = &PassOutputs[2].RequestSurface(Context);
	}

	FRHITexture* RenderTargets[3] =
	{
		DestRenderTarget0.TargetableTexture,
		DestRenderTarget1 != nullptr ? DestRenderTarget1->TargetableTexture : nullptr,
		DestRenderTarget2 != nullptr ? DestRenderTarget2->TargetableTexture : nullptr
	};

	int32 NumRenderTargets = OutputNum;

	FRHIRenderPassInfo RPInfo(NumRenderTargets, RenderTargets, ERenderTargetActions::DontLoad_Store);

	bool bIsValidVariation = IsValidBloomSetupVariation(bUseBloom, bUseSun, bUseDof, bUseEyeAdaptation);

	if (!bIsValidVariation)
	{
		RPInfo.ColorRenderTargets[0].Action = ERenderTargetActions::DontLoad_DontStore;
	}

	FRHITexture* InputRenderTarget0 = GetInput(ePId_Input0)->GetOutput()->PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;

	FRHITexture* InputRenderTarget1 = GetInput(ePId_Input1)->IsValid() ? GetInput(ePId_Input1)->GetOutput()->PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture : nullptr;

	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessBloomSetupES2"));
	{
		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessBloomSetupVS_ES2> VertexShader(Context.GetShaderMap());
		auto ShaderPermutationVector = FPostProcessBloomSetupPS_ES2::BuildPermutationVector(bUseBloom, bUseSun, bUseDof, bUseEyeAdaptation, bUseMetalMSAAHDRDecode);
		TShaderMapRef<FPostProcessBloomSetupPS_ES2> PixelShader(Context.GetShaderMap(), ShaderPermutationVector);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader->SetVS(Context);
		PixelShader->SetPS(Context, PixelShader, InputRenderTarget0, InputRenderTarget1);

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DstSize,
			SrcSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();

	for (int32 i = 0; i < NumRenderTargets; ++i)
	{
		if (RenderTargets[i] != nullptr)
		{
			Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargets[i]);
		}
	}
}

FPooledRenderTargetDesc FRCPassPostProcessBloomSetupES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	bool bIsValidVariation = IsValidBloomSetupVariation(bUseBloom, bUseSun, bUseDof, bUseEyeAdaptation);

	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;

	if (!bIsValidVariation && InPassOutputId == ePId_Output0)
	{
		Ret.TargetableFlags |= TexCreate_Memoryless;
	}

	Ret.bForceSeparateTargetAndShaderResource = false;
	
	if (!bIsValidVariation || bUseBloom)
	{
		Ret.Format = InPassOutputId == ePId_Output0 ? PF_FloatR11G11B10 : PF_R16F;
	}
	else
	{
		Ret.Format = PF_R16F;
	}
	
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, FMath::DivideAndRoundUp(PrePostSourceViewportRect.Width(), 4));
	Ret.Extent.Y = FMath::Max(1, FMath::DivideAndRoundUp(PrePostSourceViewportRect.Height(), 4));
	Ret.DebugName = InPassOutputId == ePId_Output0 ? TEXT("BloomSetup0") : (InPassOutputId == ePId_Output1 ? TEXT("BloomSetup1") : TEXT("BloomSetup2"));
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}






//
// BLOOM DOWNSAMPLE
//

class FPostProcessBloomDownPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBloomDownPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPostProcessBloomDownPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessBloomDownPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	template <typename TRHICmdList>
	void SetPS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessBloomDownPS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("BloomDownPS_ES2"),SF_Pixel);


class FPostProcessBloomDownVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBloomDownVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessBloomDownVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, BloomDownScale);

	FPostProcessBloomDownVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		BloomDownScale.Bind(Initializer.ParameterMap, TEXT("BloomDownScale"));
	}

	void SetVS(const FRenderingCompositePassContext& Context, float InScale)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
		SetShaderValue(Context.RHICmdList, ShaderRHI, BloomDownScale, InScale);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessBloomDownVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("BloomDownVS_ES2"),SF_Vertex);


void FRCPassPostProcessBloomDownES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessBloomDown);

	uint32 DstX = FMath::DivideAndRoundUp(PrePostSourceViewportSize.X, 2);
	uint32 DstY = FMath::DivideAndRoundUp(PrePostSourceViewportSize.Y, 2);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::DontLoad_Store);

	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessBloomDownES2"));
	{

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessBloomDownVS_ES2> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessBloomDownPS_ES2> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader->SetVS(Context, Scale);
		PixelShader->SetPS(Context.RHICmdList, Context);

		FIntPoint SrcDstSize = FIntPoint::DivideAndRoundUp(PrePostSourceViewportSize, 2);

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessBloomDownES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	
	Ret.Format = PF_FloatR11G11B10;

	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::DivideAndRoundUp(PrePostSourceViewportSize.X, 2);
	Ret.Extent.Y = FMath::DivideAndRoundUp(PrePostSourceViewportSize.Y, 2);
	Ret.DebugName = TEXT("BloomDown");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}





//
// BLOOM UPSAMPLE
//

class FPostProcessBloomUpPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBloomUpPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPostProcessBloomUpPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, TintA);
	LAYOUT_FIELD(FShaderParameter, TintB);

	FPostProcessBloomUpPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		TintA.Bind(Initializer.ParameterMap, TEXT("BloomTintA"));
		TintB.Bind(Initializer.ParameterMap, TEXT("BloomTintB"));
	}

	template <typename TRHICmdList>
	void SetPS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, FVector4& InTintA, FVector4& InTintB)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
		SetShaderValue(RHICmdList, ShaderRHI, TintA, InTintA);
		SetShaderValue(RHICmdList, ShaderRHI, TintB, InTintB);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessBloomUpPS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("BloomUpPS_ES2"),SF_Pixel);


class FPostProcessBloomUpVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessBloomUpVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessBloomUpVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, BloomUpScales);

	FPostProcessBloomUpVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		BloomUpScales.Bind(Initializer.ParameterMap, TEXT("BloomUpScales"));
	}

	void SetVS(const FRenderingCompositePassContext& Context, FVector2D InScale)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
		SetShaderValue(Context.RHICmdList, ShaderRHI, BloomUpScales, InScale);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessBloomUpVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("BloomUpVS_ES2"),SF_Vertex);


void FRCPassPostProcessBloomUpES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessBloomUp);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	
	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;

	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
		
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessBloomUpES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessBloomUpVS_ES2> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessBloomUpPS_ES2> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		// The 1/8 factor is because bloom is using 8 taps in the filter.
		VertexShader->SetVS(Context, FVector2D(ScaleAB.X, ScaleAB.Y));
		FVector4 TintAScaled = TintA * (1.0f / 8.0f);
		FVector4 TintBScaled = TintB * (1.0f / 8.0f);
		PixelShader->SetPS(Context.RHICmdList, Context, TintAScaled, TintBScaled);

		FIntPoint SrcDstSize = PrePostSourceViewportSize;

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessBloomUpES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = PF_FloatR11G11B10;
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y);
	Ret.DebugName = TEXT("BloomUp");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}





//
// SUN MASK
//

class FPostProcessSunMaskPS_ES2 : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPostProcessSunMaskPS_ES2);
	SHADER_USE_PARAMETER_STRUCT(FPostProcessSunMaskPS_ES2, FGlobalShader);

	class FUseSunDim :					SHADER_PERMUTATION_BOOL("ES2_USE_SUN");
	class FUseDofDim :					SHADER_PERMUTATION_BOOL("ES2_USE_DOF");
	class FUseDepthTextureDim :			SHADER_PERMUTATION_BOOL("ES2_USE_DEPTHTEXTURE");
	class FUseMetalMSAAHDRDecodeDim :	SHADER_PERMUTATION_BOOL("METAL_MSAA_HDR_DECODE");

	using FPermutationDomain = TShaderPermutationDomain<
		FUseSunDim,
		FUseDofDim, 
		FUseDepthTextureDim,
		FUseMetalMSAAHDRDecodeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, SunColorApertureDiv2)
		SHADER_PARAMETER_STRUCT_REF(FMobileSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_TEXTURE(Texture2D, PostprocessInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, PostprocessInput0Sampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		auto bUseSunDim = PermutationVector.Get<FUseSunDim>();

		auto bUseDofDim = PermutationVector.Get<FUseDofDim>();

		auto bUseMetalMSAAHDRDecodeDim = PermutationVector.Get<FUseMetalMSAAHDRDecodeDim>();

		return IsMobilePlatform(Parameters.Platform) && 
				// Only generate shaders with SunShaft and/or Dof
				(bUseSunDim || bUseDofDim) && 
				// Only generated MetalMSAAHDRDecode shaders for SunShaft
				(!bUseMetalMSAAHDRDecodeDim || (bUseSunDim && IsMetalMobilePlatform(Parameters.Platform)));
	}	

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		
		// This post-processor has a 1-Dimensional color attachment for SV_Target0
		OutEnvironment.SetDefine(TEXT("SUBPASS_COLOR0_ATTACHMENT_DIM"), 1);

		if (IsVulkanMobilePlatform(Parameters.Platform))
		{
			// depth fetch only available during base pass rendering
			// TODO: find better place to enable frame buffer fetch feature only for base pass
			CA_SUPPRESS(6313);
			OutEnvironment.SetDefine(TEXT("VULKAN_SUBPASS_DEPTHFETCH"), 0);
		}
	}

	static FPermutationDomain RemapPermutationVector(FPermutationDomain PermutationVector)
	{
		auto UseSunDim = PermutationVector.Get<FUseSunDim>();

		if (!UseSunDim)
		{
			// Don't use MetalMSAAHDRDecode permutation without SunShaft
			PermutationVector.Set<FUseMetalMSAAHDRDecodeDim>(false);
		}

		return PermutationVector;
	}

	static FPermutationDomain BuildPermutationVector(bool bInUseSun, bool bInUseDof, bool bInUseDepthTexture, bool bInUseMetalMSAAHDRDecode)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FUseSunDim>(bInUseSun);
		PermutationVector.Set<FUseDofDim>(bInUseDof);
		PermutationVector.Set<FUseDepthTextureDim>(bInUseDepthTexture);
		PermutationVector.Set<FUseMetalMSAAHDRDecodeDim>(bInUseMetalMSAAHDRDecode);
		return RemapPermutationVector(PermutationVector);
	}

public:

	void SetPS(const FRenderingCompositePassContext& Context, const TShaderRef<FPostProcessSunMaskPS_ES2>& Shader, FRHITexture* PostprocessInput0)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();

		FParameters ShaderParameters;

		ShaderParameters.View = Context.View.ViewUniformBuffer;

		ShaderParameters.SceneTextures = CreateMobileSceneTextureUniformBufferSingleDraw(Context.RHICmdList, Context.View.FeatureLevel);

		ShaderParameters.SunColorApertureDiv2.X = Context.View.LightShaftColorMask.R;
		ShaderParameters.SunColorApertureDiv2.Y = Context.View.LightShaftColorMask.G;
		ShaderParameters.SunColorApertureDiv2.Z = Context.View.LightShaftColorMask.B;
		ShaderParameters.SunColorApertureDiv2.W = GetMobileDepthOfFieldScale(Context.View) * 0.5f;

		ShaderParameters.PostprocessInput0 = PostprocessInput0;
		ShaderParameters.PostprocessInput0Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		SetShaderParameters(Context.RHICmdList, Shader, ShaderRHI, ShaderParameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FPostProcessSunMaskPS_ES2, "/Engine/Private/PostProcessMobile.usf", "SunMaskPS_ES2", SF_Pixel);


class FPostProcessSunMaskVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunMaskVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessSunMaskVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessSunMaskVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunMaskVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunMaskVS_ES2"),SF_Vertex);

void FRCPassPostProcessSunMaskES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessSunMask);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	FIntPoint DstSize = PrePostSourceViewportSize;

	FIntPoint SrcSize;
	FIntRect SrcRect;
	const FViewInfo& View = Context.View;

	SrcSize = InputDesc->Extent; //-V595
//	uint32 ScaleFactor = View.ViewRect.Width() / SrcSize.X;
//	SrcRect = View.ViewRect / ScaleFactor;
// TODO: This won't work with scaled views.
	SrcRect = View.ViewRect;

	const FSceneRenderTargetItem& DestRenderTarget0 = PassOutputs[0].RequestSurface(Context);
	const FSceneRenderTargetItem* DestRenderTarget1 = nullptr;

	int32 NumRenderTargets = 1;

	if (!bUseDepthTexture)
	{
		DestRenderTarget1 = &PassOutputs[1].RequestSurface(Context);
		NumRenderTargets += 1;
	}

	FRHITexture* RenderTargets[2] =
	{
		DestRenderTarget0.TargetableTexture,
		DestRenderTarget1 != nullptr ? DestRenderTarget1->TargetableTexture : nullptr
	};

	FRHITexture* InputRenderTarget = GetInput(ePId_Input0)->GetOutput()->PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;

	FRHIRenderPassInfo RPInfo(NumRenderTargets, RenderTargets, ERenderTargetActions::DontLoad_Store);

	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessSunMaskES2"));

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessSunMaskVS_ES2> VertexShader(Context.GetShaderMap());
	auto ShaderPermutationVector = FPostProcessSunMaskPS_ES2::BuildPermutationVector(bUseSun, bUseDof, bUseDepthTexture, bUseMetalMSAAHDRDecode);
	TShaderMapRef<FPostProcessSunMaskPS_ES2> PixelShader(Context.GetShaderMap(), ShaderPermutationVector);

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context, PixelShader, InputRenderTarget);

	DrawRectangle(
		Context.RHICmdList,
		SrcRect.Min.X, SrcRect.Min.Y,
		SrcRect.Width(), SrcRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y,
		SrcRect.Width(), SrcRect.Height(),
		DstSize,
		SrcSize,
		VertexShader,
		EDRF_UseTriangleOptimization);

	Context.RHICmdList.EndRenderPass();

	for (int i = 0; i < NumRenderTargets; ++i)
	{
		if (RenderTargets[i] != nullptr)
		{
			Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargets[i]);
		}
	}
}

FPooledRenderTargetDesc FRCPassPostProcessSunMaskES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = InPassOutputId == ePId_Output1 ? PF_FloatR11G11B10 : PF_R16F;
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y);
	Ret.DebugName = TEXT("SunMask");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}




//
// SUN ALPHA
//

template<uint32 UseDof>
class FPostProcessSunAlphaPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunAlphaPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ES2_USE_DOF"), UseDof ? (uint32)1 : (uint32)0);
	}

	FPostProcessSunAlphaPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessSunAlphaPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

typedef FPostProcessSunAlphaPS_ES2<0> FPostProcessSunAlphaPS_ES2_0;
typedef FPostProcessSunAlphaPS_ES2<1> FPostProcessSunAlphaPS_ES2_1;
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessSunAlphaPS_ES2_0,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunAlphaPS_ES2"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessSunAlphaPS_ES2_1,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunAlphaPS_ES2"),SF_Pixel);

class FPostProcessSunAlphaVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunAlphaVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessSunAlphaVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, LightShaftCenter);

	FPostProcessSunAlphaVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		LightShaftCenter.Bind(Initializer.ParameterMap, TEXT("LightShaftCenter"));
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		SetShaderValue(Context.RHICmdList, ShaderRHI, LightShaftCenter, Context.View.LightShaftCenter);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunAlphaVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunAlphaVS_ES2"),SF_Vertex);

template <uint32 UseDof>
static void SunAlpha_SetShader(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessSunAlphaVS_ES2> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessSunAlphaPS_ES2<UseDof> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context);
}

void FRCPassPostProcessSunAlphaES2::SetShader(const FRenderingCompositePassContext& Context)
{
	if(GetMobileDepthOfFieldScale(Context.View))
	{
		SunAlpha_SetShader<1>(Context);
	}
	else
	{
		SunAlpha_SetShader<0>(Context);
	}
}

void FRCPassPostProcessSunAlphaES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessSunAlpha);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X/4);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y/4);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	
	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;

	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessSunAlphaES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		SetShader(Context);

		FIntPoint SrcDstSize = PrePostSourceViewportSize / 4;
		TShaderMapRef<FPostProcessSunAlphaVS_ES2> VertexShader(Context.GetShaderMap());

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessSunAlphaES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	// Only need one 8-bit channel as output (but mobile hardware often doesn't support that as a render target format).
	// Highlight compression (tonemapping) was used to keep this in 8-bit.
	Ret.Format = PF_G8;
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/4);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/4);
	Ret.DebugName = TEXT("SunAlpha");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}





//
// SUN BLUR
//

class FPostProcessSunBlurPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunBlurPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPostProcessSunBlurPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessSunBlurPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunBlurPS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunBlurPS_ES2"),SF_Pixel);


class FPostProcessSunBlurVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunBlurVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessSunBlurVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, LightShaftCenter);

	FPostProcessSunBlurVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		LightShaftCenter.Bind(Initializer.ParameterMap, TEXT("LightShaftCenter"));
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		SetShaderValue(Context.RHICmdList, ShaderRHI, LightShaftCenter, Context.View.LightShaftCenter);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunBlurVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunBlurVS_ES2"),SF_Vertex);


void FRCPassPostProcessSunBlurES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessSunBlur);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X/4);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y/4);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	
	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}

	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("SunBlurES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessSunBlurVS_ES2> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessSunBlurPS_ES2> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader->SetVS(Context);
		PixelShader->SetPS(Context);

		FIntPoint SrcDstSize = PrePostSourceViewportSize / 4;

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessSunBlurES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	// Only need one 8-bit channel as output (but mobile hardware often doesn't support that as a render target format).
	// Highlight compression (tonemapping) was used to keep this in 8-bit.
	Ret.Format = PF_G8;
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/4);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/4);
	Ret.DebugName = TEXT("SunBlur");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}




//
// SUN MERGE
//

template <uint32 UseSunBloom>
class FPostProcessSunMergePS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunMergePS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		CA_SUPPRESS(6313);
		OutEnvironment.SetDefine(TEXT("ES2_USE_BLOOM"), (UseSunBloom & 1) ? (uint32)1 : (uint32)0);
		OutEnvironment.SetDefine(TEXT("ES2_USE_SUN"), (UseSunBloom >> 1) ? (uint32)1 : (uint32)0);
	}

	FPostProcessSunMergePS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, SunColorVignetteIntensity);
	LAYOUT_FIELD(FShaderParameter, VignetteColor);
	LAYOUT_FIELD(FShaderParameter, BloomColor);

	FPostProcessSunMergePS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SunColorVignetteIntensity.Bind(Initializer.ParameterMap, TEXT("SunColorVignetteIntensity"));
		VignetteColor.Bind(Initializer.ParameterMap, TEXT("VignetteColor"));
		BloomColor.Bind(Initializer.ParameterMap, TEXT("BloomColor"));
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		FVector4 SunColorVignetteIntensityParam;
		SunColorVignetteIntensityParam.X = Context.View.LightShaftColorApply.R;
		SunColorVignetteIntensityParam.Y = Context.View.LightShaftColorApply.G;
		SunColorVignetteIntensityParam.Z = Context.View.LightShaftColorApply.B;
		SunColorVignetteIntensityParam.W = Settings.VignetteIntensity;
		SetShaderValue(Context.RHICmdList, ShaderRHI, SunColorVignetteIntensity, SunColorVignetteIntensityParam);

		// Scaling Bloom1 by extra factor to match filter area difference between PC default and mobile.
		SetShaderValue(Context.RHICmdList, ShaderRHI, BloomColor, Context.View.FinalPostProcessSettings.Bloom1Tint * Context.View.FinalPostProcessSettings.BloomIntensity * 0.5);
	}
};

typedef FPostProcessSunMergePS_ES2<0> FPostProcessSunMergePS_ES2_0;
typedef FPostProcessSunMergePS_ES2<1> FPostProcessSunMergePS_ES2_1;
typedef FPostProcessSunMergePS_ES2<2> FPostProcessSunMergePS_ES2_2;
typedef FPostProcessSunMergePS_ES2<3> FPostProcessSunMergePS_ES2_3;
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessSunMergePS_ES2_0,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunMergePS_ES2"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessSunMergePS_ES2_1,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunMergePS_ES2"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessSunMergePS_ES2_2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunMergePS_ES2"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessSunMergePS_ES2_3,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunMergePS_ES2"),SF_Pixel);


class FPostProcessSunMergeVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunMergeVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessSunMergeVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, LightShaftCenter);

	FPostProcessSunMergeVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		LightShaftCenter.Bind(Initializer.ParameterMap, TEXT("LightShaftCenter"));
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		SetShaderValue(Context.RHICmdList, ShaderRHI, LightShaftCenter, Context.View.LightShaftCenter);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunMergeVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunMergeVS_ES2"),SF_Vertex);



template <uint32 UseSunBloom>
TShaderRef<FShader> SunMerge_SetShader(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessSunMergeVS_ES2> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessSunMergePS_ES2<UseSunBloom> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context);

	return VertexShader;
}

TShaderRef<FShader> FRCPassPostProcessSunMergeES2::SetShader(const FRenderingCompositePassContext& Context)
{
	const FViewInfo& View = Context.View;
	uint32 UseBloom = (View.FinalPostProcessSettings.BloomIntensity > 0.0f) ? 1 : 0;
	uint32 UseSun = Context.View.bLightShaftUse ? 1 : 0;
	uint32 UseSunBloom = UseBloom + (UseSun<<1);

	switch(UseSunBloom)
	{
		case 0: return SunMerge_SetShader<0>(Context);
		case 1: return SunMerge_SetShader<1>(Context);
		case 2: return SunMerge_SetShader<2>(Context);
		case 3: return SunMerge_SetShader<3>(Context);
	}

	check(false);
	return TShaderRef<FShader>();
}

void FRCPassPostProcessSunMergeES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessSunMerge);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X / 4);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y / 4);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("SunMergeES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		TShaderRef<FShader> VertexShader = SetShader(Context);

		FIntPoint SrcDstSize = PrePostSourceViewportSize / 4;

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());

	// Double buffer sun+bloom+vignette composite.
	if(Context.View.AntiAliasingMethod == AAM_TemporalAA)
	{
		FSceneViewState* ViewState = (FSceneViewState*)Context.View.State;
		if(ViewState) 
		{
			ViewState->MobileAaBloomSunVignette0 = PassOutputs[0].PooledRenderTarget;
		}
	}
}

FPooledRenderTargetDesc FRCPassPostProcessSunMergeES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	// This might not have a valid input texture.
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = GetHDRPixelFormat();
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/4);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/4);
	Ret.DebugName = TEXT("SunMerge");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	Ret.AutoWritable = false;
	return Ret;
}





//
// DOF DOWNSAMPLE
//

class FPostProcessDofDownVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDofDownVS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	/** Default constructor. */
	FPostProcessDofDownVS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	/** Initialization constructor. */
	FPostProcessDofDownVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

template<uint32 UseSun>
class FPostProcessDofDownPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDofDownPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ES2_USE_SUN"), UseSun ? (uint32)1 : (uint32)0);
	}

	FPostProcessDofDownPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessDofDownPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessDofDownVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofDownVS_ES2"),SF_Vertex);

typedef FPostProcessDofDownPS_ES2<0> FPostProcessDofDownPS_ES2_0;
typedef FPostProcessDofDownPS_ES2<1> FPostProcessDofDownPS_ES2_1;
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessDofDownPS_ES2_0,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofDownPS_ES2"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessDofDownPS_ES2_1,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofDownPS_ES2"),SF_Pixel);

template <uint32 UseSun>
static void DofDown_SetShader(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessDofDownVS_ES2> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessDofDownPS_ES2<UseSun> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context);
}

void FRCPassPostProcessDofDownES2::SetShader(const FRenderingCompositePassContext& Context)
{
	const FViewInfo& View = Context.View;
	if(Context.View.bLightShaftUse)
	{
		DofDown_SetShader<1>(Context);
	}
	else
	{
		DofDown_SetShader<0>(Context);
	}
}

void FRCPassPostProcessDofDownES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessDofDown);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);
	FIntPoint PrePostSourceViewportSize = PrePostSourceViewportRect.Size();
	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X/2);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y/2);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	FIntPoint DstSize = PrePostSourceViewportSize / 2;

	FIntPoint SrcSize;
	FIntRect SrcRect;
	if(bUseViewRectSource)
	{
		// Mobile with framebuffer fetch uses view rect as source.
		const FViewInfo& View = Context.View;
		SrcSize = InputDesc->Extent;
		//	uint32 ScaleFactor = View.ViewRect.Width() / SrcSize.X;
		//	SrcRect = View.ViewRect / ScaleFactor;
		// TODO: This won't work with scaled views.
		SrcRect = PrePostSourceViewportRect;
	}
	else
	{
		// Otherwise using exact size texture.
		SrcSize = DstSize;
		SrcRect = DstRect;
	}

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("DofDownES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		SetShader(Context);

		TShaderMapRef<FPostProcessDofDownVS_ES2> VertexShader(Context.GetShaderMap());

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DstSize,
			SrcSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessDofDownES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = GetHDRPixelFormat();
	Ret.NumSamples = 1;
	FIntPoint PrePostSourceViewportSize = PrePostSourceViewportRect.Size();
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/2);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/2);
	Ret.DebugName = TEXT("DofDown");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}




//
// DOF NEAR
//

class FPostProcessDofNearVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDofNearVS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	/** Default constructor. */
	FPostProcessDofNearVS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	/** Initialization constructor. */
	FPostProcessDofNearVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

template<uint32 UseSun>
class FPostProcessDofNearPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDofNearPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ES2_USE_SUN"), UseSun ? (uint32)1 : (uint32)0);
	}

	FPostProcessDofNearPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessDofNearPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessDofNearVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofNearVS_ES2"),SF_Vertex);

typedef FPostProcessDofNearPS_ES2<0> FPostProcessDofNearPS_ES2_0;
typedef FPostProcessDofNearPS_ES2<1> FPostProcessDofNearPS_ES2_1;
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessDofNearPS_ES2_0,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofNearPS_ES2"),SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>,FPostProcessDofNearPS_ES2_1,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofNearPS_ES2"),SF_Pixel);

template <uint32 UseSun>
static void DofNear_SetShader(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessDofNearVS_ES2> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessDofNearPS_ES2<UseSun> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context);
}

void FRCPassPostProcessDofNearES2::SetShader(const FRenderingCompositePassContext& Context)
{
	const FViewInfo& View = Context.View;
	if(Context.View.bLightShaftUse)
	{
		DofNear_SetShader<1>(Context);
	}
	else
	{
		DofNear_SetShader<0>(Context);
	}
}

void FRCPassPostProcessDofNearES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessDofNear);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);
	FIntPoint SrcSize = InputDesc->Extent;

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X/4);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y/4);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("DofNearES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		SetShader(Context);

		FIntPoint SrcDstSize = PrePostSourceViewportSize / 4;
		TShaderMapRef<FPostProcessDofNearVS_ES2> VertexShader(Context.GetShaderMap());

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessDofNearES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	// Only need one 8-bit channel as output (but mobile hardware often doesn't support that as a render target format).
	Ret.Format = PF_G8;
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/4);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/4);
	Ret.DebugName = TEXT("DofNear");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}



//
// DOF BLUR
//

class FPostProcessDofBlurPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDofBlurPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPostProcessDofBlurPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessDofBlurPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessDofBlurPS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofBlurPS_ES2"),SF_Pixel);


class FPostProcessDofBlurVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessDofBlurVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessDofBlurVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessDofBlurVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessDofBlurVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("DofBlurVS_ES2"),SF_Vertex);


void FRCPassPostProcessDofBlurES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessDofBlur);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X/2);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y/2);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	
	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("DofBlurES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessDofBlurVS_ES2> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessDofBlurPS_ES2> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader->SetVS(Context);
		PixelShader->SetPS(Context);

		FIntPoint SrcDstSize = PrePostSourceViewportSize / 2;

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessDofBlurES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = GetHDRPixelFormat();
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/2);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/2);
	Ret.DebugName = TEXT("DofBlur");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}


class FPostProcessIntegrateDofPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessIntegrateDofPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessIntegrateDofPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessIntegrateDofPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(, FPostProcessIntegrateDofPS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("IntegrateDOFPS_ES2"), SF_Pixel);


class FPostProcessIntegrateDofVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessIntegrateDofVS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessIntegrateDofVS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessIntegrateDofVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(, FPostProcessIntegrateDofVS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("IntegrateDOFVS_ES2"), SF_Vertex);

void FRCPassIntegrateDofES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessIntegrateDof);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = PrePostSourceViewportSize.X;
	DstRect.Max.Y = PrePostSourceViewportSize.Y;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("IntegrateDof"));
	{

		Context.SetViewportAndCallRHI(0, 0, 0.0f, PrePostSourceViewportSize.X, PrePostSourceViewportSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		TShaderMapRef<FPostProcessIntegrateDofVS_ES2> VertexShader(Context.GetShaderMap());
		TShaderMapRef<FPostProcessIntegrateDofPS_ES2> PixelShader(Context.GetShaderMap());

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

		VertexShader->SetVS(Context);
		PixelShader->SetPS(Context);

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			PrePostSourceViewportSize.X, PrePostSourceViewportSize.Y,
			0, 0,
			PrePostSourceViewportSize.X, PrePostSourceViewportSize.Y,
			PrePostSourceViewportSize,
			PrePostSourceViewportSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassIntegrateDofES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;
	Ret.DebugName = TEXT("IntegrateDof");
	return Ret;
}


//
// SUN AVG
//

class FPostProcessSunAvgPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunAvgPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPostProcessSunAvgPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessSunAvgPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunAvgPS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunAvgPS_ES2"),SF_Pixel);



class FPostProcessSunAvgVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSunAvgVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessSunAvgVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessSunAvgVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessSunAvgVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("SunAvgVS_ES2"),SF_Vertex);



static void SunAvg_SetShader(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessSunAvgVS_ES2> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessSunAvgPS_ES2> PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context);
}

void FRCPassPostProcessSunAvgES2::SetShader(const FRenderingCompositePassContext& Context)
{
	SunAvg_SetShader(Context);
}

void FRCPassPostProcessSunAvgES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessSunAvg);

	uint32 DstX = FMath::Max(1, PrePostSourceViewportSize.X/4);
	uint32 DstY = FMath::Max(1, PrePostSourceViewportSize.Y/4);

	FIntRect DstRect;
	DstRect.Min.X = 0;
	DstRect.Min.Y = 0;
	DstRect.Max.X = DstX;
	DstRect.Max.Y = DstY;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	
	// OverrideRenderTarget might patch out final render target and we have no control of the clear color anymore
	const bool bUseClearQuad = DestRenderTarget.TargetableTexture->GetClearColor() != FLinearColor::Black;
	ERenderTargetLoadAction LoadAction = ERenderTargetLoadAction::EClear;
	if (bUseClearQuad)
	{
		LoadAction = ERenderTargetLoadAction::ENoAction;
	}
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, MakeRenderTargetActions(LoadAction, ERenderTargetStoreAction::EStore));
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("SunAvgES2"));
	{
		if (bUseClearQuad)
		{
			DrawClearQuad(Context.RHICmdList, FLinearColor::Black);
		}

		Context.SetViewportAndCallRHI(0, 0, 0.0f, DstX, DstY, 1.0f);

		SetShader(Context);

		FIntPoint SrcDstSize = PrePostSourceViewportSize / 4;
		TShaderMapRef<FPostProcessSunAvgVS_ES2> VertexShader(Context.GetShaderMap());

		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			DstX, DstY,
			0, 0,
			DstX, DstY,
			SrcDstSize,
			SrcDstSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessSunAvgES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = GetHDRPixelFormat();
	Ret.NumSamples = 1;
	Ret.Extent.X = FMath::Max(1, PrePostSourceViewportSize.X/4);
	Ret.Extent.Y = FMath::Max(1, PrePostSourceViewportSize.Y/4);
	Ret.DebugName = TEXT("SunAvg");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}





//
// MOBILE AA
//

class FPostProcessAaPS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessAaPS_ES2, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}	

	FPostProcessAaPS_ES2() {}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);
	LAYOUT_FIELD(FShaderParameter, AaBlendAmount);

	FPostProcessAaPS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		AaBlendAmount.Bind(Initializer.ParameterMap, TEXT("AaBlendAmount"));
	}

	void SetPS(const FRenderingCompositePassContext& Context)
	{
		FRHIPixelShader* ShaderRHI = Context.RHICmdList.GetBoundPixelShader();
		const FPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		// Compute the blend factor which decides the trade off between ghosting in motion and flicker when not moving.
		// This works by computing the screen space motion vector of distant point at the center of the screen.
		// This factor will effectively provide an idea of the amount of camera rotation.
		// Higher camera rotation = less blend factor (0.0).
		// Lower or no camera rotation = high blend factor (0.25).
		FSceneViewState* ViewState = (FSceneViewState*)Context.View.State;
		if(ViewState)
		{
			const FViewInfo& View = Context.View;

			FMatrix Proj = View.ViewMatrices.ComputeProjectionNoAAMatrix();
			FMatrix PrevProj = View.PrevViewInfo.ViewMatrices.ComputeProjectionNoAAMatrix();

			FMatrix ViewProj = ( View.ViewMatrices.GetViewMatrix() * Proj ).GetTransposed();
			FMatrix PrevViewProj = ( View.PrevViewInfo.ViewMatrices.GetViewMatrix() * PrevProj ).GetTransposed();

			double InvViewProj[16];
			Inverse4x4( InvViewProj, (float*)ViewProj.M );

			const float* p = (float*)PrevViewProj.M;

			const double cxx = InvViewProj[ 0]; const double cxy = InvViewProj[ 1]; const double cxz = InvViewProj[ 2]; const double cxw = InvViewProj[ 3];
			const double cyx = InvViewProj[ 4]; const double cyy = InvViewProj[ 5]; const double cyz = InvViewProj[ 6]; const double cyw = InvViewProj[ 7];
			const double czx = InvViewProj[ 8]; const double czy = InvViewProj[ 9]; const double czz = InvViewProj[10]; const double czw = InvViewProj[11];
			const double cwx = InvViewProj[12]; const double cwy = InvViewProj[13]; const double cwz = InvViewProj[14]; const double cww = InvViewProj[15];

			const double pxx = (double)(p[ 0]); const double pxy = (double)(p[ 1]); const double pxz = (double)(p[ 2]); const double pxw = (double)(p[ 3]);
			const double pyx = (double)(p[ 4]); const double pyy = (double)(p[ 5]); const double pyz = (double)(p[ 6]); const double pyw = (double)(p[ 7]);
			const double pwx = (double)(p[12]); const double pwy = (double)(p[13]); const double pwz = (double)(p[14]); const double pww = (double)(p[15]);

			float CameraMotion0W = (float)(2.0*(cww*pww - cwx*pww + cwy*pww + (cxw - cxx + cxy)*pwx + (cyw - cyx + cyy)*pwy + (czw - czx + czy)*pwz));
			float CameraMotion2Z = (float)(cwy*pww + cwy*pxw + cww*(pww + pxw) - cwx*(pww + pxw) + (cxw - cxx + cxy)*(pwx + pxx) + (cyw - cyx + cyy)*(pwy + pxy) + (czw - czx + czy)*(pwz + pxz));
			float CameraMotion4Z = (float)(cwy*pww + cww*(pww - pyw) - cwy*pyw + cwx*((-pww) + pyw) + (cxw - cxx + cxy)*(pwx - pyx) + (cyw - cyx + cyy)*(pwy - pyy) + (czw - czx + czy)*(pwz - pyz));

			// Depth surface 0=far, 1=near.
			// This is simplified to compute camera motion with depth = 0.0 (infinitely far away).
			// Camera motion for pixel (in ScreenPos space).
			float ScaleM = 1.0f / CameraMotion0W;
			// Back projection value (projected screen space).
			float BackX = CameraMotion2Z * ScaleM;
			float BackY = CameraMotion4Z * ScaleM;

			// Start with the distance in screen space.
			float BlendAmount = BackX * BackX + BackY * BackY;
			if(BlendAmount > 0.0f)
			{
				BlendAmount = sqrt(BlendAmount);
			}
			
			// Higher numbers truncate anti-aliasing and ghosting faster.
			float BlendEffect = 8.0f;
			BlendAmount = 0.25f - BlendAmount * BlendEffect;
			if(BlendAmount < 0.0f)
			{
				BlendAmount = 0.0f;
			}

			SetShaderValue(Context.RHICmdList, ShaderRHI, AaBlendAmount, BlendAmount);
		}
		else
		{
			float BlendAmount = 0.0;
			SetShaderValue(Context.RHICmdList, ShaderRHI, AaBlendAmount, BlendAmount);
		}
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessAaPS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("AaPS_ES2"),SF_Pixel);



class FPostProcessAaVS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessAaVS_ES2,Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	FPostProcessAaVS_ES2(){}

public:
	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	FPostProcessAaVS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetVS(const FRenderingCompositePassContext& Context)
	{
		FRHIVertexShader* ShaderRHI = Context.RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessAaVS_ES2,TEXT("/Engine/Private/PostProcessMobile.usf"),TEXT("AaVS_ES2"),SF_Vertex);



static void Aa_SetShader(const FRenderingCompositePassContext& Context)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	TShaderMapRef<FPostProcessAaVS_ES2> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessAaPS_ES2> PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader->SetVS(Context);
	PixelShader->SetPS(Context);
}

void FRCPassPostProcessAaES2::SetShader(const FRenderingCompositePassContext& Context)
{
	Aa_SetShader(Context);
}

void FRCPassPostProcessAaES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessAa);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);
	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	const FPooledRenderTargetDesc& OutputDesc = PassOutputs[0].RenderTargetDesc;

	const FIntPoint& SrcSize = InputDesc->Extent;
	const FIntPoint& DestSize = OutputDesc.Extent;

	FSceneViewState* ViewState = (FSceneViewState*)Context.View.State;
	if (ViewState) 
	{
		// Double buffer input for temporal AA.
		ViewState->MobileAaColor0 = GetInput(ePId_Input0)->GetOutput()->PooledRenderTarget;
	}
	
	check(SrcSize == DestSize);

	ERenderTargetActions LoadStoreAction = ERenderTargetActions::Load_Store;
	//#todo-rv-vr
	if ((!IStereoRendering::IsASecondaryView(Context.View) && IStereoRendering::IsStereoEyeView(Context.View)) ||
		Context.View.Family->Views.Num() == 1)
	{
		// Full clear to avoid restore
		LoadStoreAction = ERenderTargetActions::Clear_Store;
	}

	// The previous frame target has been transitioned to writable in FRenderTargetPool::TransitionTargetsWritable(), so we
	// need to transition it to readable again. Ideally we'll get rid of this useless read->write->read transition when we
	// port this over to RDG.
	const FSceneRenderTargetItem& PrevFrameInput = GetInput(ePId_Input1)->GetOutput()->RequestInput()->GetRenderTargetItem();
	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, PrevFrameInput.ShaderResourceTexture);
	
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, LoadStoreAction);
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("AaES2"));
	{
		Context.SetViewportAndCallRHI(0, 0, 0.0f, DestSize.X, DestSize.Y, 1.0f);

		SetShader(Context);

		const bool bIsFinalPass = Context.IsViewFamilyRenderTarget(DestRenderTarget);

		// If final pass then perform simple upscaling
		const FIntRect& ViewRect = bIsFinalPass ? Context.View.UnscaledViewRect : Context.View.ViewRect;

		float XPos = ViewRect.Min.X;
		float YPos = ViewRect.Min.Y;
		float Width = ViewRect.Width();
		float Height = ViewRect.Height();

		TShaderMapRef<FPostProcessAaVS_ES2> VertexShader(Context.GetShaderMap());

		DrawRectangle(
			Context.RHICmdList,
			XPos, YPos,
			Width, Height,
			XPos, YPos,
			Width, Height,
			DestSize,
			SrcSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());

	if (FSceneRenderer::ShouldCompositeEditorPrimitives(Context.View))
	{
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
		// because of the flush it's ok to remove the const, this is not ideal as the flush can cost performance
		FViewInfo& NonConstView = (FViewInfo&)Context.View;

		// Remove jitter (ensures editor prims are stable.)
		NonConstView.ViewMatrices.HackRemoveTemporalAAProjectionJitter();

		NonConstView.InitRHIResources();
	}
}

FPooledRenderTargetDesc FRCPassPostProcessAaES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	Ret.Depth = 0;
	Ret.ArraySize = 1;
	Ret.bIsArray = false;
	Ret.NumMips = 1;
	Ret.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
	Ret.bForceSeparateTargetAndShaderResource = false;
	Ret.Format = PF_B8G8R8A8;
	Ret.NumSamples = 1;
	Ret.DebugName = TEXT("Aa");
	Ret.Extent = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc.Extent;
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	return Ret;
}

class FClearUAVUIntCS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FClearUAVUIntCS_ES2, Global);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_UAV(RWBuffer<uint>, UAV)
		SHADER_PARAMETER(uint32, ClearValue)
		SHADER_PARAMETER(uint32, NumEntries)
	END_SHADER_PARAMETER_STRUCT()

	FClearUAVUIntCS_ES2() {}
public:

	FClearUAVUIntCS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CLEAR_UAV_UINT_COMPUTE_SHADER"), 1u);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	void SetCS(const FRenderingCompositePassContext& Context, const TShaderRef<FClearUAVUIntCS_ES2>& Shader, uint32 ClearValue, uint32 NumEntries, FRHIUnorderedAccessView* NewUnorderedAccessViewRHI) const
	{
		FParameters ShaderParameters;
		ShaderParameters.ClearValue = ClearValue;
		ShaderParameters.NumEntries = NumEntries;
		ShaderParameters.UAV = NewUnorderedAccessViewRHI;

		SetShaderParameters(Context.RHICmdList, Shader, Shader.GetComputeShader(), ShaderParameters);
	}
};

IMPLEMENT_SHADER_TYPE(, FClearUAVUIntCS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("ClearUAVUIntCS"), SF_Compute);

class FAverageLuminanceVertexBuffer : public FVertexBufferWithSRV
{
public:
	virtual void InitRHI() override
	{
		// Create the texture RHI.  		
		FRHIResourceCreateInfo CreateInfo(TEXT("AverageLuminanceVertexBuffer"));

		VertexBufferRHI = RHICreateVertexBuffer(sizeof(uint32) * 2, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);

		// Create a view of the buffer
		ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_UINT);
		UnorderedAccessViewRHI = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_UINT);
	}
};

FAverageLuminanceVertexBuffer* GAverageLuminanceBuffer = new TGlobalResource<FAverageLuminanceVertexBuffer>;

/** Encapsulates the average luminance compute shader. */
class FPostProcessAverageLuminanceCS_ES2 : public FGlobalShader
{
public:
	// Changing these numbers requires PostProcessMobile.usf to be recompiled.
	static const uint32 ThreadGroupSizeX = 8;
	static const uint32 ThreadGroupSizeY = 8;
	static const uint32 LoopCountX = 2;
	static const uint32 LoopCountY = 2;

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_SHADER_TYPE(FPostProcessAverageLuminanceCS_ES2, Global);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, SourceSizeAndInvSize)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D<half>, InputTexture)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutputUIntBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEX"), LoopCountX);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEY"), LoopCountY);
		OutEnvironment.SetDefine(TEXT("AVERAGE_LUMINANCE_COMPUTE_SHADER"), 1u);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	/** Default constructor. */
	FPostProcessAverageLuminanceCS_ES2() {}

public:

	/** Initialization constructor. */
	FPostProcessAverageLuminanceCS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}


	void SetCS(const FRenderingCompositePassContext& Context, const TShaderRef<FPostProcessAverageLuminanceCS_ES2>& Shader, const FIntPoint& SrcRectExtent, FRHITexture* NewTextureRHI, FRHIUnorderedAccessView* NewUnorderedAccessViewRHI)
	{
		FParameters ShaderParameters;

		ShaderParameters.SourceSizeAndInvSize = FVector4(SrcRectExtent.X, SrcRectExtent.Y, 1.0f / SrcRectExtent.X, 1.0f / SrcRectExtent.Y);
		ShaderParameters.InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		ShaderParameters.InputTexture = NewTextureRHI;
		ShaderParameters.EyeAdaptation = GetEyeAdaptationParameters(Context.View, ERHIFeatureLevel::ES3_1);
		ShaderParameters.OutputUIntBuffer = NewUnorderedAccessViewRHI;

		SetShaderParameters(Context.RHICmdList, Shader, Shader.GetComputeShader(), ShaderParameters);
	}
};

const FIntPoint FPostProcessAverageLuminanceCS_ES2::TexelsPerThreadGroup(ThreadGroupSizeX * LoopCountX * 2, ThreadGroupSizeY * LoopCountY * 2); // Multiply 2 because we use bilinear filter, to reduce the sample count

IMPLEMENT_SHADER_TYPE(, FPostProcessAverageLuminanceCS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("AverageLuminance_MainCS"), SF_Compute);

void FRCPassPostProcessAverageLuminanceES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessAverageLuminanceToSingleTexel);

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	FIntVector DestSize = PassOutputs[0].RenderTargetDesc.GetSize();

	const FSceneRenderTargetItem& InputRenderTarget = GetInput(ePId_Input0)->GetOutput()->PooledRenderTarget->GetRenderTargetItem();

	if (!IsMetalPlatform(Context.View.GetShaderPlatform()) && !IsVulkanPlatform(Context.View.GetShaderPlatform()))
	{
		FRHIRenderPassInfo RPInfo(GSystemTextures.BlackDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::DontLoad_DontStore);

		Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("AverageLuminanceToSingleTexel"));

		Context.RHICmdList.EndRenderPass();
	}
	else
	{
		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, InputRenderTarget.TargetableTexture);
	}

	{
		// clear Average Luminance History
		{
			TShaderMapRef<FClearUAVUIntCS_ES2> ClearShader(Context.GetShaderMap());

			Context.RHICmdList.SetComputeShader(ClearShader.GetComputeShader());

			ClearShader->SetCS(Context, ClearShader, 0, DestSize.X, GAverageLuminanceBuffer->UnorderedAccessViewRHI);

			DispatchComputeShader(Context.RHICmdList, ClearShader, FMath::DivideAndRoundUp<uint32>(DestSize.X, 64), DestSize.Y, 1);

			UnsetShaderUAVs(Context.RHICmdList, ClearShader, Context.RHICmdList.GetBoundComputeShader());
		}

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, GAverageLuminanceBuffer->UnorderedAccessViewRHI);

		{
			const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

			TShaderMapRef<FPostProcessAverageLuminanceCS_ES2> ComputeShader(Context.GetShaderMap());

			Context.RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

			const FIntPoint SrcRectExtent = InputDesc->Extent;

			FIntPoint ThreadGroupCount = FIntPoint::DivideAndRoundUp(SrcRectExtent, FPostProcessAverageLuminanceCS_ES2::TexelsPerThreadGroup);

			ComputeShader->SetCS(Context, ComputeShader, SrcRectExtent, InputRenderTarget.ShaderResourceTexture, GAverageLuminanceBuffer->UnorderedAccessViewRHI);

			DispatchComputeShader(Context.RHICmdList, ComputeShader, ThreadGroupCount.X, ThreadGroupCount.Y, 1);

			UnsetShaderUAVs(Context.RHICmdList, ComputeShader, Context.RHICmdList.GetBoundComputeShader());
		}

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GAverageLuminanceBuffer->UnorderedAccessViewRHI);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessAverageLuminanceES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;

	Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;
	Ret.Reset();

	Ret.Format = PF_R32_UINT;
	Ret.ClearValue = FClearValueBinding::Black;
	Ret.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
	Ret.Flags |= GFastVRamConfig.EyeAdaptation;
	Ret.DebugName = TEXT("AverageLuminance");

	Ret.Extent = FIntPoint(2, 1);

	return Ret;
}

class FBasicEyeAdaptationCS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FBasicEyeAdaptationCS_ES2, Global);
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(Buffer<float4>, EyeAdaptationBuffer)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_SRV(Buffer<uint>, LogLuminanceWeightBuffer)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, OutputBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	/** Default constructor. */
	FBasicEyeAdaptationCS_ES2() {}

	/** Initialization constructor. */
	FBasicEyeAdaptationCS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}

public:

	/** Static Shader boilerplate */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("BASIC_EYEADAPTATION_COMPUTE_SHADER"), 1u);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	void Set(const FRenderingCompositePassContext& Context, const TShaderRef<FBasicEyeAdaptationCS_ES2>& Shader, FRHIShaderResourceView* LastEyeAdaptation, FRHIShaderResourceView* LogLuminanceWeightBuffer, FRHIUnorderedAccessView* TextureUnorderedAccessViewRHI = nullptr)
	{
		FParameters ShaderParameters;

		ShaderParameters.EyeAdaptation = GetEyeAdaptationParameters(Context.View, ERHIFeatureLevel::ES3_1);
		ShaderParameters.View = Context.View.ViewUniformBuffer;

		ShaderParameters.EyeAdaptationBuffer = LastEyeAdaptation;
		
		ShaderParameters.LogLuminanceWeightBuffer = LogLuminanceWeightBuffer;

		ShaderParameters.OutputBuffer = TextureUnorderedAccessViewRHI;

		SetShaderParameters(Context.RHICmdList, Shader, Shader.GetComputeShader(), ShaderParameters);
	}
};

IMPLEMENT_SHADER_TYPE(, FBasicEyeAdaptationCS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("BasicEyeAdaptationCS_ES2"), SF_Compute);

void FRCPassPostProcessBasicEyeAdaptationES2::Process(FRenderingCompositePassContext& Context)
{
	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	// Get the custom 1x1 target used to store exposure value and Toggle the two render targets used to store new and old.
	Context.View.SwapEyeAdaptationBuffers();

	const FExposureBufferData* EyeAdaptationThisFrameBuffer = Context.View.GetEyeAdaptationBuffer();
	const FExposureBufferData* EyeAdaptationLastFrameBuffer = Context.View.GetLastEyeAdaptationBuffer();

	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToCompute, EyeAdaptationLastFrameBuffer->UAV);

	check(EyeAdaptationThisFrameBuffer && EyeAdaptationLastFrameBuffer);

	FShaderResourceViewRHIRef LogLuminanceWeightBuffer = GetInput(ePId_Input0)->IsValid() ? GAverageLuminanceBuffer->ShaderResourceViewRHI : GEmptyVertexBufferWithUAV->ShaderResourceViewRHI;

	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessBasicEyeAdaptationES2);

	{
		TShaderMapRef<FBasicEyeAdaptationCS_ES2> ComputeShader(Context.GetShaderMap());

		Context.RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

		ComputeShader->Set(Context, ComputeShader, EyeAdaptationLastFrameBuffer->SRV, LogLuminanceWeightBuffer, EyeAdaptationThisFrameBuffer->UAV);

		DispatchComputeShader(Context.RHICmdList, ComputeShader, 1, 1, 1);

		UnsetShaderUAVs(Context.RHICmdList, ComputeShader, Context.RHICmdList.GetBoundComputeShader());

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, EyeAdaptationThisFrameBuffer->UAV);
	}

	Context.View.SetValidEyeAdaptation();
}

FPooledRenderTargetDesc FRCPassPostProcessBasicEyeAdaptationES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;

	Ret.DebugName = TEXT("EyeAdaptationBasic");
	Ret.Flags |= GFastVRamConfig.EyeAdaptation;
	return Ret;
}

class FHistogramVertexBuffer : public FVertexBufferWithSRV
{
public:
	virtual void InitRHI() override
	{
		// Create the texture RHI.  		
		FRHIResourceCreateInfo CreateInfo(TEXT("HistogramVertexBuffer"));

		VertexBufferRHI = RHICreateVertexBuffer(sizeof(uint32) * 64, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);

		// Create a view of the buffer
		ShaderResourceViewRHI = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_UINT);
		UnorderedAccessViewRHI = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_UINT);
	}
};

FHistogramVertexBuffer* GHistogramBuffer = new TGlobalResource<FHistogramVertexBuffer>;

/** Encapsulates the post processing histogram compute shader. */
class FPostProcessHistogramCS_ES2 : public FGlobalShader
{
public:
	// Changing these numbers requires PostProcessMobile.usf to be recompiled.
	static const uint32 MetalThreadGroupSizeX = 8;
	static const uint32 MetalThreadGroupSizeY = 4; // the maximum total threadgroup memory allocation on A7 and A8 GPU is 16KB-32B, so it has to limit the thread group size on IOS/TVOS platform.

	static const uint32 MetalLoopCountX = 2;
	static const uint32 MetalLoopCountY = 4;

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint MetalTexelsPerThreadGroup;

	static const uint32 ThreadGroupSizeX = 8;
	static const uint32 ThreadGroupSizeY = 8;

	static const uint32 LoopCountX = 2;
	static const uint32 LoopCountY = 2;

	static const uint32 HistogramSize = 64; // HistogramSize must be 64 and ThreadGroupSizeX * ThreadGroupSizeY must be larger than 32

	// The number of texels on each axis processed by a single thread group.
	static const FIntPoint TexelsPerThreadGroup;

	DECLARE_SHADER_TYPE(FPostProcessHistogramCS_ES2, Global);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector4, SourceSizeAndInvSize)
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_TEXTURE(Texture2D<half>, InputTexture)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, RWHistogramBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		bool bIsMetalMobilePlatform = IsMetalMobilePlatform(Parameters.Platform);

		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), bIsMetalMobilePlatform ? MetalThreadGroupSizeX : ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), bIsMetalMobilePlatform ? MetalThreadGroupSizeY : ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEX"), bIsMetalMobilePlatform ? MetalLoopCountX : LoopCountX);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEY"), bIsMetalMobilePlatform ? MetalLoopCountY : LoopCountY);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_COMPUTE_SHADER"), 1u);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	/** Default constructor. */
	FPostProcessHistogramCS_ES2() {}

public:

	LAYOUT_FIELD(FPostProcessPassParameters, PostprocessParameter);

	/** Initialization constructor. */
	FPostProcessHistogramCS_ES2(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}

	template <typename TRHICmdList>
	void SetCS(TRHICmdList& RHICmdList, const TShaderRef<FPostProcessHistogramCS_ES2>& Shader, const FRenderingCompositePassContext& Context, const FIntPoint& SrcRectExtent, FRHITexture* NewTextureRHI, FRHIUnorderedAccessView* NewUnorderedAccessViewRHI)
	{
		FParameters ShaderParameters;

		ShaderParameters.View = Context.View.ViewUniformBuffer;
		ShaderParameters.SourceSizeAndInvSize = FVector4(SrcRectExtent.X, SrcRectExtent.Y, 1.0f / SrcRectExtent.X, 1.0f / SrcRectExtent.Y);
		ShaderParameters.InputTexture = NewTextureRHI;
		ShaderParameters.InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		ShaderParameters.RWHistogramBuffer = NewUnorderedAccessViewRHI;
		ShaderParameters.EyeAdaptation = GetEyeAdaptationParameters(Context.View, ERHIFeatureLevel::ES3_1);

		SetShaderParameters(RHICmdList, Shader, Shader.GetComputeShader(), ShaderParameters);
	}
};

const FIntPoint FPostProcessHistogramCS_ES2::MetalTexelsPerThreadGroup(MetalThreadGroupSizeX * MetalLoopCountX * 2, MetalThreadGroupSizeY * MetalLoopCountY * 2); // Multiply 2 because we use bilinear filter, to reduce the sample count

const FIntPoint FPostProcessHistogramCS_ES2::TexelsPerThreadGroup(ThreadGroupSizeX * LoopCountX * 2, ThreadGroupSizeY * LoopCountY * 2); // Multiply 2 because we use bilinear filter, to reduce the sample count

IMPLEMENT_SHADER_TYPE(, FPostProcessHistogramCS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("Histogram_MainCS"), SF_Compute);

void FRCPassPostProcessHistogramES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessHistogram);
	
	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	//Histogram Pass

	const FSceneRenderTargetItem& InputRenderTarget = GetInput(ePId_Input0)->GetOutput()->PooledRenderTarget->GetRenderTargetItem();

	FIntVector DestSize = PassOutputs[0].RenderTargetDesc.GetSize();

	if (!IsMetalPlatform(Context.View.GetShaderPlatform()) && !IsVulkanPlatform(Context.View.GetShaderPlatform()))
	{
		FRHIRenderPassInfo RPInfo(GSystemTextures.BlackDummy->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::DontLoad_DontStore);

		Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("Histogram"));

		Context.RHICmdList.EndRenderPass();
	}
	else
	{
		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, InputRenderTarget.TargetableTexture);
	}

	{
		// clear Histogram History
		{
			TShaderMapRef<FClearUAVUIntCS_ES2> ClearShader(Context.GetShaderMap());

			Context.RHICmdList.SetComputeShader(ClearShader.GetComputeShader());

			ClearShader->SetCS(Context, ClearShader, 0, DestSize.X, GHistogramBuffer->UnorderedAccessViewRHI);

			DispatchComputeShader(Context.RHICmdList, ClearShader, FMath::DivideAndRoundUp<uint32>(DestSize.X, 64), DestSize.Y, 1);

			UnsetShaderUAVs(Context.RHICmdList, ClearShader, Context.RHICmdList.GetBoundComputeShader());
		}

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, GHistogramBuffer->UnorderedAccessViewRHI);

		{
			const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);			

			TShaderMapRef<FPostProcessHistogramCS_ES2> ComputeShader(Context.GetShaderMap());

			Context.RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

			const FIntPoint SrcRectExtent = InputDesc->Extent;
			const FIntPoint ThreadGroupCount = FIntPoint::DivideAndRoundUp(SrcRectExtent, IsMetalMobilePlatform(Context.View.GetShaderPlatform()) ? FPostProcessHistogramCS_ES2::MetalTexelsPerThreadGroup : FPostProcessHistogramCS_ES2::TexelsPerThreadGroup);

			ComputeShader->SetCS(Context.RHICmdList, ComputeShader, Context, SrcRectExtent, InputRenderTarget.ShaderResourceTexture, GHistogramBuffer->UnorderedAccessViewRHI);

			DispatchComputeShader(Context.RHICmdList, ComputeShader, ThreadGroupCount.X, ThreadGroupCount.Y, 1);

			UnsetShaderUAVs(Context.RHICmdList, ComputeShader, Context.RHICmdList.GetBoundComputeShader());
		}

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, GHistogramBuffer->UnorderedAccessViewRHI);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessHistogramES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	
	Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;
	Ret.Reset();
	
	Ret.Format = PF_R32_UINT;
	Ret.ClearValue = FClearValueBinding::Black;
	Ret.TargetableFlags = TexCreate_UAV | TexCreate_ShaderResource;
	Ret.Flags |= GFastVRamConfig.Histogram;
	Ret.DebugName = TEXT("Histogram");

	Ret.Extent = FIntPoint(FPostProcessHistogramCS_ES2::HistogramSize, 1);

	return Ret;
}

//////////////////////////////////////////////////////////////////////////
//! Histogram Eye Adaptation
//////////////////////////////////////////////////////////////////////////

class FHistogramEyeAdaptationCS_ES2 : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FHistogramEyeAdaptationCS_ES2, Global);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FEyeAdaptationParameters, EyeAdaptation)
		SHADER_PARAMETER_SRV(Buffer<float4>, EyeAdaptationBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, HistogramBuffer)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, OutputBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return !IsConsolePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_EYEADAPTATION_COMPUTE_SHADER"), 1u);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

public:
	FHistogramEyeAdaptationCS_ES2() = default;
	FHistogramEyeAdaptationCS_ES2(const CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(this, Initializer.PermutationId, Initializer.ParameterMap, *FParameters::FTypeInfo::GetStructMetadata());
	}

	void Set(const FRenderingCompositePassContext& Context, const TShaderRef<FHistogramEyeAdaptationCS_ES2>& Shader, FRHIShaderResourceView* LastEyeAdaptation, FRHIShaderResourceView* HistogramBuffer, FRHIUnorderedAccessView* TextureUnorderedAccessViewRHI = nullptr)
	{
		FParameters ShaderParameters;

		ShaderParameters.EyeAdaptationBuffer = LastEyeAdaptation;

		ShaderParameters.EyeAdaptation = GetEyeAdaptationParameters(Context.View, ERHIFeatureLevel::ES3_1);

		ShaderParameters.HistogramBuffer = HistogramBuffer;

		ShaderParameters.OutputBuffer = TextureUnorderedAccessViewRHI;

		SetShaderParameters(Context.RHICmdList, Shader, Shader.GetComputeShader(), ShaderParameters);
	}
};

IMPLEMENT_SHADER_TYPE(, FHistogramEyeAdaptationCS_ES2, TEXT("/Engine/Private/PostProcessMobile.usf"), TEXT("HistogramEyeAdaptationCS"), SF_Compute);

void FRCPassPostProcessHistogramEyeAdaptationES2::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessHistogramEyeAdaptation);

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	// Get the custom 1x1 target used to store exposure value and Toggle the two render targets used to store new and old.
	Context.View.SwapEyeAdaptationBuffers();

	const FExposureBufferData* EyeAdaptationThisFrameBuffer = Context.View.GetEyeAdaptationBuffer();
	const FExposureBufferData* EyeAdaptationLastFrameBuffer = Context.View.GetLastEyeAdaptationBuffer();

	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EGfxToCompute, EyeAdaptationLastFrameBuffer->UAV);
	
	check(EyeAdaptationThisFrameBuffer && EyeAdaptationLastFrameBuffer);

	FShaderResourceViewRHIRef HistogramBuffer = GetInput(ePId_Input0)->IsValid() ? GHistogramBuffer->ShaderResourceViewRHI : GEmptyVertexBufferWithUAV->ShaderResourceViewRHI;

	{
		TShaderMapRef<FHistogramEyeAdaptationCS_ES2> ComputeShader(Context.GetShaderMap());

		Context.RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

		ComputeShader->Set(Context, ComputeShader, EyeAdaptationLastFrameBuffer->SRV, HistogramBuffer, EyeAdaptationThisFrameBuffer->UAV);

		DispatchComputeShader(Context.RHICmdList, ComputeShader, 1, 1, 1);

		UnsetShaderUAVs(Context.RHICmdList, ComputeShader, Context.RHICmdList.GetBoundComputeShader());

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, EyeAdaptationThisFrameBuffer->UAV);
	}

	Context.View.SetValidEyeAdaptation();
}

FPooledRenderTargetDesc FRCPassPostProcessHistogramEyeAdaptationES2::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;

	Ret.DebugName = TEXT("EyeAdaptationHistogram");
	Ret.Flags |= GFastVRamConfig.Histogram;
	return Ret;
}
