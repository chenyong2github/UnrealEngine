// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessAmbientOcclusion.cpp: Post processing ambient occlusion implementation.
=============================================================================*/

#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "CompositionLighting/CompositionLighting.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"

DECLARE_GPU_STAT_NAMED(SSAOSetup, TEXT("ScreenSpace AO Setup") );
DECLARE_GPU_STAT_NAMED(SSAO, TEXT("ScreenSpace AO") );
DECLARE_GPU_STAT_NAMED(BasePassAO, TEXT("BasePass AO") );
DECLARE_GPU_STAT_NAMED(SSAOSmooth, TEXT("SSAO smooth"));

// Tile size for the AmbientOcclusion compute shader, tweaked for 680 GTX. */
// see GCN Performance Tip 21 http://developer.amd.com/wordpress/media/2013/05/GCNPerformanceTweets.pdf 
const int32 GAmbientOcclusionTileSizeX = 16;
const int32 GAmbientOcclusionTileSizeY = 16;

static TAutoConsoleVariable<int32> CVarAmbientOcclusionCompute(
	TEXT("r.AmbientOcclusion.Compute"),
	0,
	TEXT("If SSAO should use ComputeShader (not available on all platforms) or PixelShader.\n")
	TEXT("The [Async] Compute Shader version is WIP, not optimized, requires hardware support (not mobile/DX10/OpenGL3),\n")
	TEXT("does not use normals which allows it to run right after EarlyZPass (better performance when used with AyncCompute)\n")
	TEXT("AyncCompute is currently only functional on PS4.\n")
	TEXT(" 0: PixelShader (default)\n")
	TEXT(" 1: (WIP) Use ComputeShader if possible, otherwise fall back to '0'\n")
	TEXT(" 2: (WIP) Use AsyncCompute if efficient, otherwise fall back to '1'\n")
	TEXT(" 3: (WIP) Use AsyncCompute if possible, otherwise fall back to '1'"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAmbientOcclusionMaxQuality(
	TEXT("r.AmbientOcclusionMaxQuality"),
	100.0f,
	TEXT("Defines the max clamping value from the post process volume's quality level for ScreenSpace Ambient Occlusion\n")
	TEXT("     100: don't override quality level from the post process volume (default)\n")
	TEXT("   0..99: clamp down quality level from the post process volume to the maximum set by this cvar\n")
	TEXT(" -100..0: Enforces a different quality (the absolute value) even if the postprocessvolume asks for a lower quality."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarAmbientOcclusionStepMipLevelFactor(
	TEXT("r.AmbientOcclusionMipLevelFactor"),
	0.5f,
	TEXT("Controls mipmap level according to the SSAO step id\n")
	TEXT(" 0: always look into the HZB mipmap level 0 (memory cache trashing)\n")
	TEXT(" 0.5: sample count depends on post process settings (default)\n")
	TEXT(" 1: Go into higher mipmap level (quality loss)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAmbientOcclusionLevels(
	TEXT("r.AmbientOcclusionLevels"),
	-1,
	TEXT("Defines how many mip levels are using during the ambient occlusion calculation. This is useful when tweaking the algorithm.\n")
	TEXT("<0: decide based on the quality setting in the postprocess settings/volume and r.AmbientOcclusionMaxQuality (default)\n")
	TEXT(" 0: none (disable AmbientOcclusion)\n")
	TEXT(" 1: one\n")
	TEXT(" 2: two (costs extra performance, soft addition)\n")
	TEXT(" 3: three (larger radius cost less but can flicker)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAmbientOcclusionAsyncComputeBudget(
	TEXT("r.AmbientOcclusion.AsyncComputeBudget"),
	1,
	TEXT("Defines which level of EAsyncComputeBudget to use for balancing AsyncCompute work against Gfx work.\n")
	TEXT("Only matters if the compute version of SSAO is active (requires CS support, enabled by cvar, single pass, no normals)\n")
	TEXT("This is a low level developer tweak to get best performance on hardware that supports AsyncCompute.\n")
	TEXT(" 0: least AsyncCompute\n")
	TEXT(" 1: .. (default)\n")
	TEXT(" 2: .. \n")
	TEXT(" 3: .. \n")
	TEXT(" 4: most AsyncCompute"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAmbientOcclusionDepthBoundsTest(
	TEXT("r.AmbientOcclusion.DepthBoundsTest"),
	1,
	TEXT("Whether to use depth bounds test to cull distant pixels during AO pass. This option is only valid when pixel shader path is used (r.AmbientOcclusion.Compute=0), without upsampling."),
	ECVF_RenderThreadSafe);

float FSSAOHelper::GetAmbientOcclusionQualityRT(const FSceneView& View)
{
	float CVarValue = CVarAmbientOcclusionMaxQuality.GetValueOnRenderThread();

	if (CVarValue < 0)
	{
		return FMath::Clamp(-CVarValue, 0.0f, 100.0f);
	}
	else
	{
		return FMath::Min(CVarValue, View.FinalPostProcessSettings.AmbientOcclusionQuality);
	}
}

int32 FSSAOHelper::GetAmbientOcclusionShaderLevel(const FSceneView& View)
{
	float QualityPercent = GetAmbientOcclusionQualityRT(View);

	return	(QualityPercent > 75.0f) +
		(QualityPercent > 55.0f) +
		(QualityPercent > 25.0f) +
		(QualityPercent > 5.0f);
}

bool FSSAOHelper::IsAmbientOcclusionCompute(const FSceneView& View)
{
	return View.GetFeatureLevel() >= ERHIFeatureLevel::SM5 && CVarAmbientOcclusionCompute.GetValueOnRenderThread() >= 1;
}

int32 FSSAOHelper::GetNumAmbientOcclusionLevels()
{
	return CVarAmbientOcclusionLevels.GetValueOnRenderThread();
}

float FSSAOHelper::GetAmbientOcclusionStepMipLevelFactor()
{
	return CVarAmbientOcclusionStepMipLevelFactor.GetValueOnRenderThread();
}

EAsyncComputeBudget FSSAOHelper::GetAmbientOcclusionAsyncComputeBudget()
{
	int32 RawBudget = CVarAmbientOcclusionAsyncComputeBudget.GetValueOnRenderThread();

	return (EAsyncComputeBudget)FMath::Clamp(RawBudget, (int32)EAsyncComputeBudget::ELeast_0, (int32)EAsyncComputeBudget::EAll_4);
}

bool FSSAOHelper::IsBasePassAmbientOcclusionRequired(const FViewInfo& View)
{
	// the BaseAO pass is only worth with some AO
	return (View.FinalPostProcessSettings.AmbientOcclusionStaticFraction >= 1 / 100.0f) && IsUsingGBuffers(View.GetShaderPlatform());
}

bool FSSAOHelper::IsAmbientOcclusionAsyncCompute(const FViewInfo& View, uint32 AOPassCount)
{
	// if AsyncCompute is feasible
	// only single level is allowed.  more levels end up reading from gbuffer normals atm which is not allowed.
	if(IsAmbientOcclusionCompute(View) && (AOPassCount == 1))
	{
		int32 ComputeCVar = CVarAmbientOcclusionCompute.GetValueOnRenderThread();

		if(ComputeCVar >= 2)
		{
			// we might want AsyncCompute

			if(ComputeCVar == 3)
			{
				// enforced, no matter if efficient hardware support
				return true;
			}

			// depends on efficient hardware support
			return GSupportsEfficientAsyncCompute;
		}
	}

	return false;
}

// @return 0:off, 0..3
uint32 FSSAOHelper::ComputeAmbientOcclusionPassCount(const FViewInfo& View)
{
	// 0:off / 1 / 2 / 3
	uint32 Ret = 0;

	const bool bEnabled = ShouldRenderScreenSpaceAmbientOcclusion(View);

	if (bEnabled)
	{
		int32 CVarLevel = GetNumAmbientOcclusionLevels();

		if (IsAmbientOcclusionCompute(View) || IsForwardShadingEnabled(View.GetShaderPlatform()))
		{	
			if (CVarLevel<0)
			{
				CVarLevel = 1;
			}
			// Compute and forward only support one pass currently.
			return FMath::Min<int32>(CVarLevel, 1);
		}

		// usually in the range 0..100 
		float QualityPercent = GetAmbientOcclusionQualityRT(View);

		// don't expose 0 as the lowest quality should still render
		Ret = 1 +
			(QualityPercent > 70.0f) +
			(QualityPercent > 35.0f);

		if (CVarLevel >= 0)
		{
			// cvar can override (for scalability or to profile/test)
			Ret = CVarLevel;
		}

		// bring into valid range
		Ret = FMath::Min<uint32>(Ret, 3);
	}

	return Ret;
}

/** Shader parameters needed for screen space AmbientOcclusion passes. */
class FScreenSpaceAOParameters
{
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ScreenSpaceAOParams.Bind(ParameterMap, TEXT("ScreenSpaceAOParams"));
	}

	//@param TRHICmdList could be async compute or compute dispatch, so template on commandlist type.
	template<typename ShaderRHIParamRef, typename TRHICmdList>
	void Set(TRHICmdList& RHICmdList, const FViewInfo& View, const ShaderRHIParamRef ShaderRHI, FIntPoint InputTextureSize) const
	{
		const FFinalPostProcessSettings& Settings = View.FinalPostProcessSettings;

		FIntPoint SSAORandomizationSize = GSystemTextures.SSAORandomization->GetDesc().Extent;
		FVector2D ViewportUVToRandomUV(InputTextureSize.X / (float)SSAORandomizationSize.X, InputTextureSize.Y / (float)SSAORandomizationSize.Y);

		// e.g. 4 means the input texture is 4x smaller than the buffer size
		uint32 ScaleToFullRes = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY().X / InputTextureSize.X;

		FIntRect ViewRect = FIntRect::DivideAndRoundUp(View.ViewRect, ScaleToFullRes);

		float AORadiusInShader = Settings.AmbientOcclusionRadius;
		float ScaleRadiusInWorldSpace = 1.0f;

		if(!Settings.AmbientOcclusionRadiusInWS)
		{
			// radius is defined in view space in 400 units
			AORadiusInShader /= 400.0f;
			ScaleRadiusInWorldSpace = 0.0f;
		}

		// /4 is an adjustment for usage with multiple mips
		float f = FMath::Log2(ScaleToFullRes);
		float g = pow(Settings.AmbientOcclusionMipScale, f);
		AORadiusInShader *= pow(Settings.AmbientOcclusionMipScale, FMath::Log2(ScaleToFullRes)) / 4.0f;

		float Ratio = View.UnscaledViewRect.Width() / (float)View.UnscaledViewRect.Height();

		// Grab this and pass into shader so we can negate the fov influence of projection on the screen pos.
		float InvTanHalfFov = View.ViewMatrices.GetProjectionMatrix().M[0][0];

		FVector4 Value[6];

		float StaticFraction = FMath::Clamp(Settings.AmbientOcclusionStaticFraction, 0.0f, 1.0f);

		// clamp to prevent user error
		float FadeRadius = FMath::Max(1.0f, Settings.AmbientOcclusionFadeRadius);
		float InvFadeRadius = 1.0f / FadeRadius;

		FVector2D TemporalOffset(0.0f, 0.0f);
		
		if(View.State)
		{
			TemporalOffset = (View.State->GetCurrentTemporalAASampleIndex() % 8) * FVector2D(2.48f, 7.52f) / 64.0f;
		}
		const float HzbStepMipLevelFactorValue = FMath::Clamp(FSSAOHelper::GetAmbientOcclusionStepMipLevelFactor(), 0.0f, 100.0f);
		const float InvAmbientOcclusionDistance = 1.0f / FMath::Max(Settings.AmbientOcclusionDistance_DEPRECATED, KINDA_SMALL_NUMBER);

		// /1000 to be able to define the value in that distance
		Value[0] = FVector4(Settings.AmbientOcclusionPower, Settings.AmbientOcclusionBias / 1000.0f, InvAmbientOcclusionDistance, Settings.AmbientOcclusionIntensity);
		Value[1] = FVector4(ViewportUVToRandomUV.X, ViewportUVToRandomUV.Y, AORadiusInShader, Ratio);
		Value[2] = FVector4(ScaleToFullRes, Settings.AmbientOcclusionMipThreshold / ScaleToFullRes, ScaleRadiusInWorldSpace, Settings.AmbientOcclusionMipBlend);
		Value[3] = FVector4(TemporalOffset.X, TemporalOffset.Y, StaticFraction, InvTanHalfFov);
		Value[4] = FVector4(InvFadeRadius, -(Settings.AmbientOcclusionFadeDistance - FadeRadius) * InvFadeRadius, HzbStepMipLevelFactorValue, 0);
		Value[5] = FVector4(View.ViewRect.Width(), View.ViewRect.Height(), ViewRect.Min.X, ViewRect.Min.Y);

		SetShaderValueArray(RHICmdList, ShaderRHI, ScreenSpaceAOParams, Value, 6);
	}

	friend FArchive& operator<<(FArchive& Ar, FScreenSpaceAOParameters& This);

private:
	FShaderParameter ScreenSpaceAOParams;
};

/** Encapsulates the post processing ambient occlusion pixel shader. */
template <uint32 bInitialPass>
class FPostProcessAmbientOcclusionSetupPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessAmbientOcclusionSetupPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INITIAL_PASS"), bInitialPass);
	}

	/** Default constructor. */
	FPostProcessAmbientOcclusionSetupPS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FSceneTextureShaderParameters SceneTextureParameters;
	FShaderParameter AmbientOcclusionSetupParams;

	/** Initialization constructor. */
	FPostProcessAmbientOcclusionSetupPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		AmbientOcclusionSetupParams.Bind(Initializer.ParameterMap, TEXT("AmbientOcclusionSetupParams"));
	}

	void SetParameters(const FRenderingCompositePassContext& Context)
	{
		const FFinalPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(Context.RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetPS(Context.RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SceneTextureParameters.Set(Context.RHICmdList, ShaderRHI, Context.View.FeatureLevel, ESceneTextureSetupMode::All);

		// e.g. 4 means the input texture is 4x smaller than the buffer size
		uint32 ScaleToFullRes = FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().X / Context.Pass->GetOutput(ePId_Output0)->RenderTargetDesc.Extent.X;

		// /1000 to be able to define the value in that distance
		FVector4 AmbientOcclusionSetupParamsValue = FVector4(ScaleToFullRes, Settings.AmbientOcclusionMipThreshold / ScaleToFullRes, Context.View.ViewRect.Width(), Context.View.ViewRect.Height());
		SetShaderValue(Context.RHICmdList, ShaderRHI, AmbientOcclusionSetupParams, AmbientOcclusionSetupParamsValue);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << SceneTextureParameters << AmbientOcclusionSetupParams;
		return bShaderHasOutdatedParameters;
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessAmbientOcclusion.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainSetupPS");
	}
};


// #define avoids a lot of code duplication
#define VARIATION1(A) typedef FPostProcessAmbientOcclusionSetupPS<A> FPostProcessAmbientOcclusionSetupPS##A; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessAmbientOcclusionSetupPS##A, SF_Pixel);

	VARIATION1(0)			VARIATION1(1)
#undef VARIATION1

// --------------------------------------------------------
void FRCPassPostProcessAmbientOcclusionSetup::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_GPU_STAT(Context.RHICmdList, SSAOSetup);
	const FViewInfo& View = Context.View;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	FIntPoint DestSize = PassOutputs[0].RenderTargetDesc.Extent;

	// e.g. 4 means the input texture is 4x smaller than the buffer size
	uint32 ScaleFactor = FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY().X / DestSize.X;

	FIntRect SrcRect = View.ViewRect;
	FIntRect DestRect = SrcRect  / ScaleFactor;

	SCOPED_DRAW_EVENTF(Context.RHICmdList, AmbientOcclusionSetup, TEXT("AmbientOcclusionSetup %dx%d"), DestRect.Width(), DestRect.Height());

	// Set the view family's render target/viewport.
	FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::Load_Store);
	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("AmbientOcclusionSetup"));
	{

		Context.SetViewportAndCallRHI(DestRect);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());

		if (IsInitialPass())
		{
			TShaderMapRef<FPostProcessAmbientOcclusionSetupPS<1> > PixelShader(Context.GetShaderMap());

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(Context);
		}
		else
		{
			TShaderMapRef<FPostProcessAmbientOcclusionSetupPS<0> > PixelShader(Context.GetShaderMap());

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(Context);
		}

		VertexShader->SetParameters(Context);
		DrawPostProcessPass(
			Context.RHICmdList,
			0, 0,
			DestRect.Width(), DestRect.Height(),
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DestRect.Size(),
			FSceneRenderTargets::Get(Context.RHICmdList).GetBufferSizeXY(),
			*VertexShader,
			View.StereoPass,
			Context.HasHmdMesh(),
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, FResolveParams());
}

FPooledRenderTargetDesc FRCPassPostProcessAmbientOcclusionSetup::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;
	
	if(IsInitialPass())
	{
		Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;
	}
	else
	{
		Ret = GetInput(ePId_Input1)->GetOutput()->RenderTargetDesc;
	}

	Ret.Reset();
	Ret.Format = PF_FloatRGBA;
	Ret.ClearValue = FClearValueBinding::None;
	Ret.TargetableFlags &= ~TexCreate_DepthStencilTargetable;
	Ret.TargetableFlags |= TexCreate_RenderTargetable;
	Ret.Extent = FIntPoint::DivideAndRoundUp(Ret.Extent, 2);

	Ret.DebugName = TEXT("AmbientOcclusionSetup");
	
	return Ret;
}

bool FRCPassPostProcessAmbientOcclusionSetup::IsInitialPass() const
{
	const FPooledRenderTargetDesc* InputDesc0 = GetInputDesc(ePId_Input0);
	const FPooledRenderTargetDesc* InputDesc1 = GetInputDesc(ePId_Input1);

	if(!InputDesc0 && InputDesc1)
	{
		return false;
	}
	if(InputDesc0 && !InputDesc1)
	{
		return true;
	}
	// internal error, SetInput() was done wrong
	check(0);
	return false;
}

// --------------------------------------------------------

class FPostProcessAmbientOcclusionSmoothCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessAmbientOcclusionSmoothCS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		constexpr int32 ThreadGroupSize1D = FRCPassPostProcessAmbientOcclusionSmooth::ThreadGroupSize1D;

		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSize1D);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSize1D);
	}

	/** Default constructor. */
	FPostProcessAmbientOcclusionSmoothCS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FShaderParameter SSAOSmoothParams;
	FShaderParameter SSAOSmoothResult;

	/** Initialization constructor. */
	FPostProcessAmbientOcclusionSmoothCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SSAOSmoothParams.Bind(Initializer.ParameterMap, TEXT("SSAOSmoothParams"));
		SSAOSmoothResult.Bind(Initializer.ParameterMap, TEXT("SSAOSmoothResult"));
	}

	template <typename TRHICmdList>
	void SetParameters(
		TRHICmdList& RHICmdList,
		const FRenderingCompositePassContext& Context,
		const FIntRect& OutputRect,
		FRHIUnorderedAccessView* OutUAV)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetCS(ShaderRHI, Context, RHICmdList, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		FVector4 SSAOSmoothParamsValue(OutputRect.Min.X, OutputRect.Min.Y, OutputRect.Width(), OutputRect.Height());
		SetShaderValue(RHICmdList, ShaderRHI, SSAOSmoothParams, SSAOSmoothParamsValue);

		RHICmdList.SetUAVParameter(ShaderRHI, SSAOSmoothResult.GetBaseIndex(), OutUAV);
	}

	template <typename TRHICmdList>
	void UnsetParameters(TRHICmdList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();
		RHICmdList.SetUAVParameter(ShaderRHI, SSAOSmoothResult.GetBaseIndex(), nullptr);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << SSAOSmoothParams << SSAOSmoothResult;
		return bShaderHasOutdatedParameters;
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessAmbientOcclusion.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainSSAOSmoothCS");
	}
};

