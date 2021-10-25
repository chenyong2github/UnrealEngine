// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessMaterial.cpp: Post processing Material implementation.
=============================================================================*/

#include "PostProcess/PostProcessMaterial.h"
#include "RendererModule.h"
#include "Materials/Material.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "PostProcess/SceneFilterRendering.h"
#include "SceneRendering.h"
#include "ClearQuad.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "RHI/Public/PipelineStateCache.h"
#include "PostProcessing.h"
#include "PostProcessMobile.h"
#include "BufferVisualizationData.h"
#include "SceneTextureParameters.h"

namespace
{

TAutoConsoleVariable<int32> CVarPostProcessAllowStencilTest(
	TEXT("r.PostProcessAllowStencilTest"),
	1,
	TEXT("Enables stencil testing in post process materials.\n")
	TEXT("0: disable stencil testing\n")
	TEXT("1: allow stencil testing\n")
	);

TAutoConsoleVariable<int32> CVarPostProcessAllowBlendModes(
	TEXT("r.PostProcessAllowBlendModes"),
	1,
	TEXT("Enables blend modes in post process materials.\n")
	TEXT("0: disable blend modes. Uses replace\n")
	TEXT("1: allow blend modes\n")
	);

TAutoConsoleVariable<int32> CVarPostProcessingDisableMaterials(
	TEXT("r.PostProcessing.DisableMaterials"),
	0,
	TEXT(" Allows to disable post process materials. \n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

bool IsPostProcessStencilTestAllowed()
{
	return CVarPostProcessAllowStencilTest.GetValueOnRenderThread() != 0;
}

bool IsCustomDepthEnabled()
{
	static const IConsoleVariable* CVarCustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));

	check(CVarCustomDepth);

	return CVarCustomDepth->GetInt() == 3;
}

enum class ECustomDepthPolicy : uint32
{
	// Custom depth is disabled.
	Disabled,

	// Custom Depth-Stencil is enabled; potentially simultaneous SRV / DSV usage.
	Enabled
};

ECustomDepthPolicy GetMaterialCustomDepthPolicy(const FMaterial* Material, ERHIFeatureLevel::Type FeatureLevel)
{
	check(Material);

	// Material requesting stencil test and post processing CVar allows it.
	if (Material->IsStencilTestEnabled() && IsPostProcessStencilTestAllowed())
	{
		// Custom stencil texture allocated and available.
		if (IsCustomDepthEnabled())
		{
			return ECustomDepthPolicy::Enabled;
		}
		else
		{
			UE_LOG(LogRenderer, Warning, TEXT("PostProcessMaterial uses stencil test, but stencil not allocated. Set r.CustomDepth to 3 to allocate custom stencil."));
		}
	}

	return ECustomDepthPolicy::Disabled;
}

FRHIDepthStencilState* GetMaterialStencilState(const FMaterial* Material)
{
	static FRHIDepthStencilState* StencilStates[] =
	{
		TStaticDepthStencilState<false, CF_Always, true, CF_Less>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_LessEqual>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_Greater>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_GreaterEqual>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_Equal>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_NotEqual>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_Never>::GetRHI(),
		TStaticDepthStencilState<false, CF_Always, true, CF_Always>::GetRHI(),
	};
	static_assert(EMaterialStencilCompare::MSC_Count == UE_ARRAY_COUNT(StencilStates), "Ensure that all EMaterialStencilCompare values are accounted for.");

	check(Material);

	return StencilStates[Material->GetStencilCompare()];
}

bool IsMaterialBlendEnabled(const FMaterial* Material)
{
	check(Material);

	return Material->GetBlendableOutputAlpha() && CVarPostProcessAllowBlendModes.GetValueOnRenderThread() != 0;
}

FRHIBlendState* GetMaterialBlendState(const FMaterial* Material)
{
	static FRHIBlendState* BlendStates[] =
	{
		TStaticBlendState<>::GetRHI(),
		TStaticBlendState<>::GetRHI(),
		TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI(),
		TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI(),
		TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI(),
		TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI(),
		TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI(),
	};
	static_assert(EBlendMode::BLEND_MAX == UE_ARRAY_COUNT(BlendStates), "Ensure that all EBlendMode values are accounted for.");

	check(Material);

	return BlendStates[Material->GetBlendMode()];
}

class FPostProcessMaterialShader : public FMaterialShader
{
public:
	using FParameters = FPostProcessMaterialParameters;
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FPostProcessMaterialShader, FMaterialShader);

