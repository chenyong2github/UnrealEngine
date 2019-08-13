// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

namespace
{

TAutoConsoleVariable<int32> CVarPostProcessAllowStencilTest(
	TEXT("r.PostProcessAllowStencilTest"),
	1,
	TEXT("Enables stencil testing in post process materials.\n")
	TEXT("0: disable stencil testing\n")
	TEXT("1: allow stencil testing\n")
	TEXT("2: allow stencil testing and, if necessary, making a copy of custom depth/stencil buffer\n")
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

bool IsPostProcessStencilTestWithCopyAllowed()
{
	return CVarPostProcessAllowStencilTest.GetValueOnRenderThread() == 2;
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
	Enabled,

	// Custom Depth-Stencil is enabled; but makes a copy of the target to avoid simultaneous SRV / DSV usage.
	EnabledWithCopy
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

void GetMaterialInfo(
	const UMaterialInterface* InMaterialInterface,
	ERHIFeatureLevel::Type InFeatureLevel,
	EPixelFormat InOutputFormat,
	const FMaterial*& OutMaterial,
	const FMaterialRenderProxy*& OutMaterialProxy,
	const FMaterialShaderMap*& OutMaterialShaderMap)
{
	const FMaterialRenderProxy* MaterialProxy = InMaterialInterface->GetRenderProxy();
	check(MaterialProxy);

	const FMaterial* Material = MaterialProxy->GetMaterialNoFallback(InFeatureLevel);

	if (!Material || Material->GetMaterialDomain() != MD_PostProcess || !Material->GetRenderingThreadShaderMap())
	{
		// Fallback to the default post process material.
		const UMaterialInterface* DefaultMaterial = UMaterial::GetDefaultMaterial(MD_PostProcess);
		check(DefaultMaterial);
		check(DefaultMaterial != InMaterialInterface);

		return GetMaterialInfo(
			DefaultMaterial,
			InFeatureLevel,
			InOutputFormat,
			OutMaterial,
			OutMaterialProxy,
			OutMaterialShaderMap);
	}

	if (Material->IsStencilTestEnabled() || Material->GetBlendableOutputAlpha())
	{
		// Only allowed to have blend/stencil test if output format is compatible with ePId_Input0. 
		// PF_Unknown implies output format is that of EPId_Input0
		ensure(InOutputFormat == PF_Unknown);
	}

	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();;
	check(MaterialShaderMap);

	OutMaterial = Material;
	OutMaterialProxy = MaterialProxy;
	OutMaterialShaderMap = MaterialShaderMap;
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
	static_assert(EMaterialStencilCompare::MSC_Count == ARRAY_COUNT(StencilStates), "Ensure that all EMaterialStencilCompare values are accounted for.");

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
	static_assert(EBlendMode::BLEND_MAX == ARRAY_COUNT(BlendStates), "Ensure that all EBlendMode values are accounted for.");

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
		if (Parameters.Material->GetMaterialDomain() == MD_PostProcess)
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

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FMobileDimension>())
		{
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
		}
	}

protected:
	template <typename TRHIShader>
	void SetParameters(FRHICommandList& RHICmdList, TRHIShader* ShaderRHI, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FParameters& Parameters)
	{
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Proxy, *Proxy->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::All);
		SetShaderParameters(RHICmdList, this, ShaderRHI, Parameters);
	}
};

class FPostProcessMaterialVS : public FPostProcessMaterialShader
{
public:
	DECLARE_MATERIAL_SHADER(FPostProcessMaterialVS);

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FParameters& Parameters)
	{
		FPostProcessMaterialShader::SetParameters(RHICmdList, GetVertexShader(), View, Proxy, Parameters);
	}

	FPostProcessMaterialVS() = default;
	FPostProcessMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessMaterialShader(Initializer)
	{}
};

class FPostProcessMaterialPS : public FPostProcessMaterialShader
{
public:
	DECLARE_MATERIAL_SHADER(FPostProcessMaterialPS);

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FMaterialRenderProxy* Proxy, const FParameters& Parameters)
	{
		FPostProcessMaterialShader::SetParameters(RHICmdList, GetPixelShader(), View, Proxy, Parameters);
	}

	FPostProcessMaterialPS() = default;
	FPostProcessMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPostProcessMaterialShader(Initializer)
	{}
};