IMPLEMENT_SHADER_TYPE3(FPostProcessAmbientOcclusionSmoothCS, SF_Compute);

FRCPassPostProcessAmbientOcclusionSmooth::FRCPassPostProcessAmbientOcclusionSmooth(ESSAOType InAOType, bool bInDirectOutput)
	: AOType(InAOType)
	, bDirectOutput(bInDirectOutput)
{}

template <typename TRHICmdList>
void FRCPassPostProcessAmbientOcclusionSmooth::DispatchCS(
	TRHICmdList& RHICmdList,
	const FRenderingCompositePassContext& Context,
	const FIntRect& OutputRect,
	FRHIUnorderedAccessView* OutUAV) const
{
	TShaderMapRef<FPostProcessAmbientOcclusionSmoothCS> ComputeShader(Context.GetShaderMap());
	RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
	ComputeShader->SetParameters(RHICmdList, Context, OutputRect, OutUAV);
	const uint32 NumGroupsX = FMath::DivideAndRoundUp(OutputRect.Width(), ThreadGroupSize1D);
	const uint32 NumGroupsY = FMath::DivideAndRoundUp(OutputRect.Height(), ThreadGroupSize1D);
	DispatchComputeShader(RHICmdList, *ComputeShader, NumGroupsX, NumGroupsY, 1);
	ComputeShader->UnsetParameters(RHICmdList);
}