	class FMobileDimension : SHADER_PERMUTATION_BOOL("POST_PROCESS_MATERIAL_MOBILE");
	using FPermutationDomain = TShaderPermutationDomain<FMobileDimension>;

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		if (Parameters.MaterialParameters.MaterialDomain == MD_PostProcess)
		{
			const FPermutationDomain PermutationVector(Parameters.PermutationId);

			if (PermutationVector.Get<FMobileDimension>())
			{
				return IsMobilePlatform(Parameters.Platform) && IsMobileHDR();
			}
			else
			{
				return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
			}
		}
		return false;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);

		EBlendableLocation Location = EBlendableLocation(Parameters.MaterialParameters.BlendableLocation);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Location == BL_AfterTonemapping || Location == BL_ReplacingTonemapper) ? 0 : 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FMobileDimension>())
		{
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.MaterialParameters.BlendableLocation != BL_AfterTonemapping) ? 1 : 0);
		}
	}

protected:
	template <typename TRHIShader>
	static void SetParameters(FRHICommandList& RHICmdList, const TShaderRef<FPostProcessMaterialShader> & Shader, TRHIShader* ShaderRHI, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FMaterial& Material, const FParameters& Parameters)
	{
		FMaterialShader* MaterialShader = Shader.GetShader();
		MaterialShader->SetParameters(RHICmdList, ShaderRHI, Proxy, Material, View);
		SetShaderParameters(RHICmdList, Shader, ShaderRHI, Parameters);
	}
};

class FPostProcessMaterialVS : public FPostProcessMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FPostProcessMaterialVS, Material);

	static void SetParameters(FRHICommandList& RHICmdList, const TShaderRef<FPostProcessMaterialVS>& Shader, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FMaterial& Material, const FParameters& Parameters)
	{
		FPostProcessMaterialShader::SetParameters(RHICmdList, Shader, Shader.GetVertexShader(), View, Proxy, Material, Parameters);
	}

	FPostProcessMaterialVS() = default;
	FPostProcessMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessMaterialShader(Initializer)
	{}
};

class FPostProcessMaterialPS : public FPostProcessMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FPostProcessMaterialPS, Material);

	static void SetParameters(FRHICommandList& RHICmdList, const TShaderRef<FPostProcessMaterialPS>& Shader, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FMaterial& Material, const FParameters& Parameters)
	{
		FPostProcessMaterialShader::SetParameters(RHICmdList, Shader, Shader.GetPixelShader(), View, Proxy, Material, Parameters);
	}

	FPostProcessMaterialPS() = default;
	FPostProcessMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessMaterialShader(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		uint32 StencilCompareFunction = Parameters.MaterialParameters.bIsStencilTestEnabled ? Parameters.MaterialParameters.StencilCompare : EMaterialStencilCompare::MSC_Never;

		OutEnvironment.SetDefine(TEXT("MOBILE_STENCIL_COMPARE_FUNCTION"), StencilCompareFunction);
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessMaterialVS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(,FPostProcessMaterialPS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS"), SF_Pixel);

class FPostProcessMaterialVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FFilterVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FFilterVertex, Position), VET_Float4, 0, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

void GetMaterialInfo(
	const UMaterialInterface* InMaterialInterface,
	ERHIFeatureLevel::Type InFeatureLevel,
	EPixelFormat InOutputFormat,
	const FMaterial*& OutMaterial,
	const FMaterialRenderProxy*& OutMaterialProxy,
	const FMaterialShaderMap*& OutMaterialShaderMap,
	TShaderRef<FPostProcessMaterialVS>& OutVertexShader,
	TShaderRef<FPostProcessMaterialPS>& OutPixelShader)
{
	FMaterialShaderTypes ShaderTypes;
	{
		const bool bIsMobile = InFeatureLevel <= ERHIFeatureLevel::ES3_1;
		FPostProcessMaterialShader::FPermutationDomain PermutationVector;
		PermutationVector.Set<FPostProcessMaterialShader::FMobileDimension>(bIsMobile);

		ShaderTypes.AddShaderType< FPostProcessMaterialVS>(PermutationVector.ToDimensionValueId());
		ShaderTypes.AddShaderType< FPostProcessMaterialPS>(PermutationVector.ToDimensionValueId());
	}

	const FMaterialRenderProxy* MaterialProxy = InMaterialInterface->GetRenderProxy();
	check(MaterialProxy);

	const FMaterial* Material = nullptr;
	FMaterialShaders Shaders;
	while (MaterialProxy)
	{
		Material = MaterialProxy->GetMaterialNoFallback(InFeatureLevel);
		if (Material && Material->GetMaterialDomain() == MD_PostProcess)
		{
			if (Material->TryGetShaders(ShaderTypes, nullptr, Shaders))
			{
				break;
			}
		}
		MaterialProxy = MaterialProxy->GetFallback(InFeatureLevel);
	}

	check(Material);

	if (Material->IsStencilTestEnabled() || Material->GetBlendableOutputAlpha())
	{
		// Only allowed to have blend/stencil test if output format is compatible with ePId_Input0. 
		// PF_Unknown implies output format is that of EPId_Input0
		ensure(InOutputFormat == PF_Unknown);
	}

	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();
	check(MaterialShaderMap);

	OutMaterial = Material;
	OutMaterialProxy = MaterialProxy;
	OutMaterialShaderMap = MaterialShaderMap;
	Shaders.TryGetVertexShader(OutVertexShader);
	Shaders.TryGetPixelShader(OutPixelShader);
}

TGlobalResource<FPostProcessMaterialVertexDeclaration> GPostProcessMaterialVertexDeclaration;

} //! namespace

static void AddCopyAndFlipTexturePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SrcTexture, FRDGTextureRef DestTexture)
{
	if (IsOpenGLPlatform(GShaderPlatformForFeatureLevel[View.GetFeatureLevel()]))
	{
		// The OpenGL RHI can copy and flip at the same time by using an upside down destination rectangle.
		FResolveParams ResolveParams;
		FMemory::Memzero(ResolveParams);
		ResolveParams.Rect.X1 = 0;
		ResolveParams.Rect.X2 = SrcTexture->Desc.Extent.X;
		ResolveParams.Rect.Y1 = 0;
		ResolveParams.Rect.Y2 = SrcTexture->Desc.Extent.Y;
		ResolveParams.DestRect.X1 = 0;
		ResolveParams.DestRect.X2 = DestTexture->Desc.Extent.X;
		ResolveParams.DestRect.Y1 = DestTexture->Desc.Extent.Y - 1;
		ResolveParams.DestRect.Y2 = -1;
		AddCopyToResolveTargetPass(GraphBuilder, SrcTexture, DestTexture, ResolveParams);
		return;
	}

	// Other RHIs can't flip and copy at the same time, so we'll use a pixel shader to perform the copy, together with the FlipYAxis
	// flag on the screen pass. This path is only taken when using the mobile preview feature in the editor with
	// r.Mobile.ForceRHISwitchVerticalAxis set to 1, so we don't care about it being sub-optimal.
	FIntPoint Size = SrcTexture->Desc.Extent;
	const FScreenPassTextureViewport InputViewport(SrcTexture->Desc.Extent, FIntRect(FIntPoint::ZeroValue, Size));
	const FScreenPassTextureViewport OutputViewport(DestTexture->Desc.Extent, FIntRect(FIntPoint::ZeroValue, Size));
	TShaderMapRef<FCopyRectPS> PixelShader(View.ShaderMap);

	FCopyRectPS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
	Parameters->InputTexture = SrcTexture;
	Parameters->InputSampler = TStaticSamplerState<>::GetRHI();
	Parameters->RenderTargets[0] = FRenderTargetBinding(DestTexture, ERenderTargetLoadAction::ENoAction);

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DrawTexture"), View, OutputViewport, InputViewport, PixelShader, Parameters, EScreenPassDrawFlags::FlipYAxis);
}

void AddMobileMSAADecodeAndDrawTexturePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FScreenPassTexture Input,
	FScreenPassRenderTarget Output)
{
	const FScreenPassTextureViewport InputViewport(Input);
	const FScreenPassTextureViewport OutputViewport(Output);

	TShaderMapRef<FMSAADecodeAndCopyRectPS_Mobile> PixelShader(View.ShaderMap);

	FMSAADecodeAndCopyRectPS_Mobile::FParameters* Parameters = GraphBuilder.AllocParameters<FMSAADecodeAndCopyRectPS_Mobile::FParameters>();
	Parameters->InputTexture = Input.Texture;
	Parameters->InputSampler = TStaticSamplerState<>::GetRHI();
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("MobileMSAADecodeAndDrawTexture"), View, OutputViewport, InputViewport, PixelShader, Parameters);
}