IMPLEMENT_MATERIAL_SHADER(FPostProcessMaterialVS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainVS", SF_Vertex);
IMPLEMENT_MATERIAL_SHADER(FPostProcessMaterialPS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainPS", SF_Pixel);

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

TGlobalResource<FPostProcessMaterialVertexDeclaration> GPostProcessMaterialVertexDeclaration;

} //! namespace

FPostProcessMaterialInput GetPostProcessMaterialInput(FIntRect ViewportRect, FRDGTextureRef Texture, FRHISamplerState* Sampler)
{
	FPostProcessMaterialInput Input;
	Input.Viewport = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(ViewportRect, Texture));
	Input.Texture = Texture;
	Input.Sampler = Sampler;
	return Input;
}

FRDGTextureRef ComputePostProcessMaterial(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const UMaterialInterface* MaterialInterface,
	const FPostProcessMaterialInputs& Inputs)
{
	Inputs.Validate();

	FRDGTextureRef SceneColorTexture = nullptr;
	FIntRect SceneColorViewportRect;
	Inputs.GetInput(EPostProcessMaterialInput::SceneColor, SceneColorTexture, SceneColorViewportRect);

	const FScreenPassTextureViewport SceneColorViewport(SceneColorViewportRect, SceneColorTexture);

	const ERHIFeatureLevel::Type FeatureLevel = ScreenPassView.View.GetFeatureLevel();

	const FMaterial* Material = nullptr;
	const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	const FMaterialShaderMap* MaterialShaderMap = nullptr;
	GetMaterialInfo(MaterialInterface, FeatureLevel, Inputs.OverrideOutputFormat, Material, MaterialRenderProxy, MaterialShaderMap);

	RDG_EVENT_SCOPE(GraphBuilder, "PostProcessMaterial %dx%d Material=%s", SceneColorViewport.Rect.Width(), SceneColorViewport.Rect.Height(), *Material->GetFriendlyName());

	FRHIDepthStencilState* DefaultDepthStencilState = FScreenPassDrawInfo::FDefaultDepthStencilState::GetRHI();
	FRHIDepthStencilState* DepthStencilState = DefaultDepthStencilState;

	FRDGTextureRef DepthStencilTexture = nullptr;
	FRDGTextureRef DepthStencilTextureForSRV = nullptr;

	// Allocate custom depth stencil texture(s) and depth stencil state.
	const ECustomDepthPolicy CustomStencilPolicy = GetMaterialCustomDepthPolicy(Material, FeatureLevel);

	if (CustomStencilPolicy != ECustomDepthPolicy::Disabled)
	{
		check(Inputs.CustomDepthTexture);

		DepthStencilTexture = Inputs.CustomDepthTexture;

		if (CustomStencilPolicy == ECustomDepthPolicy::EnabledWithCopy)
		{
			// NOTE: SM4 does not allow depth stencil to be a destination for a copy. Therefore,
			// we create a shader resource texture and copy into that.
			FRDGTextureDesc CopyDesc = Inputs.CustomDepthTexture->Desc;
			CopyDesc.Flags = TexCreate_None;
			CopyDesc.TargetableFlags = TexCreate_ShaderResource;

			DepthStencilTextureForSRV = GraphBuilder.CreateTexture(CopyDesc, TEXT("CustomDepthCopy"));

			AddCopyTexturePass(GraphBuilder, DepthStencilTexture, DepthStencilTextureForSRV);
		}
		else
		{
			DepthStencilTextureForSRV = DepthStencilTexture;
		}

		DepthStencilState = GetMaterialStencilState(Material);
	}

	FRHIBlendState* DefaultBlendState = FScreenPassDrawInfo::FDefaultBlendState::GetRHI();
	FRHIBlendState* BlendState = GetMaterialBlendState(Material);

	const FViewInfo& View = ScreenPassView.View;

	// Blend / Depth Stencil usage requires that the render target have primed color data.
	const bool bIsCompositeWithInput = DepthStencilState != DefaultDepthStencilState || BlendState != DefaultBlendState;

	// Multiple views are composited onto the same target.
	const bool bIsNotPrimaryView = (&View != View.Family->Views[0]);

	// We only prime color on the output texture if we are using fixed function Blend / Depth-Stencil,
	// or we need to retain previously rendered views.
	const bool bPrimeOutputColor = bIsCompositeWithInput || bIsNotPrimaryView;

	FRDGTextureRef OutputTexture = Inputs.OverrideOutputTexture;

	// We can re-use the scene color texture as the render target if we're not simultaneously reading from it.
	// This is only necessary to do if we're going to be priming content from the render target since it avoids
	// the copy. Otherwise, we just allocate a new render target.
	if (!OutputTexture && !MaterialShaderMap->UsesSceneTexture(PPI_PostProcessInput0) && bPrimeOutputColor)
	{
		OutputTexture = SceneColorTexture;
	}
	else
	{
		// Allocate new transient output texture if none exists.
		if (!OutputTexture)
		{
			const EPixelFormat OverrideOutputFormat = Inputs.OverrideOutputFormat;

			FRDGTextureDesc OutputTextureDesc = SceneColorTexture->Desc;
			if (OverrideOutputFormat != PF_Unknown)
			{
				OutputTextureDesc.Format = OverrideOutputFormat;
			}
			OutputTextureDesc.Reset();
			OutputTextureDesc.ClearValue = FClearValueBinding(FLinearColor::Black);
			OutputTextureDesc.Flags |= GFastVRamConfig.PostProcessMaterial;

			OutputTexture = GraphBuilder.CreateTexture(OutputTextureDesc, TEXT("PostProcessMaterialOutput"));
		}

		if (bPrimeOutputColor)
		{
			// Copy existing contents to new output and use load-action to preserve untouched pixels.
			AddDrawTexturePass(GraphBuilder, ScreenPassView, SceneColorTexture, OutputTexture);
		}
	}

	ERenderTargetLoadAction OutputLoadAction = ERenderTargetLoadAction::ENoAction;

	// We have color data that needs to be loaded.
	if (bPrimeOutputColor)
	{
		OutputLoadAction = ERenderTargetLoadAction::ELoad;
	}
	// HMD masks leave unrendered pixels; perform a clear instead.
	else if (ScreenPassView.bHasHMDMask)
	{
		OutputLoadAction = ERenderTargetLoadAction::EClear;
	}

	if (OutputTexture == SceneColorTexture)
	{
		ensureMsgf(OutputLoadAction == ERenderTargetLoadAction::ELoad,
			TEXT("We only want to re-use the input texture if we're going to load its contents. Otherwise RDG will emit a warning."));
	}

	FPostProcessMaterialParameters* PostProcessMaterialParameters = GraphBuilder.AllocParameters<FPostProcessMaterialParameters>();

	PostProcessMaterialParameters->PostProcessOutput = GetScreenPassTextureViewportParameters(SceneColorViewport);
	PostProcessMaterialParameters->CustomDepth = DepthStencilTextureForSRV;

	PostProcessMaterialParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, OutputLoadAction);

	if (DepthStencilTexture)
	{
		PostProcessMaterialParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthStencilTexture,
			ERenderTargetLoadAction::ENoAction,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthRead_StencilRead);
	}

	PostProcessMaterialParameters->PostProcessInput_BilinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();;

	FRDGTextureRef BlackTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, TEXT("BlackDummy"));

    // This gets passed in whether or not it's used.
	GraphBuilder.RemoveUnusedTextureWarning(BlackTexture);

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	for (uint32 InputIndex = 0; InputIndex < kPostProcessMaterialInputCountMax; ++InputIndex)
	{
		FRDGTextureRef InputTexture = nullptr;
		FIntRect InputViewportRect;
		Inputs.GetInput((EPostProcessMaterialInput)InputIndex, InputTexture, InputViewportRect);

		// Need to provide valid textures for when shader compilation doesn't cull unused parameters.
		if (!InputTexture || !MaterialShaderMap->UsesSceneTexture(PPI_PostProcessInput0 + InputIndex))
		{
			InputTexture = BlackTexture;
		}

		PostProcessMaterialParameters->PostProcessInput[InputIndex] = GetPostProcessMaterialInput(InputViewportRect, InputTexture, PointClampSampler);
	}

	const bool bIsMobile = FeatureLevel <= ERHIFeatureLevel::ES3_1;

	const bool bFlipYAxis = Inputs.bFlipYAxis && RHINeedsToSwitchVerticalAxis(GShaderPlatformForFeatureLevel[FeatureLevel]);

	PostProcessMaterialParameters->bFlipYAxis = bFlipYAxis;

	FPostProcessMaterialShader::FPermutationDomain PermutationVector;
	PermutationVector.Set<FPostProcessMaterialShader::FMobileDimension>(bIsMobile);

	FPostProcessMaterialVS* VertexShader = MaterialShaderMap->GetShader<FPostProcessMaterialVS>(PermutationVector);
	FPostProcessMaterialPS* PixelShader = MaterialShaderMap->GetShader<FPostProcessMaterialPS>(PermutationVector);

	const uint32 MaterialStencilRef = Material->GetStencilRefValue();

	const auto SetupFunction = [VertexShader, PixelShader, &ScreenPassView, MaterialRenderProxy, PostProcessMaterialParameters, MaterialStencilRef]
		(FRHICommandListImmediate& RHICmdList)
	{
		VertexShader->SetParameters(RHICmdList, ScreenPassView.View, MaterialRenderProxy, *PostProcessMaterialParameters);
		PixelShader->SetParameters(RHICmdList, ScreenPassView.View, MaterialRenderProxy, *PostProcessMaterialParameters);
		RHICmdList.SetStencilRef(MaterialStencilRef);
	};

	const FScreenPassDrawInfo ScreenPassDraw(
		VertexShader,
		PixelShader,
		BlendState,
		DepthStencilState,
		bFlipYAxis ? FScreenPassDrawInfo::EFlags::FlipYAxis : FScreenPassDrawInfo::EFlags::None);

	ScreenPassDraw.Validate();

	const bool bNeedsGBuffer = Material->NeedsGBuffer();

	if (bNeedsGBuffer)
	{
		FSceneRenderTargets::Get(GraphBuilder.RHICmdList).AdjustGBufferRefCount(GraphBuilder.RHICmdList, 1);
	}

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("PostProcessMaterial"),
		PostProcessMaterialParameters,
		ERDGPassFlags::Raster,
		[ScreenPassView, SceneColorViewport, ScreenPassDraw, PostProcessMaterialParameters, DepthStencilTexture, DepthStencilTextureForSRV, SetupFunction, bFlipYAxis, bNeedsGBuffer]
	(FRHICommandListImmediate& RHICmdList)
	{
		FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

		const bool bUsesDepthStencilCopy = DepthStencilTexture != DepthStencilTextureForSRV;

		if (bUsesDepthStencilCopy)
		{
			// Swap in the new copied SRV just prior to rendering.
			SceneRenderTargets.CustomDepth = DepthStencilTextureForSRV->GetPooledRenderTarget();
		}

		DrawScreenPass<decltype(SetupFunction)>(
			RHICmdList,
			ScreenPassView,
			SceneColorViewport,
			SceneColorViewport,
			ScreenPassDraw,
			SetupFunction);

		if (bUsesDepthStencilCopy)
		{
			// Swap back to the up-to-date target.
			SceneRenderTargets.CustomDepth = DepthStencilTexture->GetPooledRenderTarget();
		}

		if (bNeedsGBuffer)
		{
			SceneRenderTargets.AdjustGBufferRefCount(RHICmdList, -1);
		}
	});

	return OutputTexture;
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

