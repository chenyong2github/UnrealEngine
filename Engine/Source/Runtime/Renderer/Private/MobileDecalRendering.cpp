// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileDecalRendering.cpp: Decals for mobile renderer
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "DecalRenderingShared.h"

extern FRHIRasterizerState* GetDecalRasterizerState(EDecalRasterizerState DecalRasterizerState);
extern void RenderMeshDecalsMobile(FRHICommandList& RHICmdList, const FViewInfo& View);
void RenderDeferredDecalsMobile(FRHICommandList& RHICmdList, const FScene& Scene, const FViewInfo& View);

void FMobileSceneRenderer::RenderDecals(FRHICommandListImmediate& RHICmdList)
{
	if (!IsMobileHDR())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_DecalsDrawTime);

	// Deferred decals
	if (Scene->Decals.Num() > 0)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			RenderDeferredDecalsMobile(RHICmdList, *Scene, View);
		}
	}

	// Mesh decals
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.MeshDecalBatches.Num() > 0)
		{
			RenderMeshDecalsMobile(RHICmdList, View);
		}
	}
}

void RenderDeferredDecalsMobile(FRHICommandList& RHICmdList, const FScene& Scene, const FViewInfo& View)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Build a list of decals that need to be rendered for this view
	FTransientDecalRenderDataList SortedDecals;
	FDecalRendering::BuildVisibleDecalList(Scene, View, DRS_Mobile, &SortedDecals);
	if (SortedDecals.Num())
	{
		SCOPED_DRAW_EVENT(RHICmdList, DeferredDecals);
		INC_DWORD_STAT_BY(STAT_Decals, SortedDecals.Num());

		FUniformBufferRHIRef PassUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(RHICmdList, View.GetFeatureLevel(), ESceneTextureSetupMode::None, UniformBuffer_SingleDraw);
		FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
		SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);
				
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
		RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

		EDecalRasterizerState LastDecalRasterizerState = DRS_Undefined;
		TOptional<EDecalBlendMode> LastDecalBlendMode;
		TOptional<bool> LastDecalDepthState;

		for (int32 DecalIndex = 0, DecalCount = SortedDecals.Num(); DecalIndex < DecalCount; DecalIndex++)
		{
			const FTransientDecalRenderData& DecalData = SortedDecals[DecalIndex];
			const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
			const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
			const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);
						
			const float ConservativeRadius = DecalData.ConservativeRadius;
			const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);

			// update rasterizer state if needed
			{
				bool bReverseHanded = false;
				{
					// Account for the reversal of handedness caused by negative scale on the decal
					const auto& Scale3d = DecalProxy.ComponentTrans.GetScale3D();
					bReverseHanded = Scale3d[0] * Scale3d[1] * Scale3d[2] < 0.f;
				}
				EDecalRasterizerState DecalRasterizerState = FDecalRenderingCommon::ComputeDecalRasterizerState(bInsideDecal, bReverseHanded, View.bReverseCulling);

				if (LastDecalRasterizerState != DecalRasterizerState)
				{
					LastDecalRasterizerState = DecalRasterizerState;
					GraphicsPSOInit.RasterizerState = GetDecalRasterizerState(DecalRasterizerState);
				}
			}

			// update DepthStencil state if needed
			if (!LastDecalDepthState.IsSet() || LastDecalDepthState.GetValue() != bInsideDecal)
			{
				LastDecalDepthState = bInsideDecal;
				if (bInsideDecal)
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
						false, CF_Always,
						true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
						false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
						GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
						false, CF_DepthNearOrEqual,
						true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
						false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
						GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1), 0x00>::GetRHI();
				}
			}

			// update BlendMode if needed
			if (!LastDecalBlendMode.IsSet() || LastDecalBlendMode.GetValue() != DecalData.FinalDecalBlendMode)
			{
				LastDecalBlendMode = DecalData.FinalDecalBlendMode;
				GraphicsPSOInit.BlendState = FDecalRendering::GetDecalBlendState(View.FeatureLevel, DRS_Mobile, DecalData.FinalDecalBlendMode, DecalData.bHasNormal);
			}

			// Set shader params
			FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, DRS_Mobile, FrustumComponentToClip);
			
			RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);
		}
	}
}