FScreenPassTexture AddPostProcessMaterialPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FPostProcessMaterialInputs& Inputs,
	const UMaterialInterface* MaterialInterface)
{
	Inputs.Validate();

	const FScreenPassTexture SceneColor = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);

	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();

	const FMaterial* Material = nullptr;
	const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	const FMaterialShaderMap* MaterialShaderMap = nullptr;
	TShaderRef<FPostProcessMaterialVS> VertexShader;
	TShaderRef<FPostProcessMaterialPS> PixelShader;
	GetMaterialInfo(MaterialInterface, FeatureLevel, Inputs.OutputFormat, Material, MaterialRenderProxy, MaterialShaderMap, VertexShader, PixelShader);

	FRHIDepthStencilState* DefaultDepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();
	FRHIDepthStencilState* DepthStencilState = DefaultDepthStencilState;

	FRDGTextureRef DepthStencilTexture = nullptr;

	// Allocate custom depth stencil texture(s) and depth stencil state.
	const ECustomDepthPolicy CustomStencilPolicy = GetMaterialCustomDepthPolicy(Material, FeatureLevel);

	if (CustomStencilPolicy == ECustomDepthPolicy::Enabled)
	{
		check(Inputs.CustomDepthTexture);
		DepthStencilTexture = Inputs.CustomDepthTexture;
		DepthStencilState = GetMaterialStencilState(Material);
	}

	FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
	FRHIBlendState* BlendState = DefaultBlendState;
	
	if (IsMaterialBlendEnabled(Material))
	{
		BlendState = GetMaterialBlendState(Material);
	}

	// Blend / Depth Stencil usage requires that the render target have primed color data.
	const bool bCompositeWithInput = DepthStencilState != DefaultDepthStencilState || BlendState != DefaultBlendState;

	// We only prime color on the output texture if we are using fixed function Blend / Depth-Stencil,
	// or we need to retain previously rendered views.
	const bool bPrimeOutputColor = bCompositeWithInput || !View.IsFirstInFamily();

	// Inputs.OverrideOutput is used to force drawing directly to the backbuffer. OpenGL doesn't support using the backbuffer color target with a custom depth/stencil
	// buffer, so in that case we must draw to an intermediate target and copy to the backbuffer at the end. Ideally, we would test if Inputs.OverrideOutput.Texture
	// is actually the backbuffer (as returned by AndroidEGL::GetOnScreenColorRenderBuffer() and such), but it's not worth doing all the plumbing and increasing the
	// RHI surface area just for this hack.
	const bool bBackbufferWithDepthStencil = (DepthStencilTexture != nullptr && !GRHISupportsBackBufferWithCustomDepthStencil && Inputs.OverrideOutput.IsValid());

	// The other case when we must render to an intermediate target is when we have to flip the image vertically because we're the last postprocess pass on mobile OpenGL.
	// We can't simply output a flipped image, because the parts of the input image which show through the stencil mask or are blended in must also be flipped. In that case,
	// we render normally to the intermediate target and flip the image when we copy to the output target.
	const bool bCompositeWithInputAndFlipY = bCompositeWithInput && Inputs.bFlipYAxis;

	// We need to decode the target color for blending material, force it rendering to an intermediate render target and decode the color.
	const bool bCompositeWithInputAndDecode = Inputs.bMetalMSAAHDRDecode && bCompositeWithInput;

	const bool bForceIntermediateTarget = bBackbufferWithDepthStencil || bCompositeWithInputAndFlipY || bCompositeWithInputAndDecode;

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	// We can re-use the scene color texture as the render target if we're not simultaneously reading from it.
	// This is only necessary to do if we're going to be priming content from the render target since it avoids
	// the copy. Otherwise, we just allocate a new render target.
	if (!Output.IsValid() && !MaterialShaderMap->UsesSceneTexture(PPI_PostProcessInput0) && bPrimeOutputColor && !bForceIntermediateTarget && Inputs.bAllowSceneColorInputAsOutput && GMaxRHIShaderPlatform != SP_PCD3D_ES3_1)
	{
		Output = FScreenPassRenderTarget(SceneColor, ERenderTargetLoadAction::ELoad);
	}
	else
	{
		// Allocate new transient output texture if none exists.
		if (!Output.IsValid() || bForceIntermediateTarget)
		{
			FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
			OutputDesc.Reset();
			if (Inputs.OutputFormat != PF_Unknown)
			{
				OutputDesc.Format = Inputs.OutputFormat;
			}
			OutputDesc.ClearValue = FClearValueBinding(FLinearColor::Black);
			OutputDesc.Flags |= GFastVRamConfig.PostProcessMaterial;

			Output = FScreenPassRenderTarget(GraphBuilder.CreateTexture(OutputDesc, TEXT("PostProcessMaterial")), SceneColor.ViewRect, View.GetOverwriteLoadAction());
		}

		if (bPrimeOutputColor || bForceIntermediateTarget)
		{
			// Copy existing contents to new output and use load-action to preserve untouched pixels.
			if (Inputs.bMetalMSAAHDRDecode)
			{
				AddMobileMSAADecodeAndDrawTexturePass(GraphBuilder, View, SceneColor, Output);
			}
			else
			{
				AddDrawTexturePass(GraphBuilder, View, SceneColor, Output);
			}
			Output.LoadAction = ERenderTargetLoadAction::ELoad;
		}
	}

	const FScreenPassTextureViewport SceneColorViewport(SceneColor);
	const FScreenPassTextureViewport OutputViewport(Output);

	RDG_EVENT_SCOPE(GraphBuilder, "PostProcessMaterial %dx%d Material=%s", SceneColorViewport.Rect.Width(), SceneColorViewport.Rect.Height(), *Material->GetFriendlyName());

	const uint32 MaterialStencilRef = Material->GetStencilRefValue();

	const bool bMobilePlatform = IsMobilePlatform(View.GetShaderPlatform());

	FPostProcessMaterialParameters* PostProcessMaterialParameters = GraphBuilder.AllocParameters<FPostProcessMaterialParameters>();
	PostProcessMaterialParameters->SceneTextures = Inputs.SceneTextures;
	PostProcessMaterialParameters->View = View.ViewUniformBuffer;
	if (bMobilePlatform)
	{
		PostProcessMaterialParameters->EyeAdaptationBuffer = GetEyeAdaptationBuffer(View);
	}
	else
	{
		PostProcessMaterialParameters->EyeAdaptationTexture = GetEyeAdaptationTexture(GraphBuilder, View);
	}
	PostProcessMaterialParameters->PostProcessOutput = GetScreenPassTextureViewportParameters(OutputViewport);
	PostProcessMaterialParameters->MobileCustomStencilTexture = DepthStencilTexture;
	PostProcessMaterialParameters->MobileCustomStencilTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PostProcessMaterialParameters->MobileStencilValueRef = MaterialStencilRef;
	PostProcessMaterialParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// The target color will be decoded if bForceIntermediateTarget is true in any case, but we might still need to decode the input color
	PostProcessMaterialParameters->bMetalMSAAHDRDecode = Inputs.bMetalMSAAHDRDecode ? 1 : 0;

	if (DepthStencilTexture && !bMobilePlatform)
	{
		PostProcessMaterialParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthStencilTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthRead_StencilRead);
	}
	else if (!DepthStencilTexture && bMobilePlatform && Material->IsStencilTestEnabled()) // we have to set a default texture for MobileStencilTexture and override the MobileStencilValueRef to make all function to pass the stencil test
	{
		PostProcessMaterialParameters->MobileCustomStencilTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
		
		switch (Material->GetStencilCompare())
		{
		case EMaterialStencilCompare::MSC_Less:
			PostProcessMaterialParameters->MobileStencilValueRef = -1;
			break;
		case EMaterialStencilCompare::MSC_LessEqual:
		case EMaterialStencilCompare::MSC_GreaterEqual:
		case EMaterialStencilCompare::MSC_Equal:
			PostProcessMaterialParameters->MobileStencilValueRef = 0;
			break;
		case EMaterialStencilCompare::MSC_Greater:
		case EMaterialStencilCompare::MSC_NotEqual:
			PostProcessMaterialParameters->MobileStencilValueRef = 1;
			break;
		case EMaterialStencilCompare::MSC_Always:
			PostProcessMaterialParameters->MobileStencilValueRef = 256;
			break;
		default:
			break;
		}
	}

	PostProcessMaterialParameters->PostProcessInput_BilinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();;

	const FScreenPassTexture BlackDummy(GSystemTextures.GetBlackDummy(GraphBuilder));

    // This gets passed in whether or not it's used.
	GraphBuilder.RemoveUnusedTextureWarning(BlackDummy.Texture);

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	for (uint32 InputIndex = 0; InputIndex < kPostProcessMaterialInputCountMax; ++InputIndex)
	{
		FScreenPassTexture Input = Inputs.GetInput((EPostProcessMaterialInput)InputIndex);

		// Need to provide valid textures for when shader compilation doesn't cull unused parameters.
		if (!Input.Texture || !MaterialShaderMap->UsesSceneTexture(PPI_PostProcessInput0 + InputIndex))
		{
			Input = BlackDummy;
		}

		PostProcessMaterialParameters->PostProcessInput[InputIndex] = GetScreenPassTextureInput(Input, PointClampSampler);
	}

	PostProcessMaterialParameters->bFlipYAxis = Inputs.bFlipYAxis && !bForceIntermediateTarget;

	ClearUnusedGraphResources(VertexShader, PixelShader, PostProcessMaterialParameters);

	EScreenPassDrawFlags ScreenPassFlags = EScreenPassDrawFlags::AllowHMDHiddenAreaMask;

	if (PostProcessMaterialParameters->bFlipYAxis)
	{
		ScreenPassFlags |= EScreenPassDrawFlags::FlipYAxis;
	}

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("PostProcessMaterial"),
		View,
		OutputViewport,
		SceneColorViewport,
		FScreenPassPipelineState(VertexShader, PixelShader, BlendState, DepthStencilState),
		PostProcessMaterialParameters,
		ScreenPassFlags,
		[&View, VertexShader, PixelShader, MaterialRenderProxy, Material, PostProcessMaterialParameters, MaterialStencilRef](FRHICommandListImmediate& RHICmdList)
	{
		FPostProcessMaterialVS::SetParameters(RHICmdList, VertexShader, View, MaterialRenderProxy, *Material, *PostProcessMaterialParameters);
		FPostProcessMaterialPS::SetParameters(RHICmdList, PixelShader, View, MaterialRenderProxy, *Material, *PostProcessMaterialParameters);
		RHICmdList.SetStencilRef(MaterialStencilRef);
	});

	if (bForceIntermediateTarget && !bCompositeWithInputAndDecode)
	{
		if (!Inputs.bFlipYAxis)
		{
			// We shouldn't get here unless we had an override target.
			check(Inputs.OverrideOutput.IsValid());
			AddDrawTexturePass(GraphBuilder, View, Output.Texture, Inputs.OverrideOutput.Texture);
			Output = Inputs.OverrideOutput;
		}
		else
		{
			FScreenPassRenderTarget TempTarget = Output;
			if (Inputs.OverrideOutput.IsValid())
			{
				Output = Inputs.OverrideOutput;
			}
			else
			{
				Output = FScreenPassRenderTarget(SceneColor, ERenderTargetLoadAction::ENoAction);
			}

			AddCopyAndFlipTexturePass(GraphBuilder, View, TempTarget.Texture, Output.Texture);
		}
	}

	return MoveTemp(Output);
}