FRDGTextureRef AddPostProcessMaterialChain(
	FRDGBuilder& GraphBuilder,
	const FScreenPassViewInfo& ScreenPassView,
	const FPostProcessMaterialInputs& Inputs,
	EBlendableLocation Location)
{
	const FViewInfo& View = ScreenPassView.View;

	if (!IsPostProcessMaterialsEnabledForView(View))
	{
		FIntRect Viewport;
		FRDGTextureRef Texture;
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor, Texture, Viewport);
		return Texture;
	}

	const FSceneViewFamily& ViewFamily = *View.Family;

	// hard coded - this should be a reasonable limit
	const uint32 MAX_PPMATERIALNODES = 10;
	FBlendableEntry* Iterator = nullptr;
	FPostProcessMaterialNode Nodes[MAX_PPMATERIALNODES];
	uint32 NodeCount = 0;
	bool bVisualizingBuffer = false;

	if (ViewFamily.EngineShowFlags.VisualizeBuffer)
	{
		// Apply requested material to the full screen
		UMaterial* Material = GetBufferVisualizationData().GetMaterial(View.CurrentBufferVisualizationMode);

		if (Material && Material->BlendableLocation == Location)
		{
			Nodes[0] = FPostProcessMaterialNode(Material, Location, Material->BlendablePriority);
			++NodeCount;
			bVisualizingBuffer = true;
		}
	}
	for (; NodeCount < MAX_PPMATERIALNODES; ++NodeCount)
	{
		FPostProcessMaterialNode* Data = IteratePostProcessMaterialNodes(View.FinalPostProcessSettings, Location, Iterator);

		if (!Data)
		{
			break;
		}

		check(Data->GetMaterialInterface());

		Nodes[NodeCount] = *Data;
	}

	::Sort(Nodes, NodeCount, FPostProcessMaterialNode::FCompare());

	const bool bBasePassCanOutputVelocity = FVelocityRendering::BasePassCanOutputVelocity(View.GetFeatureLevel());

	FRDGTextureRef LastSceneColor = nullptr;
	FIntRect SceneColorViewport;
	Inputs.GetInput(EPostProcessMaterialInput::SceneColor, LastSceneColor, SceneColorViewport);

	for (uint32 i = 0; i < NodeCount; ++i)
	{
		const UMaterialInterface* MaterialInterface = Nodes[i].GetMaterialInterface();

		FPostProcessMaterialInputs LocalInputs = Inputs;
		LocalInputs.SetInput(EPostProcessMaterialInput::SceneColor, LastSceneColor, SceneColorViewport);

		// This input is only needed for visualization and frame dumping
		if (!bVisualizingBuffer)
		{
			LocalInputs.SetInput(EPostProcessMaterialInput::PreTonemapHDRColor, nullptr, FIntRect());
			LocalInputs.SetInput(EPostProcessMaterialInput::PostTonemapHDRColor, nullptr, FIntRect());
		}

		// Velocity is only available when not generated from the base pass.
		if (bBasePassCanOutputVelocity)
		{
			LocalInputs.SetInput(EPostProcessMaterialInput::Velocity, nullptr, FIntRect());
		}

		LastSceneColor = ComputePostProcessMaterial(GraphBuilder, ScreenPassView, MaterialInterface, Inputs);
	}

	return LastSceneColor;
}

