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
#include "HairStrandsScatter.h"

/////////////////////////////////////////////////////////////////////////////////////////

static int32 GHairFastResolveVelocityThreshold = 1;
static FAutoConsoleVariableRef CVarHairFastResolveVelocityThreshold(TEXT("r.HairStrands.VelocityThreshold"), GHairFastResolveVelocityThreshold, TEXT("Threshold value (in pixel) above which a pixel is forced to be resolve with responsive AA (in order to avoid smearing). Default is 3."));

static int32 GHairPatchBufferDataBeforePostProcessing = 1;
static FAutoConsoleVariableRef CVarHairPatchBufferDataBeforePostProcessing(TEXT("r.HairStrands.PatchMaterialData"), GHairPatchBufferDataBeforePostProcessing, TEXT("Patch the buffer with hair material data before post processing run. (default 1)."));

class FHairVisibilityComposeSamplePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityComposeSamplePS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityComposeSamplePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairSampleCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityNodeOffsetAndCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairLightingSampleBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_COMPOSE_SAMPLE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairVisibilityComposeSamplePS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "ComposeSamplePS", SF_Pixel);

static void AddHairVisibilityComposeSamplePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FHairStrandsVisibilityData& VisibilityData,
	const FRDGTextureRef& CategorizationTexture,
	FRDGTextureRef& OutColorTexture,
	FRDGTextureRef& OutDepthTexture)
{
	check(VisibilityData.SampleLightingBuffer);

	FRDGTextureRef SampleLightingBuffer = GraphBuilder.RegisterExternalTexture(VisibilityData.SampleLightingBuffer);
	FRDGTextureRef NodeCount = GraphBuilder.RegisterExternalTexture(VisibilityData.NodeCount);

	FHairVisibilityComposeSamplePS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairVisibilityComposeSamplePS::FParameters>();
	Parameters->HairSampleCount = NodeCount;
	Parameters->HairCategorizationTexture = CategorizationTexture;
	Parameters->HairVisibilityNodeOffsetAndCount = GraphBuilder.RegisterExternalTexture(VisibilityData.NodeIndex);
	Parameters->HairLightingSampleBuffer = SampleLightingBuffer;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutColorTexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(OutDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityComposeSamplePS> PixelShader(View.ShaderMap);
	FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FIntPoint Resolution = OutColorTexture->Desc.Extent;
	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsComposeSample"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// Alpha usage/output is controlled with r.PostProcessing.PropagateAlpha. The value are:
		// 0: disabled(default);
		// 1: enabled in linear color space;
		// 2: same as 1, but also enable it through the tonemapper.
		//
		// When enable (PorpagateAlpha is set to 1 or 2), the alpha value means:
		// 0: valid pixel
		// 1: invalid pixel (background)
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(), Viewport.Height(),
			Viewport.Min.X, Viewport.Min.Y,
			Viewport.Width(), Viewport.Height(),
			Viewport.Size(),
			Resolution,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairVisibilityFastResolvePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairVisibilityFastResolvePS);
	SHADER_USE_PARAMETER_STRUCT(FHairVisibilityFastResolvePS, FGlobalShader);

	class FMSAACount : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_MSAACOUNT", 4, 8);
	using FPermutationDomain = TShaderPermutationDomain<FMSAACount>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, VelocityThreshold)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairVisibilityVelocityTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_FASTRESOLVE"), 1);
	}
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

	const uint32 MSAASampleCount = HairVisibilityVelocityTexture->Desc.NumSamples;
	check(MSAASampleCount == 4 || MSAASampleCount == 8);
	FHairVisibilityFastResolvePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairVisibilityFastResolvePS::FMSAACount>(MSAASampleCount == 4 ? 4 : 8);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairVisibilityFastResolvePS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);

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
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetStencilRef(STENCIL_TEMPORAL_RESPONSIVE_AA_MASK);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////
class FHairPatchGbufferDataPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairPatchGbufferDataPS);
	SHADER_USE_PARAMETER_STRUCT(FHairPatchGbufferDataPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorisationTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PATCH"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairPatchGbufferDataPS, "/Engine/Private/HairStrands/HairStrandsVisibilityComposeSubPixelPS.usf", "MainPS", SF_Pixel); 

static void AddPatchGbufferDataPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef HairCategorizationTexture,
	FRDGTextureRef OutGbufferATexture,
	FRDGTextureRef OutGbufferBTexture)
{
	const FIntPoint Resolution = OutGbufferATexture->Desc.Extent;
	FHairPatchGbufferDataPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairPatchGbufferDataPS::FParameters>();
	Parameters->CategorisationTexture = HairCategorizationTexture;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutGbufferATexture, ERenderTargetLoadAction::ELoad);
	Parameters->RenderTargets[1] = FRenderTargetBinding(OutGbufferBTexture, ERenderTargetLoadAction::ELoad);

	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairPatchGbufferDataPS> PixelShader(View.ShaderMap);
	const FIntRect Viewport = View.ViewRect;

	const FViewInfo* CapturedView = &View;

	ClearUnusedGraphResources(PixelShader, Parameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairPatchGbufferData"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);
		DrawRectangle(
			RHICmdList,
			0, 0,
			Viewport.Width(), Viewport.Height(),
			Viewport.Min.X, Viewport.Min.Y,
			Viewport.Width(), Viewport.Height(),
			Viewport.Size(),
			Resolution,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}
///////////////////////////////////////////////////////////////////////////////////////////////////

void RenderHairComposition(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FHairStrandsDatas* HairDatas)
{
	if (!HairDatas || HairDatas->HairVisibilityViews.HairDatas.Num() == 0)
		return;

	const FHairStrandsVisibilityViews& HairVisibilityViews = HairDatas->HairVisibilityViews;

	DECLARE_GPU_STAT(HairStrandsComposition);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsComposition);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsComposition);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);
	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneColorTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.GetSceneColor(), TEXT("SceneColorTexture"));
	FRDGTextureRef SceneColorDepth = GraphBuilder.RegisterExternalTexture(SceneTargets.SceneDepthZ, TEXT("SceneDepthTexture"));
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.Family)
		{
			if (ViewIndex < HairVisibilityViews.HairDatas.Num())
			{
				const FHairStrandsMacroGroupDatas& MacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[ViewIndex];
				const FHairStrandsVisibilityData& VisibilityData = HairVisibilityViews.HairDatas[ViewIndex];

				TRefCountPtr<IPooledRenderTarget> CategorisationTexture = VisibilityData.CategorizationTexture;
				if (!CategorisationTexture)
				{
					continue; // Automatically skip for any view not rendering hair
				}
				const FRDGTextureRef RDGCategorisationTexture = CategorisationTexture ? GraphBuilder.RegisterExternalTexture(CategorisationTexture, TEXT("HairVisibilityCategorisationTexture")) : nullptr;

				// todo: rehook the diffusion pass
				//AddHairDiffusionPass(
				//	GraphBuilder,
				//	View,
				//	VisibilityData,
				//	MacroGroupDatas.VirtualVoxelResources,
				//	SceneColorDepth,
				//	SceneColorSubPixelTexture,
				//	SceneColorTexture);

				AddHairVisibilityComposeSamplePass(
					GraphBuilder,
					View,
					VisibilityData,
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

				const bool bPatchBufferData = GHairPatchBufferDataBeforePostProcessing > 0;
				if (bPatchBufferData)
				{
					FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);				
					const FRDGTextureRef GBufferATexture = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferA , TEXT("GBufferA"));
					const FRDGTextureRef GBufferBTexture = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferB, TEXT("GBufferB"));
					if (GBufferATexture && GBufferBTexture)
					{
						AddPatchGbufferDataPass(
							GraphBuilder,
							View,
							RDGCategorisationTexture,
							GBufferATexture,
							GBufferBTexture);

						GraphBuilder.QueueTextureExtraction(GBufferATexture, &SceneContext.GBufferA, true);
						GraphBuilder.QueueTextureExtraction(GBufferBTexture, &SceneContext.GBufferB, true);
					}
				}
			}
		}
	}

	GraphBuilder.Execute();
}