static bool IsPostProcessMaterialsEnabledForView(const FViewInfo& View)
{
	if (!View.Family->EngineShowFlags.PostProcessing ||
		!View.Family->EngineShowFlags.PostProcessMaterial ||
		View.Family->EngineShowFlags.VisualizeShadingModels ||
		CVarPostProcessingDisableMaterials.GetValueOnRenderThread() != 0)
	{
		return false;
	}

	return true;
}

static FPostProcessMaterialNode* IteratePostProcessMaterialNodes(const FFinalPostProcessSettings& Dest, EBlendableLocation Location, FBlendableEntry*& Iterator)
{
	for (;;)
	{
		FPostProcessMaterialNode* DataPtr = Dest.BlendableManager.IterateBlendables<FPostProcessMaterialNode>(Iterator);

		if (!DataPtr || DataPtr->GetLocation() == Location)
		{
			return DataPtr;
		}
	}
}

FPostProcessMaterialChain GetPostProcessMaterialChain(const FViewInfo& View, EBlendableLocation Location)
{
	if (!IsPostProcessMaterialsEnabledForView(View))
	{
		return {};
	}

	const FSceneViewFamily& ViewFamily = *View.Family;

	TArray<FPostProcessMaterialNode, TInlineAllocator<10>> Nodes;
	FBlendableEntry* Iterator = nullptr;

	if (ViewFamily.EngineShowFlags.VisualizeBuffer)
	{
		UMaterialInterface* VisMaterial = GetBufferVisualizationData().GetMaterial(View.CurrentBufferVisualizationMode);
		UMaterial* Material = VisMaterial ? VisMaterial->GetMaterial() : nullptr;

		if (Material && Material->BlendableLocation == Location)
		{
			Nodes.Add(FPostProcessMaterialNode(Material, Location, Material->BlendablePriority, Material->bIsBlendable));
		}
	}

	while (FPostProcessMaterialNode* Data = IteratePostProcessMaterialNodes(View.FinalPostProcessSettings, Location, Iterator))
	{
		check(Data->GetMaterialInterface());
		Nodes.Add(*Data);
	}

	if (!Nodes.Num())
	{
		return {};
	}

	::Sort(Nodes.GetData(), Nodes.Num(), FPostProcessMaterialNode::FCompare());

	FPostProcessMaterialChain OutputChain;
	OutputChain.Reserve(Nodes.Num());

	for (const FPostProcessMaterialNode& Node : Nodes)
	{
		OutputChain.Add(Node.GetMaterialInterface());
	}

	return OutputChain;
}