FRenderingCompositePass* AddPostProcessMaterialPass(
	const FPostprocessContext& PostProcessContext,
	const UMaterialInterface* MaterialInterface,
	EPixelFormat OverrideOutputFormat)
{
	const FMaterial* Material = nullptr;

	{
		const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
		const FMaterialShaderMap* MaterialShaderMap = nullptr;
		GetMaterialInfo(MaterialInterface, PostProcessContext.View.GetFeatureLevel(), OverrideOutputFormat, Material, MaterialRenderProxy, MaterialShaderMap);
		check(Material);
	}

	if (Material->NeedsGBuffer())
	{
		FSceneRenderTargets::Get(PostProcessContext.RHICmdList).AdjustGBufferRefCount(PostProcessContext.RHICmdList, 1);
	}

	return PostProcessContext.Graph.RegisterPass(new(FMemStack::Get()) TRCPassForRDG<kPostProcessMaterialInputCountMax, 1>(
		[MaterialInterface, Material, OverrideOutputFormat](FRenderingCompositePass* Pass, FRenderingCompositePassContext& InContext)
	{
		FRDGBuilder GraphBuilder(InContext.RHICmdList);

		FPostProcessMaterialInputs Inputs;
		Inputs.OverrideOutputFormat = OverrideOutputFormat;

		// Either finds the overridden frame buffer target or returns null.
		Inputs.OverrideOutputTexture = Pass->FindRDGTextureForOutput(GraphBuilder, ePId_Output0, TEXT("FrameBufferOverride"));

		for (uint32 InputIndex = 0; InputIndex < kPostProcessMaterialInputCountMax; ++InputIndex)
		{
			FRDGTextureRef InputTexture = Pass->CreateRDGTextureForOptionalInput(GraphBuilder, EPassInputId(InputIndex), TEXT("PostProcessInput"));

			/**
			 * TODO: Propagate each texture viewport through the graph setup instead of guessing. This is wrong for
			 * any scaled target (e.g. half resolution bloom). We deal with the upsample from TAAU explicitly here,
			 * but it's a band-aid at best. The problem is that we rely too heavily on the ViewRect--in pixels--which
			 * only applies to the primary screen resolution viewport.
			 */
			const FIntRect InputViewportRect = (InputIndex == 0) ? InContext.SceneColorViewRect : InContext.View.ViewRect;

			Inputs.SetInput((EPostProcessMaterialInput)InputIndex, InputTexture, InputViewportRect);
		}

		Inputs.bFlipYAxis = ShouldMobilePassFlipVerticalAxis(Pass);

		if (TRefCountPtr<IPooledRenderTarget> CustomDepthTarget = FSceneRenderTargets::Get(InContext.RHICmdList).CustomDepth)
		{
			Inputs.CustomDepthTexture = GraphBuilder.RegisterExternalTexture(CustomDepthTarget, TEXT("CustomDepth"));
		}

		FRDGTextureRef OutputTexture = ComputePostProcessMaterial(GraphBuilder, FScreenPassViewInfo(InContext.View), MaterialInterface, Inputs);

		Pass->ExtractRDGTextureForOutput(GraphBuilder, ePId_Output0, OutputTexture);

		GraphBuilder.Execute();

		if (Material->NeedsGBuffer())
		{
			FSceneRenderTargets::Get(InContext.RHICmdList).AdjustGBufferRefCount(InContext.RHICmdList, -1);
		}
	}));
}

