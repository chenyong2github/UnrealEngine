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

static TAutoConsoleVariable<int32> CVarPostProcessAllowStencilTest(
	TEXT("r.PostProcessAllowStencilTest"),
	1,
	TEXT("Enables stencil testing in post process materials.\n")
	TEXT("0: disable stencil testing\n")
	TEXT("1: allow stencil testing\n")
	TEXT("2: allow stencil testing and, if necessary, making a copy of custom depth/stencil buffer\n")
	);

static TAutoConsoleVariable<int32> CVarPostProcessAllowBlendModes(
	TEXT("r.PostProcessAllowBlendModes"),
	1,
	TEXT("Enables blend modes in post process materials.\n")
	TEXT("0: disable blend modes. Uses replace\n")
	TEXT("1: allow blend modes\n")
	);

enum class EPostProcessMaterialTarget
{
	HighEnd,
	Mobile
};

static bool ShouldCachePostProcessMaterial(EPostProcessMaterialTarget MaterialTarget, EShaderPlatform Platform, const FMaterial* Material)
{
	if (Material->GetMaterialDomain() == MD_PostProcess)
	{
		switch (MaterialTarget)
		{
		case EPostProcessMaterialTarget::HighEnd:
			return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
		case EPostProcessMaterialTarget::Mobile:
			return IsMobilePlatform(Platform) && IsMobileHDR();
		}
	}

	return false;
}

template<EPostProcessMaterialTarget MaterialTarget>
class FPostProcessMaterialVS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FPostProcessMaterialVS, Material);
public:

	/**
	  * Only compile these shaders for post processing domain materials
	  */
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material)
	{
		return ShouldCachePostProcessMaterial(MaterialTarget, Platform, Material);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);

		if (MaterialTarget == EPostProcessMaterialTarget::Mobile)
		{
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
		}
	}
	
	FPostProcessMaterialVS( )	{ }
	FPostProcessMaterialVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMaterialShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	void SetParameters(FRHICommandList& RHICmdList, const FRenderingCompositePassContext& Context, const FMaterialRenderProxy* Proxy)
	{
		const FVertexShaderRHIParamRef ShaderRHI = GetVertexShader();
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Proxy, *Proxy->GetMaterial(Context.View.GetFeatureLevel()), Context.View, Context.View.ViewUniformBuffer, ESceneTextureSetupMode::All);
		PostprocessParameter.SetVS(ShaderRHI, Context, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
	}

	// Begin FShader interface
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		Ar << PostprocessParameter;
		return bShaderHasOutdatedParameters;
	}
	//  End FShader interface 
private:
	FPostProcessPassParameters PostprocessParameter;
};

typedef FPostProcessMaterialVS<EPostProcessMaterialTarget::HighEnd> FPostProcessMaterialVS_HighEnd;
typedef FPostProcessMaterialVS<EPostProcessMaterialTarget::Mobile> FPostProcessMaterialVS_Mobile;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,FPostProcessMaterialVS_HighEnd,TEXT("/Engine/Private/PostProcessMaterialShaders.usf"),TEXT("MainVS"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,FPostProcessMaterialVS_Mobile,TEXT("/Engine/Private/PostProcessMaterialShaders.usf"),TEXT("MainVS_ES2"),SF_Vertex);

/**
 * A pixel shader for rendering a post process material
 */
