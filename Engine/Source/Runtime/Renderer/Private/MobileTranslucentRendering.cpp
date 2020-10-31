// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileTranslucentRendering.cpp: translucent rendering implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "HitProxies.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "LightMapRendering.h"
#include "MaterialShaderType.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "BasePassRendering.h"
#include "DynamicPrimitiveDrawing.h"
#include "TranslucentRendering.h"
#include "MobileBasePassRendering.h"
#include "ScenePrivate.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"
#include "MeshPassProcessor.inl"
#include "ClearQuad.h"

void FMobileSceneRenderer::RenderTranslucency(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews)
{
	ETranslucencyPass::Type TranslucencyPass = 
		ViewFamily.AllowTranslucencyAfterDOF() ? ETranslucencyPass::TPT_StandardTranslucency : ETranslucencyPass::TPT_AllTranslucency;
		
	if (ShouldRenderTranslucency(TranslucencyPass))
	{
		SCOPED_DRAW_EVENT(RHICmdList, Translucency);
		SCOPED_GPU_STAT(RHICmdList, Translucency);

		for (int32 ViewIndex = 0; ViewIndex < PassViews.Num(); ViewIndex++)
		{
			SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

			const FViewInfo& View = *PassViews[ViewIndex];
			if (!View.ShouldRenderView())
			{
				continue;
			}

			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			if (!View.Family->UseDebugViewPS())
			{
				if (Scene->UniformBuffers.UpdateViewUniformBuffer(View))
				{
					UpdateTranslucentBasePassUniformBuffer(RHICmdList, View);
					UpdateDirectionalLightUniformBuffers(RHICmdList, View);
				}
		
				const EMeshPass::Type MeshPass = TranslucencyPassToMeshPass(TranslucencyPass);
				View.ParallelMeshDrawCommandPasses[MeshPass].DispatchDraw(nullptr, RHICmdList);
			}
		}
	}
}

bool FMobileSceneRenderer::RenderInverseOpacity(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	// Function MUST be self-contained wrt RenderPasses
	check(RHICmdList.IsOutsideRenderPass());

	bool bDirty = false;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	SceneContext.AllocSceneColor(RHICmdList);

	if (Scene->UniformBuffers.UpdateViewUniformBuffer(View))
	{
		UpdateTranslucentBasePassUniformBuffer(RHICmdList, View);
		UpdateDirectionalLightUniformBuffers(RHICmdList, View);
	}

	const bool bMobileMSAA = SceneContext.GetSceneColorSurface()->GetNumSamples() > 1;
	
	FRHITexture* SceneColorResolve = bMobileMSAA ? SceneContext.GetSceneColorTexture() : nullptr;
	ERenderTargetActions ColorTargetAction = bMobileMSAA ? ERenderTargetActions::Clear_Resolve : ERenderTargetActions::Clear_Store;
	FRHIRenderPassInfo RPInfo(
		SceneContext.GetSceneColorSurface(), 
		ColorTargetAction,
		SceneColorResolve,
		SceneContext.GetSceneDepthSurface(),
		EDepthStencilTargetActions::ClearDepthStencil_DontStoreDepthStencil,
		nullptr,
		FExclusiveDepthStencil::DepthWrite_StencilWrite
	);
	// Opacity could fetch depth as we use exactly the same shaders as in base pass
	RPInfo.SubpassHint = ESubpassHint::DepthReadSubpass;

	// make sure targets are writable
	FRHITransitionInfo TransitionsBefore[3];
	int32 NumTransitionsBefore = 0;
	TransitionsBefore[NumTransitionsBefore] = FRHITransitionInfo(SceneContext.GetSceneColorSurface(), ERHIAccess::Unknown, ERHIAccess::RTV);
	++NumTransitionsBefore;
	TransitionsBefore[NumTransitionsBefore] = FRHITransitionInfo(SceneContext.GetSceneDepthSurface(), ERHIAccess::Unknown, ERHIAccess::DSVWrite);
	++NumTransitionsBefore;
	if (SceneColorResolve)
	{
		TransitionsBefore[NumTransitionsBefore] = FRHITransitionInfo(SceneColorResolve, ERHIAccess::Unknown, ERHIAccess::RTV | ERHIAccess::ResolveDst);
		++NumTransitionsBefore;
	}
	RHICmdList.Transition(MakeArrayView(TransitionsBefore, NumTransitionsBefore));

	if (Scene->UniformBuffers.UpdateViewUniformBuffer(View))
	{
		UpdateTranslucentBasePassUniformBuffer(RHICmdList, View);
		UpdateDirectionalLightUniformBuffers(RHICmdList, View);
	}
	
	RHICmdList.BeginRenderPass(RPInfo, TEXT("InverseOpacity"));

	// Mobile multi-view is not side by side stereo
	const FViewInfo& TranslucentViewport = (View.bIsMobileMultiViewEnabled) ? Views[0] : View;
	RHICmdList.SetViewport(TranslucentViewport.ViewRect.Min.X, TranslucentViewport.ViewRect.Min.Y, 0.0f, TranslucentViewport.ViewRect.Max.X, TranslucentViewport.ViewRect.Max.Y, 1.0f);
		
	// Default clear value for a SceneColor is (0,0,0,0), after this passs will blend inverse opacity into final render target with an 1-SrcAlpha op
	// to make this blending work untouched pixels must have alpha = 1
	DrawClearQuad(RHICmdList, FLinearColor(0,0,0,1));

	RHICmdList.NextSubpass();
	if (ShouldRenderTranslucency(ETranslucencyPass::TPT_AllTranslucency) && View.ShouldRenderView())
	{		
		View.ParallelMeshDrawCommandPasses[EMeshPass::MobileInverseOpacity].DispatchDraw(nullptr, RHICmdList);
		bDirty |= View.ParallelMeshDrawCommandPasses[EMeshPass::MobileInverseOpacity].HasAnyDraw();
	}
	
	RHICmdList.EndRenderPass();
	
	ERHIAccess AccessBefore = bMobileMSAA ? ERHIAccess::RTV | ERHIAccess::ResolveDst : ERHIAccess::RTV;
	RHICmdList.Transition(FRHITransitionInfo(SceneContext.GetSceneColorTexture(), AccessBefore, ERHIAccess::SRVMask));
	
	return bDirty;
}

// This pass is registered only when we render to scene capture, see UpdateSceneCaptureContentMobile_RenderThread()
FMeshPassProcessor* CreateMobileInverseOpacityPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState PassDrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.MobileTranslucentBasePassUniformBuffer);
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	PassDrawRenderState.SetBlendState(TStaticBlendState<CW_ALPHA, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
	PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);

	const FMobileBasePassMeshProcessor::EFlags Flags = 
		FMobileBasePassMeshProcessor::EFlags::CanUseDepthStencil | 
		FMobileBasePassMeshProcessor::EFlags::ForcePassDrawRenderState;

	return new(FMemStack::Get()) FMobileBasePassMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext, Flags, ETranslucencyPass::TPT_AllTranslucency);
}