FRenderingCompositeOutputRef AddPostProcessMaterialReplaceTonemapPass(
	FPostprocessContext& Context,
	FRenderingCompositeOutputRef SeparateTranslucency,
	FRenderingCompositeOutputRef CombinedBloom)
{
	if (!Context.View.Family->EngineShowFlags.PostProcessing || !Context.View.Family->EngineShowFlags.PostProcessMaterial)
	{
		return Context.FinalOutput;
	}

	FBlendableEntry* Iterator = nullptr;
	FPostProcessMaterialNode PPNode;
	while (FPostProcessMaterialNode* Data = IteratePostProcessMaterialNodes(Context.View.FinalPostProcessSettings, BL_ReplacingTonemapper, Iterator))
	{
		check(Data->GetMaterialInterface());

		if (PPNode.IsValid())
		{
			FPostProcessMaterialNode::FCompare Dummy;

			// take the one with the highest priority
			if (!Dummy.operator()(PPNode, *Data))
			{
				continue;
			}
		}

		PPNode = *Data;
	}

	if (UMaterialInterface* MaterialInterface = PPNode.GetMaterialInterface())
	{
		FRenderingCompositePass* Pass = AddPostProcessMaterialPass(Context, MaterialInterface);
		Pass->SetInput(EPassInputId(EPostProcessMaterialInput::SceneColor), Context.FinalOutput);
		Pass->SetInput(EPassInputId(EPostProcessMaterialInput::SeparateTranslucency), SeparateTranslucency);
		Pass->SetInput(EPassInputId(EPostProcessMaterialInput::CombinedBloom), CombinedBloom);

		return FRenderingCompositeOutputRef(Pass);
	}

	return Context.FinalOutput;
}

