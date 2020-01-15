// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsComposition.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"
#include "HairStrandsRendering.h"

/////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairFastResolveVelocityThreshold = 1;
static FAutoConsoleVariableRef CVarHairFastResolveVelocityThreshold(TEXT("r.HairStrands.VelocityThreshold"), GHairFastResolveVelocityThreshold, TEXT("Threshold value (in pixel) above which a pixel is forced to be resolve with responsive AA (in order to avoid smearing). Default is 3."));

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityComposeSubPixelPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityComposeSubPixelPS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityComposeSubPixelPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SubPixelColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorisationTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityComposeSubPixelPS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "SubColorPS", SF_Pixel);

static void AddHairVisibilityComposeSubPixelPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& SubPixelColorTexture,
	const FRDGTextureRef CategorisationTexture,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutDepthTexture)
{
	FHairVisibilityComposeSubPixelPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityComposeSubPixelPS::FParameters>();
	Parameters->SubPixelColorTexture = SubPixelColorTexture;
	Parameters->CategorisationTexture = CategorisationTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(OutDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	FHairVisibilityComposeSubPixelPS::FPermutationDomain PermutationVector;
	TShaderMapRef<FHairVisibilityComposeSubPixelPS> PixelShader(View.ShaderMap, PermutationVector);
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutColorTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(*PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityComposeSubSPixel"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			// Write stencil value for partially covered pixel, in order to run responsive AA on them
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityFastResolvePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityFastResolvePS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityFastResolvePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, VelocityThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityVelocityTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return Parameters.Platform == SP_PCD3D_SM5; }
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityFastResolvePS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "FastResolvePS", SF_Pixel);

static void AddHairVisibilityFastResolvePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef& HairVisibilityVelocityTexture,
	FRDGTextureRef& OutDepthTexture)
{
	const FIntPoint Resolution = OutDepthTexture->Desc.Extent;
	FRDGTextureRef DummyTexture;
	{
		FRDGTextureDesc Desc;
		Desc.Extent = Resolution;
		Desc.Depth = 0;
		Desc.Format = PF_R8G8B8A8;
		Desc.NumMips = 1;
		Desc.NumSamples = 1;
		Desc.Flags = TexCreate_None;
		Desc.TargetableFlags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
		Desc.ClearValue = FClearValueBinding(0);
		DummyTexture = GraphBuilder.CreateTexture(Desc, TEXT("HairDummyTexture"));
	}

	FVector2D PixelVelocity(1.f / (Resolution.X * 2), 1.f / (Resolution.Y * 2));
	const float VelocityThreshold = FMath::Clamp(GHairFastResolveVelocityThreshold, 0, 512) * FMath::Min(PixelVelocity.X, PixelVelocity.Y);

	FHairVisibilityFastResolvePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityFastResolvePS::FParameters>();
	Parameters->HairVisibilityVelocityTexture = HairVisibilityVelocityTexture;
	Parameters->VelocityThreshold = VelocityThreshold;
	Parameters->RenderTargets[0] = FRenderTargetBinding(DummyTexture, ERenderTargetLoadAction::ENoAction);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(
		OutDepthTexture,
		ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ELoad,
		FExclusiveDepthStencil::DepthNop_StencilWrite);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityFastResolvePS> PixelShader(View.ShaderMap);
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(*PixelShader, Parameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsVisibilityMarkTAAFastResolve"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();	
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
				false, CF_Always, 
				true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
				false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
				STENCIL_TEMPORAL_RESPONSIVE_AA_MASK, STENCIL_TEMPORAL_RESPONSIVE_AA_MASK>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void RenderHairComposeSubPixel(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FHairStrandsDatas* HairDatas)
{
	if (!HairDatas || HairDatas->HairVisibilityViews.HairDatas.Num() == 0)
		return;

	const FHairStrandsVisibilityViews& HairVisibilityViews = HairDatas->HairVisibilityViews;

	DECLARE_GPU_STAT(HairStrandsComposeSubPixel);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsComposeSubPixel);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsComposeSubPixel);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);
	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneColorSubPixelTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.SceneColorSubPixel, TEXT("SceneColorSubPixelTexture"));
	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
	FRDGTextureRef SceneColorDepth = GraphBuilder.RegisterExternalTexture(SceneTargets.SceneDepthZ, TEXT("SceneDepthTexture"));
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			if (ViewIndex < HairVisibilityViews.HairDatas.Num())
			{
				TRefCountPtr<IPooledRenderTarget> CategorisationTexture = HairVisibilityViews.HairDatas[ViewIndex].CategorizationTexture;
				if (!CategorisationTexture)
				{
					continue; // Automatically skip for any view not rendering hair
				}
				const FRDGTextureRef RDGCategorisationTexture = CategorisationTexture ? GraphBuilder.RegisterExternalTexture(CategorisationTexture, TEXT("HairVisibilityCategorisationTexture")) : nullptr;

				// #hair_todo : compose partially covered hair with transparent surface: this can be done by 
				// rendering quad(s) covering the hair at the correct depth. This will be sorted with other 
				// transparent surface, which should make the overall sorting workable
				AddHairVisibilityComposeSubPixelPass(
					GraphBuilder,
					View,
					SceneColorSubPixelTexture,
					RDGCategorisationTexture,
					SceneColorTexture,
					SceneColorDepth);

				if (HairVisibilityViews.HairDatas[ViewIndex].VelocityTexture)
				{
					FRDGTextureRef RDGHairVisibilityVelocityTexture = GraphBuilder.RegisterExternalTexture(HairVisibilityViews.HairDatas[ViewIndex].VelocityTexture, TEXT("HairVisibilityVelocityTexture"));
					AddHairVisibilityFastResolvePass(
						GraphBuilder,
						View,
						RDGHairVisibilityVelocityTexture,
						SceneColorDepth);
				}
			}
		}
	}

	GraphBuilder.Execute();
}
