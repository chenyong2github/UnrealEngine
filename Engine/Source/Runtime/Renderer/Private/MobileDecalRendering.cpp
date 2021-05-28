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
#include "SceneTextureParameters.h"

extern FRHIRasterizerState* GetDecalRasterizerState(EDecalRasterizerState DecalRasterizerState);
extern void RenderMeshDecalsMobile(FRHICommandList& RHICmdList, const FViewInfo& View);
void RenderDeferredDecalsMobile(FRHICommandList& RHICmdList, const FScene& Scene, const FViewInfo& View);

FRHIBlendState* MobileForward_GetDecalBlendState(EDecalBlendMode DecalBlendMode)
{
	switch(DecalBlendMode)
	{
	case DBM_Translucent:
	case DBM_DBuffer_Color:
	case DBM_DBuffer_ColorNormal:
	case DBM_DBuffer_ColorRoughness:
	case DBM_DBuffer_ColorNormalRoughness:
		return TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
	case DBM_Stain:
		// Modulate
		return TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha>::GetRHI();
	case DBM_Emissive:
	case DBM_DBuffer_Emissive:
		// Additive
		return TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_One>::GetRHI();
	case DBM_AlphaComposite:
	case DBM_DBuffer_AlphaComposite:
		// Premultiplied alpha
		return TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
	default:
		check(0);
	};
	return TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
}

FRHIBlendState* MobileDeferred_GetDecalBlendState(EDecalBlendMode DecalBlendMode, bool bHasNormal)
{
	switch (DecalBlendMode)
	{
	case DBM_Translucent:
		if (bHasNormal)
		{
			return TStaticBlendState<
				CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
			>::GetRHI();
		}
		else
		{
			return TStaticBlendState<
				CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
				CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
			>::GetRHI();
		}
	case DBM_Stain:
		if (bHasNormal)
		{
			return TStaticBlendState<
				CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
				CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
			>::GetRHI();
		}
		else
		{
			return TStaticBlendState<
				CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,				// Emissive
				CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						
				CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
				CW_RGB, BO_Add, BF_DestColor, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
			>::GetRHI();
		}
	case DBM_Emissive:
	case DBM_DBuffer_Emissive:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_SourceAlpha, BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,				
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One
		>::GetRHI();
	
	case DBM_DBuffer_EmissiveAlphaComposite:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_One,	 BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,				
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One
		>::GetRHI();
	
	case DBM_AlphaComposite:
	case DBM_DBuffer_AlphaComposite:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_One,  BF_One, BO_Add, BF_Zero, BF_One,	// Emissive
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,				
			CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
			CW_RGB, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One	// BaseColor
		>::GetRHI();

	case DBM_DBuffer_ColorNormalRoughness:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One  // BaseColor
		>::GetRHI();
	
	case DBM_DBuffer_ColorRoughness:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One  // BaseColor
		>::GetRHI();

	case DBM_DBuffer_ColorNormal:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One  // BaseColor
		>::GetRHI();
	
	case DBM_DBuffer_NormalRoughness:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One
		>::GetRHI();

	case DBM_DBuffer_Color:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One  // BaseColor
		>::GetRHI();

	case DBM_DBuffer_Roughness:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Metallic, Specular, Roughness
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One
		>::GetRHI();
	
	case DBM_Normal:
	case DBM_DBuffer_Normal:
		return TStaticBlendState<
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,
			CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One,	// Normal
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One,						
			CW_RGB, BO_Add, BF_Zero, BF_One, BO_Add, BF_Zero, BF_One
		>::GetRHI();

	case DBM_Volumetric_DistanceFunction:
		return TStaticBlendState<>::GetRHI();
	
	case DBM_AmbientOcclusion:
		return TStaticBlendState<CW_RED, BO_Add, BF_DestColor, BF_Zero>::GetRHI();
	
	default:
		check(0);
	};

	return TStaticBlendState<>::GetRHI(); 
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileDecalPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_SRV(Buffer<float4>, EyeAdaptationBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FMobileDecalPassUniformParameters, "MobileDecalPass", SceneTextures);

BEGIN_SHADER_PARAMETER_STRUCT(FMobileDecalPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileDecalPassUniformParameters, Pass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

TUniformBufferRef<FMobileDecalPassUniformParameters> CreateMobileDecalPassUniformBuffer(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	FMobileDecalPassUniformParameters Parameters;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SetupMobileSceneTextureUniformParameters(SceneContext, EMobileSceneTextureSetupMode::All, Parameters.SceneTextures);
	Parameters.EyeAdaptationBuffer = GetEyeAdaptationBuffer(View);
	return TUniformBufferRef<FMobileDecalPassUniformParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleFrame);
}

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

			FUniformBufferRHIRef PassUniformBuffer = CreateMobileDecalPassUniformBuffer(RHICmdList, View);
			FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
			SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

			RenderDeferredDecalsMobile(RHICmdList, *Scene, View);
		}
	}

	// Mesh decals
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		if (View.MeshDecalBatches.Num() > 0)
		{
			FUniformBufferRHIRef PassUniformBuffer = CreateMobileDecalPassUniformBuffer(RHICmdList, View);
			FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
			SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

			RenderMeshDecalsMobile(RHICmdList, View);
		}
	}
}

void RenderDeferredDecalsMobile(FRHICommandList& RHICmdList, const FScene& Scene, const FViewInfo& View)
{
	const bool bDeferredShading = IsMobileDeferredShadingEnabled(View.GetShaderPlatform());

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	// Build a list of decals that need to be rendered for this view
	FTransientDecalRenderDataList SortedDecals;
	FDecalRendering::BuildVisibleDecalList(Scene, View, DRS_Mobile, &SortedDecals);
	if (SortedDecals.Num())
	{
		SCOPED_DRAW_EVENT(RHICmdList, DeferredDecals);
		INC_DWORD_STAT_BY(STAT_Decals, SortedDecals.Num());

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
		RHICmdList.SetStreamSource(0, GetUnitCubeVertexBuffer(), 0);

		for (int32 DecalIndex = 0, DecalCount = SortedDecals.Num(); DecalIndex < DecalCount; DecalIndex++)
		{
			const FTransientDecalRenderData& DecalData = SortedDecals[DecalIndex];
			const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
			const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
			const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);
						
			const float ConservativeRadius = DecalData.ConservativeRadius;
			const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);
			bool bReverseHanded = false;
			{
				// Account for the reversal of handedness caused by negative scale on the decal
				const auto& Scale3d = DecalProxy.ComponentTrans.GetScale3D();
				bReverseHanded = Scale3d[0] * Scale3d[1] * Scale3d[2] < 0.f;
			}
			EDecalRasterizerState DecalRasterizerState = FDecalRenderingCommon::ComputeDecalRasterizerState(bInsideDecal, bReverseHanded, View.bReverseCulling);
			GraphicsPSOInit.RasterizerState = GetDecalRasterizerState(DecalRasterizerState);

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
			
			if (bDeferredShading)
			{
				GraphicsPSOInit.BlendState = MobileDeferred_GetDecalBlendState(DecalData.FinalDecalBlendMode, DecalData.bHasNormal);
			}
			else
			{
				GraphicsPSOInit.BlendState = MobileForward_GetDecalBlendState(DecalData.FinalDecalBlendMode);
			}

			// Set shader params
			FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, DRS_Mobile, FrustumComponentToClip);
			
			RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, UE_ARRAY_COUNT(GCubeIndices) / 3, 1);
		}
	}
}