FScreenPassTexture AddPostProcessMaterialChain(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FPostProcessMaterialInputs& InputsTemplate,
	const FPostProcessMaterialChain& Materials)
{
	FScreenPassTexture Outputs = InputsTemplate.GetInput(EPostProcessMaterialInput::SceneColor);

	bool bFirstMaterialInChain = true;
	for (const UMaterialInterface* MaterialInterface : Materials)
	{
		FPostProcessMaterialInputs Inputs = InputsTemplate;
		Inputs.SetInput(EPostProcessMaterialInput::SceneColor, Outputs);
		
		// Only the first material in the chain needs to decode the input color
		Inputs.bMetalMSAAHDRDecode = Inputs.bMetalMSAAHDRDecode && bFirstMaterialInChain;
		bFirstMaterialInChain = false;

		// Certain inputs are only respected by the final post process material in the chain.
		if (MaterialInterface != Materials.Last())
		{
			Inputs.OverrideOutput = FScreenPassRenderTarget();
			Inputs.bFlipYAxis = false;
		}

		Outputs = AddPostProcessMaterialPass(GraphBuilder, View, Inputs, MaterialInterface);
	}

	return Outputs;
}

extern void AddDumpToColorArrayPass(FRDGBuilder& GraphBuilder, FScreenPassTexture Input, TArray<FColor>* OutputColorArray);