void FRCPassPostProcessAmbientOcclusionSmooth::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_GPU_STAT(Context.RHICmdList, SSAOSmooth);

	UnbindRenderTargets(Context.RHICmdList);
	Context.SetViewportAndCallRHI(Context.View.ViewRect);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
	const FSceneRenderTargetItem& DestRenderTarget = bDirectOutput ? SceneContext.ScreenSpaceAO->GetRenderTargetItem() : PassOutputs[0].RequestSurface(Context);
	const FIntPoint OutputExtent = bDirectOutput ? SceneContext.GetBufferSizeXY() : PassOutputs[0].RenderTargetDesc.Extent;
	const int32 DownSampleFactor = FMath::DivideAndRoundUp(Context.ReferenceBufferSize.X, OutputExtent.X);
	const FIntRect OutputRect = Context.GetViewport() / DownSampleFactor;

	if (AOType == ESSAOType::EAsyncCS)
	{
		FRHIAsyncComputeCommandListImmediate& AsyncComputeCmdList = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
		FComputeFenceRHIRef AsyncStartFence = Context.RHICmdList.CreateComputeFence(TEXT("AsyncStartFence"));

		SCOPED_COMPUTE_EVENTF(AsyncComputeCmdList, SSAOSmooth, TEXT("SSAO smooth %dx%d"), OutputRect.Width(), OutputRect.Height());

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget.UAV, AsyncStartFence);
		AsyncComputeCmdList.WaitComputeFence(AsyncStartFence);
		DispatchCS(AsyncComputeCmdList, Context, OutputRect, DestRenderTarget.UAV);
	}
	else
	{
		check(AOType == ESSAOType::ECS);
		SCOPED_DRAW_EVENTF(Context.RHICmdList, SSAOSmooth, TEXT("SSAO smooth %dx%d"), OutputRect.Width(), OutputRect.Height());

		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget.UAV);
		DispatchCS(Context.RHICmdList, Context, OutputRect, DestRenderTarget.UAV);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessAmbientOcclusionSmooth::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	if (bDirectOutput)
	{
		FPooledRenderTargetDesc Ret;
		Ret.DebugName = TEXT("AmbientOcclusionDirect");
		return Ret;
	}

	const FPooledRenderTargetDesc* Input0Desc = GetInputDesc(ePId_Input0);
	check(Input0Desc);
	FPooledRenderTargetDesc Ret = *Input0Desc;
	Ret.Reset();
	Ret.Format = PF_G8;
	Ret.ClearValue = FClearValueBinding::None;
	Ret.TargetableFlags &= ~TexCreate_DepthStencilTargetable;
	Ret.TargetableFlags |= TexCreate_UAV;
	Ret.DebugName = TEXT("SSAOSmoothResult");
	return Ret;
}