FRenderingCompositeOutputRef AddPostProcessMaterialChain(
	FPostprocessContext& Context,
	EBlendableLocation Location,
	FRenderingCompositeOutputRef SeparateTranslucency,
	FRenderingCompositeOutputRef PreTonemapHDRColor,
	FRenderingCompositeOutputRef PostTonemapHDRColor,
	FRenderingCompositeOutputRef PreFlattenVelocity)
{
	if (!IsPostProcessMaterialsEnabledForView(Context.View))
	{
		return Context.FinalOutput;
	}

	// hard coded - this should be a reasonable limit
	const uint32 MAX_PPMATERIALNODES = 10;
	FBlendableEntry* Iterator = 0;
	FPostProcessMaterialNode PPNodes[MAX_PPMATERIALNODES];
	uint32 PPNodeCount = 0;
	bool bVisualizingBuffer = false;

	if (Context.View.Family->EngineShowFlags.VisualizeBuffer)
	{
		// Apply requested material to the full screen
		UMaterial* Material = GetBufferVisualizationData().GetMaterial(Context.View.CurrentBufferVisualizationMode);

		if (Material && Material->BlendableLocation == Location)
		{
			PPNodes[0] = FPostProcessMaterialNode(Material, Location, Material->BlendablePriority);
			++PPNodeCount;
			bVisualizingBuffer = true;
		}
	}
	for (; PPNodeCount < MAX_PPMATERIALNODES; ++PPNodeCount)
	{
		FPostProcessMaterialNode* Data = IteratePostProcessMaterialNodes(Context.View.FinalPostProcessSettings, Location, Iterator);

		if (!Data)
		{
			break;
		}

		check(Data->GetMaterialInterface());

		PPNodes[PPNodeCount] = *Data;
	}

	::Sort(PPNodes, PPNodeCount, FPostProcessMaterialNode::FCompare());

	ERHIFeatureLevel::Type FeatureLevel = Context.View.GetFeatureLevel();

	FRenderingCompositeOutputRef LastOutput = Context.FinalOutput;

	for (uint32 i = 0; i < PPNodeCount; ++i)
	{
		const UMaterialInterface* MaterialInterface = PPNodes[i].GetMaterialInterface();

		FRenderingCompositePass* Pass = AddPostProcessMaterialPass(Context, MaterialInterface);

		Pass->SetInput(EPassInputId(EPostProcessMaterialInput::SceneColor), LastOutput);
		Pass->SetInput(EPassInputId(EPostProcessMaterialInput::SeparateTranslucency), SeparateTranslucency);

		// This input is only needed for visualization and frame dumping
		if (bVisualizingBuffer)
		{
			Pass->SetInput(EPassInputId(EPostProcessMaterialInput::PreTonemapHDRColor), PreTonemapHDRColor);
			Pass->SetInput(EPassInputId(EPostProcessMaterialInput::PostTonemapHDRColor), PostTonemapHDRColor);
		}

		if (!FVelocityRendering::BasePassCanOutputVelocity(FeatureLevel))
		{
			Pass->SetInput(EPassInputId(EPostProcessMaterialInput::Velocity), PreFlattenVelocity);
		}

		LastOutput = FRenderingCompositeOutputRef(Pass);
	}

	return LastOutput;
}