bool IsHighResolutionScreenshotMaskEnabled(const FViewInfo& View)
{
	return View.Family->EngineShowFlags.HighResScreenshotMask || View.FinalPostProcessSettings.HighResScreenshotCaptureRegionMaterial;
}

FScreenPassTexture AddHighResolutionScreenshotMaskPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHighResolutionScreenshotMaskInputs& Inputs)
{
	check(Inputs.Material || Inputs.MaskMaterial || Inputs.CaptureRegionMaterial);

	enum class EPass
	{
		Material,
		MaskMaterial,
		CaptureRegionMaterial,
		MAX
	};

	const TCHAR* PassNames[]
	{
		TEXT("Material"),
		TEXT("MaskMaterial"),
		TEXT("CaptureRegionMaterial")
	};

	static_assert(UE_ARRAY_COUNT(PassNames) == static_cast<uint32>(EPass::MAX), "Pass names array doesn't match pass enum");

	const bool bHighResScreenshotMask = View.Family->EngineShowFlags.HighResScreenshotMask != 0;

	TOverridePassSequence<EPass> PassSequence(Inputs.OverrideOutput);
	PassSequence.SetEnabled(EPass::Material, bHighResScreenshotMask && Inputs.Material != nullptr);
	PassSequence.SetEnabled(EPass::MaskMaterial, bHighResScreenshotMask && Inputs.MaskMaterial != nullptr && GIsHighResScreenshot);
	PassSequence.SetEnabled(EPass::CaptureRegionMaterial, Inputs.CaptureRegionMaterial != nullptr);
	PassSequence.Finalize();

	FScreenPassTexture Output = Inputs.SceneColor;

	if (PassSequence.IsEnabled(EPass::Material))
	{
		FPostProcessMaterialInputs PassInputs;
		PassSequence.AcceptOverrideIfLastPass(EPass::Material, PassInputs.OverrideOutput);
		PassInputs.SetInput(EPostProcessMaterialInput::SceneColor, Output);
		PassInputs.SceneTextures = Inputs.SceneTextures;

		Output = AddPostProcessMaterialPass(GraphBuilder, View, PassInputs, Inputs.Material);
	}

	if (PassSequence.IsEnabled(EPass::MaskMaterial))
	{
		PassSequence.AcceptPass(EPass::MaskMaterial);

		FPostProcessMaterialInputs PassInputs;
		PassInputs.SetInput(EPostProcessMaterialInput::SceneColor, Output);
		PassInputs.SceneTextures = Inputs.SceneTextures;

		// Explicitly allocate the render target to match the FSceneView extents and rect, so the output pixel arrangement matches
		FRDGTextureDesc MaskOutputDesc = Output.Texture->Desc;
		MaskOutputDesc.Reset();
		MaskOutputDesc.ClearValue = FClearValueBinding(FLinearColor::Black);
		MaskOutputDesc.Flags |= GFastVRamConfig.PostProcessMaterial;
		MaskOutputDesc.Extent = View.UnconstrainedViewRect.Size();

		PassInputs.OverrideOutput = FScreenPassRenderTarget(
			GraphBuilder.CreateTexture(MaskOutputDesc, TEXT("PostProcessMaterial")), View.UnscaledViewRect, View.GetOverwriteLoadAction());

		// Disallow the scene color input as output optimization since we need to not pollute the scene texture.
		PassInputs.bAllowSceneColorInputAsOutput = false;

		FScreenPassTexture MaskOutput = AddPostProcessMaterialPass(GraphBuilder, View, PassInputs, Inputs.MaskMaterial);
		AddDumpToColorArrayPass(GraphBuilder, MaskOutput, FScreenshotRequest::GetHighresScreenshotMaskColorArray());

		// The mask material pass is actually outputting to system memory. If we're the last pass in the chain
		// and the override output is valid, we need to perform a copy of the input to the output. Since we can't
		// sample from the override output (since it might be the backbuffer), we still need to participate in
		// the pass sequence.
		if (PassSequence.IsLastPass(EPass::MaskMaterial) && Inputs.OverrideOutput.IsValid())
		{
			AddDrawTexturePass(GraphBuilder, View, Output, Inputs.OverrideOutput);
			Output = Inputs.OverrideOutput;
		}
	}

	if (PassSequence.IsEnabled(EPass::CaptureRegionMaterial))
	{
		FPostProcessMaterialInputs PassInputs;
		PassSequence.AcceptOverrideIfLastPass(EPass::CaptureRegionMaterial, PassInputs.OverrideOutput);
		PassInputs.SetInput(EPostProcessMaterialInput::SceneColor, Output);
		PassInputs.SceneTextures = Inputs.SceneTextures;

		Output = AddPostProcessMaterialPass(GraphBuilder, View, PassInputs, Inputs.CaptureRegionMaterial);
	}

	return Output;
}