template<EPostProcessMaterialTarget MaterialTarget, uint32 UVPolicy>
class FPostProcessMaterialPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FPostProcessMaterialPS,Material);
public:

	/**
	  * Only compile these shaders for post processing domain materials
	  */
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const FMaterial* Material)
	{
		return ShouldCachePostProcessMaterial(MaterialTarget, Platform, Material);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const class FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Platform, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_UV_POLICY"), UVPolicy);

		EBlendableLocation Location = EBlendableLocation(Material->GetBlendableLocation());
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_AFTER_TAA_UPSAMPLE"), (Location == BL_AfterTonemapping || Location == BL_ReplacingTonemapper) ? 1 : 0);

		if (MaterialTarget == EPostProcessMaterialTarget::Mobile)
		{
			OutEnvironment.SetDefine(TEXT("MOBILE_FORCE_DEPTH_TEXTURE_READS"), 1); // Ensure post process materials will not attempt depth buffer fetch operations.
			OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Location != BL_AfterTonemapping) ? 1 : 0);
		}
	}

	FPostProcessMaterialPS() {}
	FPostProcessMaterialPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMaterialShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
	}

	template <typename TRHICmdList>
	void SetParameters(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, const FMaterialRenderProxy* Proxy)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Proxy, *Proxy->GetMaterial(Context.View.GetFeatureLevel()), Context.View, Context.View.ViewUniformBuffer, ESceneTextureSetupMode::All);
		PostprocessParameter.SetPS(RHICmdList, ShaderRHI, Context, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		Ar << PostprocessParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FPostProcessPassParameters PostprocessParameter;
};

typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::HighEnd, 0> FFPostProcessMaterialPS_HighEnd0;
typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::HighEnd, 1> FFPostProcessMaterialPS_HighEnd1;
typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::Mobile, 0> FPostProcessMaterialPS_Mobile0;
typedef FPostProcessMaterialPS<EPostProcessMaterialTarget::Mobile, 1> FPostProcessMaterialPS_Mobile1;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FFPostProcessMaterialPS_HighEnd0, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FFPostProcessMaterialPS_HighEnd1, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FPostProcessMaterialPS_Mobile0, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_ES2"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FPostProcessMaterialPS_Mobile1, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_ES2"), SF_Pixel);

FRCPassPostProcessMaterial::FRCPassPostProcessMaterial(UMaterialInterface* InMaterialInterface, ERHIFeatureLevel::Type InFeatureLevel, EPixelFormat OutputFormatIN)
: MaterialInterface(InMaterialInterface), OutputFormat(OutputFormatIN)
{
	FMaterialRenderProxy* Proxy = MaterialInterface->GetRenderProxy();
	check(Proxy);

	const FMaterial* Material = Proxy->GetMaterialNoFallback(InFeatureLevel);
	
	if (!Material || Material->GetMaterialDomain() != MD_PostProcess)
	{
		MaterialInterface = UMaterial::GetDefaultMaterial(MD_PostProcess);
	}

	if (Material && (Material->IsStencilTestEnabled() || Material->GetBlendableOutputAlpha()))
	{
		// Only allowed to have blend/stencil test if output format is compatible with ePId_Input0. 
		// PF_Unknown implies output format is that of EPId_Input0
		ensure(OutputFormat == PF_Unknown);
	}
}
		
/** The filter vertex declaration resource type. */
class FPostProcessMaterialVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	
	/** Destructor. */
	virtual ~FPostProcessMaterialVertexDeclaration() {}
	
	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FFilterVertex);
		Elements.Add(FVertexElement(0,STRUCT_OFFSET(FFilterVertex,Position),VET_Float4,0,Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	
	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};
TGlobalResource<FPostProcessMaterialVertexDeclaration> GPostProcessMaterialVertexDeclaration;

template<typename TPixelShader>
FShader* SetMobileShaders(const FMaterialShaderMap* MaterialShaderMap, FGraphicsPipelineStateInitializer &GraphicsPSOInit, FRenderingCompositePassContext &Context, FMaterialRenderProxy* Proxy)
{
	TPixelShader* PixelShader_Mobile = MaterialShaderMap->GetShader<TPixelShader>();
	FPostProcessMaterialVS_Mobile* VertexShader_Mobile = MaterialShaderMap->GetShader<FPostProcessMaterialVS_Mobile>();

	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader_Mobile);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader_Mobile);

	SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);

	VertexShader_Mobile->SetParameters(Context.RHICmdList, Context, Proxy);
	PixelShader_Mobile->SetParameters(Context.RHICmdList, Context, Proxy);
	return VertexShader_Mobile;
}