// --------------------------------------------------------

FArchive& operator<<(FArchive& Ar, FScreenSpaceAOParameters& This)
{
	Ar << This.ScreenSpaceAOParams;

	return Ar;
}

// --------------------------------------------------------

/**
 * Encapsulates the post processing ambient occlusion pixel shader.
 * @param bAOSetupAsInput true:use AO setup instead of full resolution depth and normal
 * @param bDoUpsample true:we have lower resolution pass data we need to upsample, false otherwise
 * @param ShaderQuality 0..4, 0:low 4:high
 */
template<uint32 bTAOSetupAsInput, uint32 bDoUpsample, uint32 ShaderQuality, uint32 bComputeShader>
class FPostProcessAmbientOcclusionPSandCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessAmbientOcclusionPSandCS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);

		OutEnvironment.SetDefine(TEXT("USE_UPSAMPLE"), bDoUpsample);
		OutEnvironment.SetDefine(TEXT("USE_AO_SETUP_AS_INPUT"), bTAOSetupAsInput);
		OutEnvironment.SetDefine(TEXT("SHADER_QUALITY"), ShaderQuality);
		OutEnvironment.SetDefine(TEXT("COMPUTE_SHADER"), bComputeShader);

		if(bComputeShader)
		{
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GAmbientOcclusionTileSizeX);
			OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GAmbientOcclusionTileSizeY);
		}
	}

	/** Default constructor. */
	FPostProcessAmbientOcclusionPSandCS() {}