void AddHighResScreenshotMask(FPostprocessContext& Context)
{
	if (Context.View.Family->EngineShowFlags.HighResScreenshotMask != 0)
	{
		check(Context.View.FinalPostProcessSettings.HighResScreenshotMaterial);

		FRenderingCompositeOutputRef Input = Context.FinalOutput;

		FRenderingCompositePass* CompositePass = AddPostProcessMaterialPass(Context, Context.View.FinalPostProcessSettings.HighResScreenshotMaterial);
		CompositePass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Input));
		Context.FinalOutput = FRenderingCompositeOutputRef(CompositePass);

		if (GIsHighResScreenshot)
		{
			check(Context.View.FinalPostProcessSettings.HighResScreenshotMaskMaterial);

			FRenderingCompositePass* MaskPass = AddPostProcessMaterialPass(Context, Context.View.FinalPostProcessSettings.HighResScreenshotMaskMaterial);
			MaskPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Input));
			CompositePass->AddDependency(MaskPass);

			FString BaseFilename = FString(Context.View.FinalPostProcessSettings.BufferVisualizationDumpBaseFilename);
			MaskPass->SetOutputColorArray(ePId_Output0, FScreenshotRequest::GetHighresScreenshotMaskColorArray());
		}
	}

	// Draw the capture region if a material was supplied
	if (Context.View.FinalPostProcessSettings.HighResScreenshotCaptureRegionMaterial)
	{
		auto Material = Context.View.FinalPostProcessSettings.HighResScreenshotCaptureRegionMaterial;

		FRenderingCompositePass* CaptureRegionVisualizationPass = AddPostProcessMaterialPass(Context, Material);
		CaptureRegionVisualizationPass->SetInput(ePId_Input0, FRenderingCompositeOutputRef(Context.FinalOutput));
		Context.FinalOutput = FRenderingCompositeOutputRef(CaptureRegionVisualizationPass);
	}
}