void FRCPassPostProcessMaterial::Process(FRenderingCompositePassContext& Context)
{
	FMaterialRenderProxy* Proxy = MaterialInterface->GetRenderProxy();
	check(Proxy);

	ERHIFeatureLevel::Type FeatureLevel = Context.View.GetFeatureLevel();

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(Context.RHICmdList);
	
	const FMaterial* Material = Proxy->GetMaterial(FeatureLevel);
	check(Material);

	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();

	int32 AllowStencilTest = CVarPostProcessAllowStencilTest.GetValueOnRenderThread();
	bool bReadsCustomDepthStencil = MaterialShaderMap->UsesSceneTexture(PPI_CustomDepth) || MaterialShaderMap->UsesSceneTexture(PPI_CustomStencil);
	bool bDoStencilTest = false;
	if (Material->IsStencilTestEnabled() && AllowStencilTest > 0)
	{
		static const IConsoleVariable* CVarCustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));

		int32 CustomDepthSetting = CVarCustomDepth->GetInt();
		if (CustomDepthSetting == 3)
		{
			bool AllowStencilTestWithCopy = AllowStencilTest == 2;

			// do the stencil test if 
			// not SM4 
			//   OR 
			// reads DS but allowed make DS copy 
			//   OR 
			// DS not read at all.
			bDoStencilTest = (FeatureLevel != ERHIFeatureLevel::SM4) || 
				((bReadsCustomDepthStencil == AllowStencilTestWithCopy) || AllowStencilTestWithCopy);
		}
		else
		{
			UE_LOG(LogRenderer, Warning, TEXT("PostProcessMaterial uses stencil test, but custom stencil not allocated. Set r.CustomDepth to 3 to allocate custom stencil."));
		}
	}

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	FIntVector RectSize = GetOutput(ePId_Output0)->RenderTargetDesc.GetSize();
	SCOPED_DRAW_EVENTF(Context.RHICmdList, PostProcessMaterial, TEXT("PostProcessMaterial %dx%d Material=%s"), RectSize.X, RectSize.Y, *Material->GetFriendlyName());

	// Copy of custom depth/stencil buffer if HW does not support simultaneously a texture bound as DepthRead_StencilRead and SRV
	TRefCountPtr<IPooledRenderTarget> CustomDepthStencilCopy;

	const FSceneRenderTargetItem* CustomDepthStencilTarget = nullptr;

	FDepthStencilStateRHIParamRef DepthStencilState;
	uint32 StencilRefValue = 0;

	if (bDoStencilTest)
	{
		CustomDepthStencilTarget = &SceneContext.CustomDepth->GetRenderTargetItem();

		// SM4 HW lacks support for texture bound as DepthRead_StencilRead and SRV simultaneously thus make a copy of DS buffer
		if (FeatureLevel == ERHIFeatureLevel::SM4 && bReadsCustomDepthStencil)
		{
			// Dest param of CopyResource() call can only be an SRV (No render target flags) on DX10.0 (SM4)
			FPooledRenderTargetDesc DSCopyDesc = SceneContext.CustomDepth->GetDesc();
			DSCopyDesc.Flags = TexCreate_None;
			DSCopyDesc.TargetableFlags = TexCreate_ShaderResource;

			GRenderTargetPool.FindFreeElement(Context.RHICmdList, DSCopyDesc, CustomDepthStencilCopy, TEXT("CustomDepthStencilCopy"));

			Context.RHICmdList.CopyTexture(
				SceneContext.CustomDepth->GetRenderTargetItem().ShaderResourceTexture,
				CustomDepthStencilCopy->GetRenderTargetItem().ShaderResourceTexture,
				FRHICopyTextureInfo()
				);

			// The copy DS buffer was created only as a SRV (no render target flags), thus swap with real CustomDepth buffer
			// with copy to allow DS buffer with render target flag to be set as DepthRead_StencilRead and DS buffer copy to
			// be set as SRV for post process material.
			Swap(SceneContext.CustomDepth, CustomDepthStencilCopy);
		}

		static const FDepthStencilStateRHIParamRef StencilStates[] =
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

		DepthStencilState = StencilStates[Material->GetStencilCompare()];
		StencilRefValue = Material->GetStencilRefValue();
	}
	else
	{
		DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	}

	FBlendStateRHIParamRef BlendState = TStaticBlendState<>::GetRHI();
	bool bDoOutputBlend = Material->GetBlendableOutputAlpha() && CVarPostProcessAllowBlendModes.GetValueOnRenderThread() != 0;
	if (bDoOutputBlend)
	{
		static const FBlendStateRHIParamRef BlendStates[] =
		{
			TStaticBlendState<>::GetRHI(),
			TStaticBlendState<>::GetRHI(),
			TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI(),
			TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI(),
			TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI(),
			TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI(),
		};
		static_assert(EBlendMode::BLEND_MAX == ARRAY_COUNT(BlendStates), "Ensure that all EBlendMode values are accounted for.");

		BlendState = BlendStates[Material->GetBlendMode()];
	}

	// The PP target - either from the render target pool or the ePId_Input0
	const FSceneRenderTargetItem* DestRenderTarget = nullptr;
	ERenderTargetLoadAction DestRenderTargetLoadAction = ERenderTargetLoadAction::Num;
		
	if (bDoStencilTest || bDoOutputBlend)
	{
		if (!MaterialShaderMap->UsesSceneTexture(PPI_PostProcessInput0))
		{
			PassOutputs[0].PooledRenderTarget = GetInput(ePId_Input0)->GetOutput()->RequestInput();
			DestRenderTarget = &PassOutputs[0].RequestSurface(Context);
		}
		else
		{
			DestRenderTarget = &PassOutputs[0].RequestSurface(Context);

			Context.RHICmdList.CopyTexture(
				GetInput(ePId_Input0)->GetOutput()->RequestSurface(Context).ShaderResourceTexture,
				DestRenderTarget->TargetableTexture,
				FRHICopyTextureInfo()
				);
		}

		DestRenderTargetLoadAction = ERenderTargetLoadAction::ELoad;
	}
	else
	{
		DestRenderTarget = &PassOutputs[0].RequestSurface(Context);
		DestRenderTargetLoadAction = Context.GetLoadActionForRenderTarget(*DestRenderTarget);
	}

	FIntRect SrcRect = Context.SceneColorViewRect;
	FIntRect DestRect = Context.GetSceneColorDestRect(*DestRenderTarget);
	checkf(DestRect.Size() == SrcRect.Size(), TEXT("Post process material should not be used as upscaling pass."));
	
	FRHIRenderPassInfo RPInfo;
	if (CustomDepthStencilTarget)
	{
		RPInfo = FRHIRenderPassInfo(
			DestRenderTarget->TargetableTexture,
			MakeRenderTargetActions(DestRenderTargetLoadAction, ERenderTargetStoreAction::EStore),
			CustomDepthStencilTarget->TargetableTexture,
			MakeDepthStencilTargetActions(
				MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction),
				MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::ENoAction)
				),
			FExclusiveDepthStencil::DepthRead_StencilRead
			);
	}
	else
	{
		RPInfo = FRHIRenderPassInfo(
			DestRenderTarget->TargetableTexture,
			MakeRenderTargetActions(Context.GetLoadActionForRenderTarget(*DestRenderTarget), ERenderTargetStoreAction::EStore)
			);
	}

	Context.RHICmdList.BeginRenderPass(RPInfo, TEXT("PostProcessMaterial"));
	{
		Context.SetViewportAndCallRHI(DestRect);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		Context.RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = BlendState;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = DepthStencilState;
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		const FViewInfo& View = Context.View;
		const FSceneViewFamily& ViewFamily = *(View.Family);

		FIntPoint SrcSize = InputDesc->Extent;

		FShader* VertexShader = nullptr;

		const bool bViewSizeMatchesBufferSize = (View.ViewRect == Context.SceneColorViewRect && View.ViewRect.Size() == SrcSize && View.ViewRect.Min == FIntPoint::ZeroValue);
		if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
		{
			// use mobile's post process material.
			if (bViewSizeMatchesBufferSize)
			{
				VertexShader = SetMobileShaders< FPostProcessMaterialPS_Mobile0>(MaterialShaderMap, GraphicsPSOInit, Context, Proxy);
			}
			else
			{
				VertexShader = SetMobileShaders< FPostProcessMaterialPS_Mobile1>(MaterialShaderMap, GraphicsPSOInit, Context, Proxy);
			}
			Context.RHICmdList.SetStencilRef(StencilRefValue);
		}
		// Uses highend post process material that assumed ViewSize == BufferSize.
		else if (bViewSizeMatchesBufferSize)
		{
			FFPostProcessMaterialPS_HighEnd0* PixelShader_HighEnd = MaterialShaderMap->GetShader<FFPostProcessMaterialPS_HighEnd0>();
			FPostProcessMaterialVS_HighEnd* VertexShader_HighEnd = MaterialShaderMap->GetShader<FPostProcessMaterialVS_HighEnd>();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GPostProcessMaterialVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader_HighEnd);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader_HighEnd);

			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);
			Context.RHICmdList.SetStencilRef(StencilRefValue);

			VertexShader_HighEnd->SetParameters(Context.RHICmdList, Context, Proxy);
			PixelShader_HighEnd->SetParameters(Context.RHICmdList, Context, Proxy);
			VertexShader = VertexShader_HighEnd;
		}
		// Uses highend post process material that handle ViewSize != BufferSize.
		else
		{
			FFPostProcessMaterialPS_HighEnd1* PixelShader_HighEnd = MaterialShaderMap->GetShader<FFPostProcessMaterialPS_HighEnd1>();
			FPostProcessMaterialVS_HighEnd* VertexShader_HighEnd = MaterialShaderMap->GetShader<FPostProcessMaterialVS_HighEnd>();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GPostProcessMaterialVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader_HighEnd);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader_HighEnd);

			SetGraphicsPipelineState(Context.RHICmdList, GraphicsPSOInit);
			Context.RHICmdList.SetStencilRef(StencilRefValue);

			VertexShader_HighEnd->SetParameters(Context.RHICmdList, Context, Proxy);
			PixelShader_HighEnd->SetParameters(Context.RHICmdList, Context, Proxy);
			VertexShader = VertexShader_HighEnd;
		}

		DrawPostProcessPass(
			Context.RHICmdList,
			0, 0,
			DestRect.Width(), DestRect.Height(),
			SrcRect.Min.X, SrcRect.Min.Y,
			SrcRect.Width(), SrcRect.Height(),
			DestRect.Size(),
			SrcSize,
			VertexShader,
			View.StereoPass,
			Context.HasHmdMesh(),
			EDRF_UseTriangleOptimization);
	}
	Context.RHICmdList.EndRenderPass();
	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget->TargetableTexture, DestRenderTarget->ShaderResourceTexture, FResolveParams());

	if (CustomDepthStencilCopy.IsValid() && CustomDepthStencilCopy->GetRenderTargetItem().IsValid())
	{
		Swap(SceneContext.CustomDepth, CustomDepthStencilCopy);
	}

	if(Material->NeedsGBuffer())
	{
		FSceneRenderTargets::Get(Context.RHICmdList).AdjustGBufferRefCount(Context.RHICmdList,-1);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessMaterial::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	if (OutputFormat != PF_Unknown)
	{
		Ret.Format = OutputFormat;
	}
	Ret.Reset();
	Ret.AutoWritable = false;
	Ret.DebugName = TEXT("PostProcessMaterial");
	Ret.ClearValue = FClearValueBinding(FLinearColor::Black);
	Ret.Flags |= GFastVRamConfig.PostProcessMaterial;

	return Ret;
}