public:
	FShaderParameter HZBRemapping;
	FPostProcessPassParameters PostprocessParameter;
	FSceneTextureShaderParameters SceneTextureParameters;
	FScreenSpaceAOParameters ScreenSpaceAOParams;
	FShaderResourceParameter RandomNormalTexture;
	FShaderResourceParameter RandomNormalTextureSampler;
	FShaderParameter OutTexture;

	/** Initialization constructor. */
	FPostProcessAmbientOcclusionPSandCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		SceneTextureParameters.Bind(Initializer);
		ScreenSpaceAOParams.Bind(Initializer.ParameterMap);
		RandomNormalTexture.Bind(Initializer.ParameterMap, TEXT("RandomNormalTexture"));
		RandomNormalTextureSampler.Bind(Initializer.ParameterMap, TEXT("RandomNormalTextureSampler"));
		HZBRemapping.Bind(Initializer.ParameterMap, TEXT("HZBRemapping"));
		OutTexture.Bind(Initializer.ParameterMap, TEXT("OutTexture"));
	}

	FVector4 GetHZBValue(const FViewInfo& View)
	{
		const FVector2D HZBScaleFactor(
			float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
			float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));

		// from -1..1 to UV 0..1*HZBScaleFactor
		// .xy:mul, zw:add
		const FVector4 HZBRemappingValue(
			0.5f * HZBScaleFactor.X,
			-0.5f * HZBScaleFactor.Y,
			0.5f * HZBScaleFactor.X,
			0.5f * HZBScaleFactor.Y);

		return HZBRemappingValue;
	}
	
	template <typename TRHICmdList>
	void SetParametersCompute(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, FIntPoint InputTextureSize, FRHIUnorderedAccessView* OutUAV)
	{
		const FViewInfo& View = Context.View;
		const FVector4 HZBRemappingValue = GetHZBValue(View);				
		const FSceneRenderTargetItem& SSAORandomization = GSystemTextures.SSAORandomization->GetRenderTargetItem();

		FRHIComputeShader* ShaderRHI = GetComputeShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);			
		RHICmdList.SetUAVParameter(ShaderRHI, OutTexture.GetBaseIndex(), OutUAV);

		// SF_Point is better than bilinear to avoid halos around objects
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);
		PostprocessParameter.SetCS(ShaderRHI, Context, RHICmdList, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		SetTextureParameter(RHICmdList, ShaderRHI, RandomNormalTexture, RandomNormalTextureSampler, TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), SSAORandomization.ShaderResourceTexture);
		ScreenSpaceAOParams.Set(RHICmdList, View, ShaderRHI, InputTextureSize);
		SetShaderValue(RHICmdList, ShaderRHI, HZBRemapping, HZBRemappingValue);			
	}

	
	void SetParametersGfx(FRHICommandList& RHICmdList, const FRenderingCompositePassContext& Context, FIntPoint InputTextureSize, FRHIUnorderedAccessView* OutUAV)
	{
		const FViewInfo& View = Context.View;
		const FVector4 HZBRemappingValue = GetHZBValue(View);
		const FSceneRenderTargetItem& SSAORandomization = GSystemTextures.SSAORandomization->GetRenderTargetItem();

		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		// SF_Point is better than bilinear to avoid halos around objects
		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

		SetTextureParameter(RHICmdList, ShaderRHI, RandomNormalTexture, RandomNormalTextureSampler, TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), SSAORandomization.ShaderResourceTexture);
		ScreenSpaceAOParams.Set(RHICmdList, View, ShaderRHI, InputTextureSize);
		SetShaderValue(RHICmdList, ShaderRHI, HZBRemapping, HZBRemappingValue);
	}

	template <typename TRHICmdList>
	void UnsetParameters(TRHICmdList& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = GetComputeShader();
		RHICmdList.SetUAVParameter(ShaderRHI, OutTexture.GetBaseIndex(), NULL);
	}
	
	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << HZBRemapping << PostprocessParameter << SceneTextureParameters << ScreenSpaceAOParams << RandomNormalTexture << RandomNormalTextureSampler << OutTexture;
		return bShaderHasOutdatedParameters;
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/PostProcessAmbientOcclusion.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return bComputeShader ? TEXT("MainCS") : TEXT("MainPS");
	}
};