void AddHighResScreenshotMask(FPostprocessContext& Context)
{
	FRenderingCompositePass* Pass = Context.Graph.RegisterPass(
		new(FMemStack::Get()) TRCPassForRDG<1, 1>(
			[](FRenderingCompositePass* InPass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		FHighResolutionScreenshotMaskInputs PassInputs;
		PassInputs.SceneColor.Texture = InPass->CreateRDGTextureForRequiredInput(GraphBuilder, ePId_Input0, TEXT("SceneColor"));
		PassInputs.SceneColor.ViewRect = InContext.SceneColorViewRect;

		if (FRDGTextureRef OverrideOutputTexture = InPass->FindRDGTextureForOutput(GraphBuilder, ePId_Output0, TEXT("FrameBuffer")))
		{
			PassInputs.OverrideOutput.Texture = OverrideOutputTexture;
			PassInputs.OverrideOutput.ViewRect = InContext.GetSceneColorDestRect(InPass);
			PassInputs.OverrideOutput.LoadAction = InContext.View.IsFirstInFamily() ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;
		}

		FScreenPassTexture PassOutput = AddHighResolutionScreenshotMaskPass(GraphBuilder, InContext.View, PassInputs);

		InPass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, PassOutput.Texture);

		GraphBuilder.Execute();
	}));
	Pass->SetInput(ePId_Input0, Context.FinalOutput);
	Context.FinalOutput = Pass;
}
