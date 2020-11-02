// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MobileSeparateTranslucencyPass.cpp - Mobile specific separate translucency pass
=============================================================================*/

#include "MobileSeparateTranslucencyPass.h"
#include "TranslucentRendering.h"
#include "DynamicPrimitiveDrawing.h"

bool IsMobileSeparateTranslucencyActive(const FViewInfo* Views, int32 NumViews)
{
	for (int32 ViewIdx = 0; ViewIdx < NumViews; ++ViewIdx)
	{
		if (IsMobileSeparateTranslucencyActive(Views[ViewIdx]))
		{
			return true;
		}
	}
	return false;
}

bool IsMobileSeparateTranslucencyActive(const FViewInfo& View)
{
	return View.ParallelMeshDrawCommandPasses[EMeshPass::TranslucencyAfterDOF].HasAnyDraw();
}

void AddMobileSeparateTranslucencyPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMobileSeparateTranslucencyInputs& Inputs)
{
	FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.SceneColor.Texture, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Inputs.SceneDepth.Texture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilRead);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("SeparateTranslucency %dx%d", View.ViewRect.Width(), View.ViewRect.Height()),
		PassParameters,
		ERDGPassFlags::Raster,
		[&View](FRHICommandList& RHICmdList)
	{
		// Set the view family's render target/viewport.
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		View.ParallelMeshDrawCommandPasses[EMeshPass::TranslucencyAfterDOF].DispatchDraw(nullptr, RHICmdList);
	});
}