// #define avoids a lot of code duplication
#define VARIATION0(C)	    VARIATION1(0, C) VARIATION1(1, C)
#define VARIATION1(A, C)	VARIATION2(A, 0, C) VARIATION2(A, 1, C)
#define VARIATION2(A, B, C) \
	typedef FPostProcessAmbientOcclusionPSandCS<A, B, C, false> FPostProcessAmbientOcclusionPS##A##B##C; \
	typedef FPostProcessAmbientOcclusionPSandCS<A, B, C, true> FPostProcessAmbientOcclusionCS##A##B##C; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessAmbientOcclusionPS##A##B##C, SF_Pixel); \
	IMPLEMENT_SHADER_TYPE2(FPostProcessAmbientOcclusionCS##A##B##C, SF_Compute);

	VARIATION0(0)
	VARIATION0(1)
	VARIATION0(2)
	VARIATION0(3)
	VARIATION0(4)
	
#undef VARIATION0
#undef VARIATION1
#undef VARIATION2

// ---------------------------------

template <uint32 bTAOSetupAsInput, uint32 bDoUpsample, uint32 ShaderQuality>
FShader* FRCPassPostProcessAmbientOcclusion::SetShaderTemplPS(const FRenderingCompositePassContext& Context, FGraphicsPipelineStateInitializer& GraphicsPSOInit)
{
	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessAmbientOcclusionPSandCS<bTAOSetupAsInput, bDoUpsample, ShaderQuality, false> > PixelShader(Context.GetShaderMap());

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	const FPooledRenderTargetDesc* InputDesc0 = GetInputDesc(ePId_Input0);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
	FIntPoint TexSize = InputDesc0 ? InputDesc0->Extent : SceneContext.GetBufferSizeXY();

	VertexShader->SetParameters(Context);
	PixelShader->SetParametersGfx(Context.RHICmdList, Context, TexSize, 0);

	return *VertexShader;
}



template <uint32 bTAOSetupAsInput, uint32 bDoUpsample, uint32 ShaderQuality, typename TRHICmdList>
void FRCPassPostProcessAmbientOcclusion::DispatchCS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, const FIntPoint& TexSize, FRHIUnorderedAccessView* OutUAV)
{
	TShaderMapRef<FPostProcessAmbientOcclusionPSandCS<bTAOSetupAsInput, bDoUpsample, ShaderQuality, true> > ComputeShader(Context.GetShaderMap());

	RHICmdList.SetComputeShader(ComputeShader->GetComputeShader());

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);

	ComputeShader->SetParametersCompute(RHICmdList, Context, TexSize, OutUAV);

	uint32 ScaleToFullRes = SceneContext.GetBufferSizeXY().X / TexSize.X;

	FIntRect ViewRect = FIntRect::DivideAndRoundUp(Context.View.ViewRect, ScaleToFullRes);

	uint32 GroupSizeX = FMath::DivideAndRoundUp(ViewRect.Size().X, GAmbientOcclusionTileSizeX);
	uint32 GroupSizeY = FMath::DivideAndRoundUp(ViewRect.Size().Y, GAmbientOcclusionTileSizeY);
	DispatchComputeShader(RHICmdList, *ComputeShader, GroupSizeX, GroupSizeY, 1);

	ComputeShader->UnsetParameters(RHICmdList);
}



// --------------------------------------------------------

FRCPassPostProcessAmbientOcclusion::FRCPassPostProcessAmbientOcclusion(const FSceneView& View, ESSAOType InAOType, bool bInAOSetupAsInput, bool bInForcecIntermediateOutput, EPixelFormat InIntermediateFormatOverride)
	: AOType(InAOType)
	, IntermediateFormatOverride(InIntermediateFormatOverride)
	, bAOSetupAsInput(bInAOSetupAsInput)
	, bForceIntermediateOutput(bInForcecIntermediateOutput)
{
}

