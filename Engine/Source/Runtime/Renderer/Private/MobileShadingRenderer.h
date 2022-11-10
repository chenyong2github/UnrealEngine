// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MobileShadingRenderer.h: Scene rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RendererInterface.h"
#include "StaticBoundShaderState.h"
#include "ScenePrivateBase.h"
#include "LightSceneInfo.h"
#include "SceneRendering.h"
#include "DepthRendering.h"
#include "TranslucentRendering.h"
#include "ScreenSpaceDenoise.h"
#include "RenderGraphUtils.h"
#include "TranslucentRendering.h"
#include "PostProcess/PostProcessing.h"
#include "MobileSeparateTranslucencyPass.h"

/**
 * Renderer that implements simple forward shading and associated features.
 */
class FMobileSceneRenderer : public FSceneRenderer
{
public:

	FMobileSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer);

	// FSceneRenderer interface

	virtual void Render(FRHICommandListImmediate& RHICmdList) override;

	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) override;

	bool RenderInverseOpacity(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

protected:
	/** Finds the visible dynamic shadows for each view. */
	void InitDynamicShadows(FRHICommandListImmediate& RHICmdList);

	void PrepareViewVisibilityLists();

	/** Build visibility lists on CSM receivers and non-csm receivers. */
	void BuildCSMVisibilityState(FLightSceneInfo* LightSceneInfo);

	void InitViews(FRHICommandListImmediate& RHICmdList);

	void RenderPrePass(FRHICommandListImmediate& RHICmdList);

	/** Renders the opaque base pass for mobile. */
	void RenderMobileBasePass(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	void RenderMobileEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState);

	/** Renders the debug view pass for mobile. */
	void RenderMobileDebugView(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);

	/** Render modulated shadow projections in to the scene, loops over any unrendered shadows until all are processed.*/
	void RenderModulatedShadowProjections(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Resolves scene depth in case hardware does not support reading depth in the shader */
	void ConditionalResolveSceneDepth(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Issues occlusion queries */
	void RenderOcclusion(FRHICommandListImmediate& RHICmdList);

	void RenderOcclusion(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> SceneTexturesUniformBuffer);

	bool RenderHzb(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> SceneTexturesUniformBuffer);

	/** Computes how many queries will be issued this frame */
	int32 ComputeNumOcclusionQueriesToBatch() const;

	/** Whether platform requires multiple render-passes for SceneColor rendering */
	bool RequiresMultiPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View) const;

	/** Renders decals. */
	void RenderDecals(FRHICommandListImmediate& RHICmdList);

	/** Renders the base pass for translucency. */
	void RenderTranslucency(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> PassViews);
	/** Renders separate translucency. */
	void RenderSeparateTranslucency(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture, FSeparateTranslucencyTextures& SeparateTranslucencyTextures, ETranslucencyPass::Type InTranslucencyPassType, const FViewInfo& View);

	/** Creates uniform buffers with the mobile directional light parameters, for each lighting channel. Called by InitViews */
	void CreateDirectionalLightUniformBuffers(FViewInfo& View);

	/** On chip pre-tonemap before scene color MSAA resolve (iOS only) */
	void PreTonemapMSAA(FRHICommandListImmediate& RHICmdList);

	void SortMobileBasePassAfterShadowInit(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView);
	void SetupMobileBasePassAfterShadowInit(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewVisibleCommandsPerView& ViewCommandsPerView);

	void UpdateOpaqueBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void UpdateTranslucentBasePassUniformBuffer(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void UpdateDirectionalLightUniformBuffers(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void UpdateSkyReflectionUniformBuffer();

	FRHITexture* RenderForward(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> ViewList);
	FRHITexture* RenderDeferred(FRHICommandListImmediate& RHICmdList, const TArrayView<const FViewInfo*> ViewList, const FSortedLightSetSceneInfo& SortedLightSet);

	void InitAmbientOcclusionOutputs(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ);
	void RenderAmbientOcclusion(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneDepthZ, const TRefCountPtr<IPooledRenderTarget>& WorldNormalRoughness);
	void RenderAmbientOcclusion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, FRDGTextureRef AmbientOcclusionTexture);
	void RenderAmbientOcclusion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, FRDGTextureRef WorldNormalRoughnessTexture, FRDGTextureRef AmbientOcclusionTexture);
	void ReleaseAmbientOcclusionOutputs();

	void InitPixelProjectedReflectionOutputs(FRHICommandListImmediate& RHICmdList, const FIntPoint& BufferSize);
	void RenderPixelProjectedReflection(FRHICommandListImmediate& RHICmdList, const FSceneRenderTargets& SceneContext, const FPlanarReflectionSceneProxy* PlanarReflectionSceneProxy);
	void RenderPixelProjectedReflection(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture, FRDGTextureRef PixelProjectedReflectionTexture, const FPlanarReflectionSceneProxy* PlanarReflectionSceneProxy);
	void ReleasePixelProjectedReflectionOutputs();

	void InitScreenSpaceReflectionOutputs(FRHICommandListImmediate& RHICmdList, const TRefCountPtr<IPooledRenderTarget>& SceneColor);
	void RenderScreenSpaceReflection(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FSceneRenderTargets& SceneContext);
	void RenderScreenSpaceReflection(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture, FRDGTextureRef WorldNormalRoughnessTexture, FRDGTextureRef ScreenSpaceReflectionTexture);
	void ReleaseScreenSpaceReflectionOutputs();

	bool ShouldRenderVolumetricFog() const;
	void SetupVolumetricFog();
	void ComputeVolumetricFog(FRDGBuilder& GraphBuilder, FSceneRenderTargets& SceneContext);

	bool ShouldRenderVelocities() const;

	void RenderVelocities(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef& VelocityTexture,
		TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		EVelocityPass VelocityPass,
		bool bForceVelocity);

	/** Before SetupMobileBasePassAfterShadowInit, we need to update the uniform buffer and shadow info for all movable point lights.*/
	void UpdateMovablePointLightUniformBufferAndShadowInfo();

	void AddMobilePostProcessingPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMobilePostProcessingInputs& Inputs);
	void AddMobileSeparateTranslucencyPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMobileSeparateTranslucencyInputs& Inputs);

private:
	const bool bGammaSpace;
	const bool bDeferredShading;
	const bool bUseVirtualTexturing;
	int32 NumMSAASamples;
	bool bRenderToSceneColor;
	bool bRequiresMultiPass;
	bool bKeepDepthContent;
	bool bSubmitOffscreenRendering;
	bool bModulatedShadowsInUse;
	bool bShouldRenderCustomDepth;
	bool bRequiresPixelProjectedPlanarRelfectionPass;
	bool bRequriesAmbientOcclusionPass;
	bool bRequriesScreenSpaceReflectionPass;
	static FGlobalDynamicIndexBuffer DynamicIndexBuffer;
	static FGlobalDynamicVertexBuffer DynamicVertexBuffer;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBuffer;
	FSeparateTranslucencyTextures SeparateTranslucencyTextures;
};