void FRCPassPostProcessAmbientOcclusion::ProcessCS(FRenderingCompositePassContext& Context, const FSceneRenderTargetItem* DestRenderTarget,
	const FIntRect& ViewRect, const FIntPoint& TexSize, int32 ShaderQuality, bool bDoUpsample)
{
#define SET_SHADER_CASE(RHICmdList, ShaderQualityCase) \
	case ShaderQualityCase: \
	if (bAOSetupAsInput) \
	{ \
		if (bDoUpsample) DispatchCS<1, 1, ShaderQualityCase>(RHICmdList, Context, TexSize, DestRenderTarget->UAV); \
		else DispatchCS<1, 0, ShaderQualityCase>(RHICmdList, Context, TexSize, DestRenderTarget->UAV); \
	} \
	else \
	{ \
		if (bDoUpsample) DispatchCS<0, 1, ShaderQualityCase>(RHICmdList, Context, TexSize, DestRenderTarget->UAV); \
		else DispatchCS<0, 0, ShaderQualityCase>(RHICmdList, Context, TexSize, DestRenderTarget->UAV); \
	} \
	break

	// #todo-renderpasses remove once everything is renderpasses
	UnbindRenderTargets(Context.RHICmdList);
	Context.SetViewportAndCallRHI(ViewRect, 0.0f, 1.0f);

	//for async compute we need to set up a fence to make sure the resource is ready before we start.
	if (AOType == ESSAOType::EAsyncCS)
	{
		//Grab the async compute commandlist.
		FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();

		static FName AsyncStartFenceName(TEXT("AsyncStartFence"));
		FComputeFenceRHIRef AsyncStartFence = Context.RHICmdList.CreateComputeFence(AsyncStartFenceName);

		//Fence to let us know when the Gfx pipe is done with the RT we want to write to.
		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget->UAV, AsyncStartFence);

		SCOPED_COMPUTE_EVENT(RHICmdListComputeImmediate, AsyncSSAO);
		//Async compute must wait for Gfx to be done with our dest target before we can dispatch anything.
		RHICmdListComputeImmediate.WaitComputeFence(AsyncStartFence);

		switch (ShaderQuality)
		{
			SET_SHADER_CASE(RHICmdListComputeImmediate, 0);
			SET_SHADER_CASE(RHICmdListComputeImmediate, 1);
			SET_SHADER_CASE(RHICmdListComputeImmediate, 2);
			SET_SHADER_CASE(RHICmdListComputeImmediate, 3);
			SET_SHADER_CASE(RHICmdListComputeImmediate, 4);
		default:
			break;
		};
	}
	else
	{
		//no fence necessary for inline compute.
		Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget->UAV, nullptr);
		switch (ShaderQuality)
		{
			SET_SHADER_CASE(Context.RHICmdList, 0);
			SET_SHADER_CASE(Context.RHICmdList, 1);
			SET_SHADER_CASE(Context.RHICmdList, 2);
			SET_SHADER_CASE(Context.RHICmdList, 3);
			SET_SHADER_CASE(Context.RHICmdList, 4);
		default:
			break;
		};
	}
	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, DestRenderTarget->TargetableTexture);
#undef SET_SHADER_CASE	
}

void FRCPassPostProcessAmbientOcclusion::ProcessPS(FRenderingCompositePassContext& Context,
	const FSceneRenderTargetItem* DestRenderTarget, const FSceneRenderTargetItem* SceneDepthBuffer,
	const FIntRect& ViewRect, const FIntPoint& TexSize, int32 ShaderQuality, bool bDoUpsample)
{
	// We do not support the depth bounds optimization if we are in MSAA. To do so we would have to resolve the depth buffer here OR use a multisample texture for our AO target.
	const bool bDepthBoundsTestEnabled = GSupportsDepthBoundsTest && SceneDepthBuffer && CVarAmbientOcclusionDepthBoundsTest.GetValueOnRenderThread() && SceneDepthBuffer->TargetableTexture->GetNumSamples() == 1;

	// Set the view family's render target/viewport.
	// Rendertarget will be completely overwritten.
	FRHIRenderPassInfo RPInfo(DestRenderTarget->TargetableTexture, ERenderTargetActions::DontLoad_Store);
	if (bDepthBoundsTestEnabled)
	{
		// We'll use the depth/stencil buffer for read but it will not be modified.
		// Note: VK requires us to store stencil or it (may) leave the attachment in an undefined state.
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneDepthBuffer->TargetableTexture;
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_Store);
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;
	}

	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PSAmbientOcclusion"));
	{
		Context.SetViewportAndCallRHI(ViewRect);

		float DepthFar = 0.0f;

		if (bDepthBoundsTestEnabled)
		{
			const FFinalPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
			const FMatrix& ProjectionMatrix = Context.View.ViewMatrices.GetProjectionMatrix();
			const FVector4 Far = ProjectionMatrix.TransformFVector4(FVector4(0, 0, Settings.AmbientOcclusionFadeDistance));
			DepthFar = FMath::Min(1.0f, Far.Z / Far.W);

			static_assert(bool(ERHIZBuffer::IsInverted), "Inverted depth buffer is assumed when setting depth bounds test for AO.");

			// We must clear all pixels that won't be touched by AO shader.
			FClearQuadCallbacks Callbacks;
			Callbacks.PSOModifier = [](FGraphicsPipelineStateInitializer& PSOInitializer)
			{
				PSOInitializer.bDepthBounds = true;
			};
			Callbacks.PreClear = [DepthFar](FRHICommandList& InRHICmdList)
			{
				// This is done by rendering a clear quad over a depth range from AmbientOcclusionFadeDistance to far plane.
				InRHICmdList.SetDepthBounds(0, DepthFar);	// NOTE: Inverted depth
			};
			Callbacks.PostClear = [DepthFar](FRHICommandList& InRHICmdList)
			{
				// Set depth bounds test to cover everything from near plane to AmbientOcclusionFadeDistance and run AO pixel shader.
				InRHICmdList.SetDepthBounds(DepthFar, 1.0f);
			};
			DrawClearQuad(Context.RHICmdList, FLinearColor::White, Callbacks);
		}

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// set the state
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.bDepthBounds = bDepthBoundsTestEnabled;

		FShader* VertexShader = 0;

#define SET_SHADER_CASE(ShaderQualityCase) \
		case ShaderQualityCase: \
	if (bAOSetupAsInput) \
	{ \
		if (bDoUpsample) VertexShader = SetShaderTemplPS<1, 1, ShaderQualityCase>(Context, GraphicsPSOInit); \
		else VertexShader = SetShaderTemplPS<1, 0, ShaderQualityCase>(Context, GraphicsPSOInit); \
	} \
	else \
	{ \
		if (bDoUpsample) VertexShader = SetShaderTemplPS<0, 1, ShaderQualityCase>(Context, GraphicsPSOInit); \
		else VertexShader = SetShaderTemplPS<0, 0, ShaderQualityCase>(Context, GraphicsPSOInit); \
	} \
	break

		switch (ShaderQuality)
		{
			SET_SHADER_CASE(0);
			SET_SHADER_CASE(1);
			SET_SHADER_CASE(2);
			SET_SHADER_CASE(3);
			SET_SHADER_CASE(4);
		default:
			break;
		};
#undef SET_SHADER_CASE

		if (bDepthBoundsTestEnabled)
		{
			Context.RHICmdList.SetDepthBounds(DepthFar, 1.0f);
		}

		// Draw a quad mapping scene color to the view's render target
		DrawRectangle(
			Context.RHICmdList,
			0, 0,
			ViewRect.Width(), ViewRect.Height(),
			ViewRect.Min.X, ViewRect.Min.Y,
			ViewRect.Width(), ViewRect.Height(),
			ViewRect.Size(),
			TexSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();

	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, DestRenderTarget->TargetableTexture);

	if (bDepthBoundsTestEnabled)
	{
		Context.RHICmdList.SetDepthBounds(0, 1.0f);
	}
}

void FRCPassPostProcessAmbientOcclusion::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_GPU_STAT(Context.RHICmdList, SSAO);

	const FViewInfo& View = Context.View;

	const FPooledRenderTargetDesc* InputDesc0 = GetInputDesc(ePId_Input0);
	const FPooledRenderTargetDesc* InputDesc2 = GetInputDesc(ePId_Input2);

	const FSceneRenderTargetItem* DestRenderTarget = 0;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);

	if(bAOSetupAsInput || bForceIntermediateOutput)
	{
		DestRenderTarget = &PassOutputs[0].RequestSurface(Context);
	}
	else
	{
		DestRenderTarget = &SceneContext.ScreenSpaceAO->GetRenderTargetItem();
	}

	// Compute doesn't have Input0, it runs in full resolution
	FIntPoint TexSize = InputDesc0 ? InputDesc0->Extent : SceneContext.GetBufferSizeXY();

	// usually 1, 2, 4 or 8
	uint32 ScaleToFullRes = SceneContext.GetBufferSizeXY().X / TexSize.X;

	FIntRect ViewRect = FIntRect::DivideAndRoundUp(View.ViewRect, ScaleToFullRes);

	// 0..4, 0:low 4:high
	const int32 ShaderQuality = FSSAOHelper::GetAmbientOcclusionShaderLevel(Context.View);

	bool bDoUpsample = (InputDesc2 != 0);
	
	SCOPED_DRAW_EVENTF(Context.RHICmdList, AmbientOcclusion, TEXT("AmbientOcclusion%s %dx%d SetupAsInput=%d Upsample=%d ShaderQuality=%d"), 
		(AOType == ESSAOType::EPS) ? TEXT("PS") : TEXT("CS"), ViewRect.Width(), ViewRect.Height(), bAOSetupAsInput, bDoUpsample, ShaderQuality);

	if (AOType == ESSAOType::EPS)
	{
		const FSceneRenderTargetItem* SceneDepthBuffer = (!bDoUpsample && ScaleToFullRes == 1 && SceneContext.SceneDepthZ)
			? &SceneContext.SceneDepthZ->GetRenderTargetItem()
			: nullptr;

		ProcessPS(Context, DestRenderTarget, SceneDepthBuffer, ViewRect, TexSize, ShaderQuality, bDoUpsample);
	}
	else
	{
		ProcessCS(Context, DestRenderTarget, ViewRect, TexSize, ShaderQuality, bDoUpsample);
	}	
}

FPooledRenderTargetDesc FRCPassPostProcessAmbientOcclusion::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	if(!bAOSetupAsInput && !bForceIntermediateOutput)
	{
		FPooledRenderTargetDesc Ret;

		Ret.DebugName = TEXT("AmbientOcclusionDirect");

		// we render directly to the buffer, no need for an intermediate target, we output in a single channel
		return Ret;
	}

	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	Ret.Reset();
	// R:AmbientOcclusion, GBA:used for normal
	Ret.Format = PF_B8G8R8A8;
	Ret.TargetableFlags &= ~TexCreate_DepthStencilTargetable;
	if((AOType == ESSAOType::ECS) || (AOType == ESSAOType::EAsyncCS))
	{
		Ret.TargetableFlags |= TexCreate_UAV;
		// UAV allowed format
		Ret.Format = PF_FloatRGBA;
	}
	else
	{
		Ret.TargetableFlags |= TexCreate_RenderTargetable;
	}
	Ret.DebugName = TEXT("AmbientOcclusion");

	if (IntermediateFormatOverride != PF_Unknown)
	{
		Ret.Format = IntermediateFormatOverride;
	}

	return Ret;
}
