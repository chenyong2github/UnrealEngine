// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.h: Scene rendering definitions.
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

enum class ERayTracingPrimaryRaysFlag : uint32;

class FSceneTextureParameters;
class FDistanceFieldAOParameters;
class UStaticMeshComponent;
class FExponentialHeightFogSceneInfo;
class FRaytracingLightDataPacked;
struct FSceneWithoutWaterTextures;
struct FHeightFogRenderingParameters;
struct FRayTracingReflectionOptions;
struct FHairStrandsTransmittanceMaskData;
struct FHairStrandsRenderingData;

struct FVolumetricFogLocalLightFunctionInfo;

/**
 * Encapsulates the resources and render targets used by global illumination plugins (experimental).
 */
class RENDERER_API FGlobalIlluminationExperimentalPluginResources : public FRenderResource
{
public:
	TRefCountPtr<IPooledRenderTarget> GBufferA;
	TRefCountPtr<IPooledRenderTarget> GBufferB;
	TRefCountPtr<IPooledRenderTarget> GBufferC;
	TRefCountPtr<IPooledRenderTarget> SceneDepthZ;
	TRefCountPtr<IPooledRenderTarget> SceneColor;
	FRDGTextureRef LightingChannelsTexture;
};

/**
 * Delegate callback used by global illumination plugins (experimental).
 */
class RENDERER_API FGlobalIlluminationExperimentalPluginDelegates
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FAnyRayTracingPassEnabled, bool& /*bAnyRayTracingPassEnabled*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FPrepareRayTracing, const FViewInfo& /*View*/, TArray<FRHIRayTracingShader*>& /*OutRayGenShaders*/);
	DECLARE_MULTICAST_DELEGATE_FourParams(FRenderDiffuseIndirectLight, const FScene& /*Scene*/, const FViewInfo& /*View*/, FRDGBuilder& /*GraphBuilder*/, FGlobalIlluminationExperimentalPluginResources& /*Resources*/);

	static FAnyRayTracingPassEnabled& AnyRayTracingPassEnabled();
	static FPrepareRayTracing& PrepareRayTracing();
	static FRenderDiffuseIndirectLight& RenderDiffuseIndirectLight();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	DECLARE_MULTICAST_DELEGATE_FourParams(FRenderDiffuseIndirectVisualizations, const FScene& /*Scene*/, const FViewInfo& /*View*/, FRDGBuilder& /*GraphBuilder*/, FGlobalIlluminationExperimentalPluginResources& /*Resources*/);
	static FRenderDiffuseIndirectVisualizations& RenderDiffuseIndirectVisualizations();
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
};

/**
 * Scene renderer that implements a deferred shading pipeline and associated features.
 */
class FDeferredShadingSceneRenderer : public FSceneRenderer
{
public:

	/** Defines which objects we want to render in the EarlyZPass. */
	EDepthDrawingMode EarlyZPassMode;
	bool bEarlyZPassMovable;
	bool bDitheredLODTransitionsUseStencil;
	int32 StencilLODMode = 0;
	
	const FRHITransition* TranslucencyLightingVolumeClearEndTransition = nullptr;

	FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);

	/** Clears a view */
	void ClearView(FRHICommandListImmediate& RHICmdList);

	/** Clears LPVs for all views */
	void ClearLPVs(FRDGBuilder& GraphBuilder);

	/** Propagates LPVs for all views */
	void UpdateLPVs(FRHICommandListImmediate& RHICmdList);

	/**
	 * Renders the scene's prepass for a particular view
	 * @return true if anything was rendered
	 */
	void RenderPrePassView(FRHICommandList& RHICmdList, const FViewInfo& View);

	/**
	 * Renders the scene's prepass for a particular view in parallel
	 * @return true if the depth was cleared
	 */
	bool RenderPrePassViewParallel(const FViewInfo& View, FRHICommandListImmediate& ParentCmdList,TFunctionRef<void()> AfterTasksAreStarted, bool bDoPrePre);

	/** 
	 * Culls local lights and reflection probes to a grid in frustum space, builds one light list and grid per view in the current Views.  
	 * Needed for forward shading or translucency using the Surface lighting mode, and clustered deferred shading. 
	 */
	void GatherLightsAndComputeLightGrid(FRDGBuilder& GraphBuilder, bool bNeedLightGrid, FSortedLightSetSceneInfo &SortedLightSet);

	void RenderBasePass(
		FRDGBuilder& GraphBuilder,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		ERenderTargetLoadAction SceneDepthLoadAction,
		FRDGTextureRef ForwardShadowMaskTexture);

	void RenderBasePassInternal(
		FRDGBuilder& GraphBuilder,
		const FRenderTargetBindingSlots& BasePassRenderTargets,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		FRDGTextureRef ForwardScreenSpaceShadowMask,
		bool bParallelBasePass,
		bool bRenderLightmapDensity);

	bool ShouldRenderAnisotropyPass() const;

	void RenderAnisotropyPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		bool bDoParallelPass);

	void RenderSingleLayerWater(
		FRDGBuilder& GraphBuilder,
		FRDGTextureMSAA SceneColorTexture,
		FRDGTextureMSAA SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		bool bShouldRenderVolumetricCloud,
		FSceneWithoutWaterTextures& SceneWithoutWaterTextures);
	
	void RenderSingleLayerWaterInner(
		FRDGBuilder& GraphBuilder,
		FRDGTextureMSAA SceneColorTexture,
		FRDGTextureMSAA SceneDepthTexture,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures);

	void RenderSingleLayerWaterReflections(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures);

	void RenderOcclusion(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef SmallDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		bool bIsOcclusionTesting);

	bool RenderHzb(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer);

	/** Renders the view family. */
	virtual void Render(FRHICommandListImmediate& RHICmdList) override;

	/** Render the view family's hit proxies. */
	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) override;

	virtual bool ShouldRenderVelocities() const override;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void RenderVisualizeTexturePool(FRHICommandListImmediate& RHICmdList);
#endif

private:

	static FGraphEventRef TranslucencyTimestampQuerySubmittedFence[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames + 1];
	static FGlobalDynamicIndexBuffer DynamicIndexBufferForInitViews;
	static FGlobalDynamicIndexBuffer DynamicIndexBufferForInitShadows;
	static FGlobalDynamicVertexBuffer DynamicVertexBufferForInitViews;
	static FGlobalDynamicVertexBuffer DynamicVertexBufferForInitShadows;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBufferForInitViews;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBufferForInitShadows;

	FSeparateTranslucencyDimensions SeparateTranslucencyDimensions;

	/** Creates a per object projected shadow for the given interaction. */
	void CreatePerObjectProjectedShadow(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		bool bCreateTranslucentObjectShadow,
		bool bCreateInsetObjectShadow,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows);

	/**
	* Performs once per frame setup prior to visibility determination.
	*/
	void PreVisibilityFrameSetup(FRHICommandListImmediate& RHICmdList);

	/** Determines which primitives are visible for each view. */
	bool InitViews(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, struct FILCUpdatePrimTaskData& ILCTaskData);

	void InitViewsPossiblyAfterPrepass(FRHICommandListImmediate& RHICmdList, struct FILCUpdatePrimTaskData& ILCTaskData);
		
	void CreateIndirectCapsuleShadows();

	/**
	* Setup the prepass. This is split out so that in parallel we can do the fx prerender after we start the parallel tasks
	* @return true if the depth was cleared
	*/
	bool PreRenderPrePass(FRHICommandListImmediate& RHICmdList);

	void PreRenderDitherFill(FRHIAsyncComputeCommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext, FRHIUnorderedAccessView* StencilTextureUAV);
	void PreRenderDitherFill(FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext, FRHIUnorderedAccessView* StencilTextureUAV);

	void RenderPrePassEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState, EDepthDrawingMode DepthDrawingMode, bool bRespectUseAsOccluderFlag);

	/**
	 * Renders the scene's prepass and occlusion queries.
	 * @return true if the depth was cleared
	 */
	bool RenderPrePass(FRHICommandListImmediate& RHICmdList, TFunctionRef<void()> AfterTasksAreStarted);

	/**
	 * Renders the active HMD's hidden area mask as a depth prepass, if available.
	 * @return true if depth is cleared
	 */
	bool RenderPrePassHMD(FRHICommandListImmediate& RHICmdList);

	void RenderFog(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef LightShaftOcclusionTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);

	void RenderUnderWaterFog(
		FRDGBuilder& GraphBuilder,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);

	void RenderAtmosphere(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef LightShaftOcclusionTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);

	/** Render debug visualizations about the sky atmosphere into the scene render target.*/
	void RenderDebugSkyAtmosphere(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture);

	void RenderDiffuseIndirectAndAmbientOcclusion(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef LightingChannelsTexture,
		FHairStrandsRenderingData* HairDatas);

	/** Renders sky lighting and reflections that can be done in a deferred pass. */
	void RenderDeferredReflectionsAndSkyLighting(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureMSAA SceneColorTexture,
		FRDGTextureRef DynamicBentNormalAOTexture,
		FRDGTextureRef VelocityTexture,
		struct FHairStrandsRenderingData* HairDatas);

	void RenderDeferredReflectionsAndSkyLightingHair(FRDGBuilder& GraphBuilder, struct FHairStrandsRenderingData* HairDatas);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Renders debug visualizations for global illumination plugins (experimental). */
	void RenderGlobalIlluminationExperimentalPluginVisualizations(FRDGBuilder& GraphBuilder, FRDGTextureRef LightingChannelsTexture);
#endif

	/** Computes DFAO, modulates it to scene color (which is assumed to contain diffuse indirect lighting), and stores the output bent normal for use occluding specular. */
	void RenderDFAOAsIndirectShadowing(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef VelocityTexture,
		FRDGTextureRef& DynamicBentNormalAO);

	bool ShouldRenderDistanceFieldLighting() const;

	/** Render Ambient Occlusion using mesh distance fields and the surface cache, which supports dynamic rigid meshes. */
	void RenderDistanceFieldLighting(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const class FDistanceFieldAOParameters& Parameters,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef VelocityTexture,
		FRDGTextureRef& OutDynamicBentNormalAO,
		bool bModulateToSceneColor,
		bool bVisualizeAmbientOcclusion);

	/** Render Ambient Occlusion using mesh distance fields on a screen based grid. */
	void RenderDistanceFieldAOScreenGrid(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FDistanceFieldAOParameters& Parameters,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef VelocityTexture,
		FRDGTextureRef DistanceFieldNormal,
		FRDGTextureRef& OutDynamicBentNormalAO);

	void RenderMeshDistanceFieldVisualization(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FDistanceFieldAOParameters& Parameters);

	/** Whether tiled deferred is supported and can be used at all. */
	bool CanUseTiledDeferred() const;

	/** Whether to use tiled deferred shading given a number of lights that support it. */
	bool ShouldUseTiledDeferred(int32 NumTiledDeferredLights) const;

	/** 
	 * True if the 'r.UseClusteredDeferredShading' flag is 1 and sufficient feature level. 
	 * NOTE: When true it takes precedence over the TiledDeferred path, since they handle the same lights.
	 */
	bool ShouldUseClusteredDeferredShading() const;

	/**
	 * Have the requisite lights been injected into the light grid, AKA can we run the shading pass?
	 */
	bool AreClusteredLightsInLightGrid() const;


	/** Add a clustered deferred shading lighting render pass.	Note: in the future it should take the RenderGraph builder as argument */
	void AddClusteredDeferredShadingPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FSortedLightSetSceneInfo& SortedLightsSet);

	/** Renders the lights in SortedLights in the range [TiledDeferredLightsStart, TiledDeferredLightsEnd) using tiled deferred shading. */
	FRDGTextureRef RenderTiledDeferredLighting(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef InSceneColorTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights,
		int32 TiledDeferredLightsStart,
		int32 TiledDeferredLightsEnd,
		const FSimpleLightArray& SimpleLights);

	/** Renders the scene's lighting. */
	FRDGTextureRef RenderLights(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef LightingChannelsTexture,
		FSortedLightSetSceneInfo& SortedLightSet,
		const FHairStrandsRenderingData* HairDatas);

	/** Renders an array of lights for the stationary light overlap viewmode. */
	void RenderLightArrayForOverlapViewmode(
		FRHICommandList& RHICmdList,
		FRHITexture* LightingChannelsTexture,
		const TSparseArray<FLightSceneInfoCompact>& LightArray);

	/** Render stationary light overlap as complexity to scene color. */
	void RenderStationaryLightOverlap(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef LightingChannelsTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer);
	
	/** Renders the scene's translucency passes. */
	void RenderTranslucency(
		FRDGBuilder& GraphBuilder,
		FRDGTextureMSAA SceneColorTexture,
		FRDGTextureMSAA SceneDepthTexture,
		const FHairStrandsRenderingData* HairDatas,
		FSeparateTranslucencyTextures* OutSeparateTranslucencyTextures,
		ETranslucencyView ViewsToRender);

	/** Renders the scene's translucency given a specific pass. */
	void RenderTranslucencyInner(
		FRDGBuilder& GraphBuilder,
		FRDGTextureMSAA SceneColorTexture,
		FRDGTextureMSAA SceneDepthTexture,
		FSeparateTranslucencyTextures* OutSeparateTranslucencyTextures,
		ETranslucencyView ViewsToRender,
		FRDGTextureRef SceneColorCopyTexture,
		ETranslucencyPass::Type TranslucencyPass);

	/** Renders the scene's light shafts */
	FRDGTextureRef RenderLightShaftOcclusion(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FIntPoint SceneTextureExtent);

	void RenderLightShaftBloom(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FIntPoint SceneTextureExtent,
		FRDGTextureRef SceneColorTexture,
		FSeparateTranslucencyTextures& OutSeparateTranslucencyTextures);

	bool ShouldRenderDistortion() const;
	void RenderDistortion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture);

	/** Renders world-space lightmap density instead of the normal color. */
	void RenderLightMapDensities(FRDGBuilder& GraphBuilder, const FRenderTargetBindingSlots& RenderTargets);

	/** Renders one of the EDebugViewShaderMode instead of the normal color. */
	void RenderDebugViewMode(FRDGBuilder& GraphBuilder, const FRenderTargetBindingSlots& RenderTargets);

	/** Updates the downsized depth buffer with the current full resolution depth buffer using a min/max checkerboard pattern. */
	void UpdateHalfResDepthSurfaceCheckerboardMinMax(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture);

	FRDGTextureRef CopyStencilToLightingChannelTexture(FRDGBuilder& GraphBuilder, FRDGTextureSRVRef SceneStencilTexture);

	/** Injects reflective shadowmaps into LPVs */
	bool InjectReflectiveShadowMaps(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo);

	/** Renders capsule shadows for all per-object shadows using it for the given light. */
	bool RenderCapsuleDirectShadows(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo& LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		TArrayView<const FProjectedShadowInfo* const> CapsuleShadows,
		bool bProjectingForForwardShading) const;

	/** Sets up ViewState buffers for rendering capsule shadows. */
	void SetupIndirectCapsuleShadows(
		FRDGBuilder& GraphBuilder, 
		const FViewInfo& View, 
		int32& NumCapsuleShapes, 
		int32& NumMeshesWithCapsules, 
		int32& NumMeshDistanceFieldCasters,
		FRHIShaderResourceView*& IndirectShadowLightDirectionSRV) const;

	/** Renders indirect shadows from capsules modulated onto scene color. */
	void RenderIndirectCapsuleShadows(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextureUniformBuffer,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef ScreenSpaceAO,
		bool& bScreenSpaceAOIsValid) const;

	/** Renders capsule shadows for movable skylights, using the cone of visibility (bent normal) from DFAO. */
	void RenderCapsuleShadowsForMovableSkylight(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef& BentNormalOutput) const;

	void RenderShadowProjections(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		FRDGTextureRef SceneDepthTexture,
		const FLightSceneInfo* LightSceneInfo,
		const FHairStrandsRenderingData* HairDatas,
		bool bProjectingForForwardShading);

	void RenderDeferredShadowProjections(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		FRDGTextureRef SceneDepthTexture,
		const FHairStrandsRenderingData* HairDatas,
		bool& bInjectedTranslucentVolume);

	void RenderForwardShadowProjections(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneDepthTexture,
		FRDGTextureRef& ForwardScreenSpaceShadowMask,
		FRDGTextureRef& ForwardScreenSpaceShadowMaskSubPixel,
		const FHairStrandsRenderingData* InHairDatas);

	/** Used by RenderLights to render a light function to the attenuation buffer. */
	bool RenderLightFunction(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		bool bLightAttenuationCleared,
		bool bProjectingForForwardShading);

	/** Renders a light function indicating that whole scene shadowing being displayed is for previewing only, and will go away in game. */
	bool RenderPreviewShadowsIndicator(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		bool bLightAttenuationCleared);

	/** Renders a light function with the given material. */
	bool RenderLightFunctionForMaterial(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		const FMaterialRenderProxy* MaterialProxy,
		bool bLightAttenuationCleared,
		bool bProjectingForForwardShading,
		bool bRenderingPreviewShadowsIndicator);

	/** Used by RenderLights to render a light to the scene color buffer. */
	void RenderLight(
		FRHICommandList& RHICmdList,
		const FLightSceneInfo* LightSceneInfo,
		FRHITexture* ScreenShadowMaskTexture,
		FRHITexture* LightingChannelTexture,
		const struct FHairStrandsVisibilityViews* InHairVisibilityViews,
		bool bRenderOverlap,
		bool bIssueDrawEvent);

	void RenderLight(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef LightingChannelsTexture,
		const FHairStrandsVisibilityViews* InHairVisibilityViews,
		bool bRenderOverlap);

	void RenderLightsForHair(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FSortedLightSetSceneInfo& SortedLightSet,
		const FHairStrandsRenderingData* HairDatas,
		FRDGTextureRef InScreenShadowMaskSubPixelTexture,
		FRDGTextureRef LightingChannelsTexture);

	/** Specialized version of RenderLight for hair (run lighting evaluation on at sub-pixel rate, without depth bound) */
	void RenderLightForHair(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		FRDGTextureRef LightingChannelsTexture,
		const FHairStrandsTransmittanceMaskData& InTransmittanceMaskData,
		const struct FHairStrandsVisibilityViews* InHairVisibilityViews);

	/** Renders an array of simple lights using standard deferred shading. */
	void RenderSimpleLightsStandardDeferred(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FSimpleLightArray& SimpleLights);

	/** Clears the translucency lighting volumes before light accumulation. */
	void ClearTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, int32 ViewIndex);

	/** Clears the translucency lighting volume via an async compute shader overlapped with the basepass. */
	void ClearTranslucentVolumeLightingAsyncCompute(FRHICommandListImmediate& RHICmdList);

	/** Add AmbientCubemap to the lighting volumes. */
	void InjectAmbientCubemapTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, const FViewInfo& View, int32 ViewIndex);

	/** Clears the volume texture used to accumulate per object shadows for translucency. */
	void ClearTranslucentVolumePerObjectShadowing(FRHICommandList& RHICmdList, const int32 ViewIndex);

	/** Accumulates the per object shadow's contribution for translucency. */
	void AccumulateTranslucentVolumeObjectShadowing(FRHICommandList& RHICmdList, const FProjectedShadowInfo* InProjectedShadowInfo, bool bClearVolume, const FViewInfo& View, const int32 ViewIndex);

	/** Accumulates direct lighting for the given light.  InProjectedShadowInfo can be NULL in which case the light will be unshadowed. */
	void InjectTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, const FLightSceneInfo& LightSceneInfo, const FProjectedShadowInfo* InProjectedShadowInfo, const FViewInfo& View, int32 ViewIndex);

	/** Accumulates direct lighting for an array of unshadowed lights. */
	void InjectTranslucentVolumeLightingArray(FRDGBuilder& GraphBuilder, const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights, int32 FirstLightIndex, int32 LightsEndIndex);

	/** Accumulates direct lighting for simple lights. */
	void InjectSimpleTranslucentVolumeLightingArray(FRDGBuilder& GraphBuilder, const FSimpleLightArray& SimpleLights, const FViewInfo& View, const int32 ViewIndex);

	/** Filters the translucency lighting volumes to reduce aliasing. */
	void FilterTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, const FViewInfo& View, const int32 ViewIndex);

	bool ShouldRenderVolumetricFog() const;

	void SetupVolumetricFog();

	void RenderLocalLightsForVolumetricFog(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		bool bUseTemporalReprojection,
		const struct FVolumetricFogIntegrationParameterData& IntegrationData,
		const FExponentialHeightFogSceneInfo& FogInfo,
		FIntVector VolumetricFogGridSize,
		FVector GridZParams,
		const FRDGTextureDesc& VolumeDesc,
		FRDGTexture*& OutLocalShadowedLightScattering);

	void RenderLightFunctionForVolumetricFog(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures,
		FIntVector VolumetricFogGridSize,
		float VolumetricFogMaxDistance,
		FMatrix& OutLightFunctionWorldToShadow,
		FRDGTexture*& OutLightFunctionTexture,
		bool& bOutUseDirectionalLightShadowing);

	void VoxelizeFogVolumePrimitives(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FVolumetricFogIntegrationParameterData& IntegrationData,
		FIntVector VolumetricFogGridSize,
		FVector GridZParams,
		float VolumetricFogDistance);

	void ComputeVolumetricFog(FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures);

	void VisualizeVolumetricLightmap(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef SceneDepthTexture);

	/** Render image based reflections (SSR, Env, SkyLight) without compute shaders */
	void RenderStandardDeferredImageBasedReflections(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, bool bReflectionEnv, const TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO, TRefCountPtr<IPooledRenderTarget>& VelocityRT);

	bool HasDeferredPlanarReflections(const FViewInfo& View) const;
	void RenderDeferredPlanarReflections(FRDGBuilder& GraphBuilder, const FSceneTextureParameters& SceneTextures, const FViewInfo& View, FRDGTextureRef& ReflectionsOutput);

	bool ShouldDoReflectionEnvironment() const;
	
	bool ShouldRenderDistanceFieldAO() const;

	void CopySceneCaptureComponentToTarget(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef ViewFamilyTexture);

	void SetupImaginaryReflectionTextureParameters(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FSceneTextureParameters* OutTextures);

	void RenderRayTracingReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		int DenoiserMode,
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderRayTracingDeferredReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		int DenoiserMode,
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderDitheredLODFadingOutMask(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneDepthTexture);

	void RenderRayTracingShadows(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		const FLightSceneInfo& LightSceneInfo,
		const IScreenSpaceDenoiser::FShadowRayTracingConfig& RayTracingConfig,
		const IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements,
		const struct FHairStrandsOcclusionResources* HairResources,
		FRDGTextureRef LightingChannelsTexture,
		FRDGTextureUAV* OutShadowMaskUAV,
		FRDGTextureUAV* OutRayHitDistanceUAV,
		FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV);

	void CompositeRayTracingSkyLight(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FIntPoint SceneTextureExtent,
		FRDGTextureRef SkyLightRT,
		FRDGTextureRef HitDistanceRT);
	
	bool RenderRayTracingGlobalIllumination(
		FRDGBuilder& GraphBuilder, 
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig* RayTracingConfig,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);
	
	void RenderRayTracingGlobalIlluminationBruteForce(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
		int32 UpscaleFactor,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);

	void RayTracingGlobalIlluminationCreateGatherPoints(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		int32 UpscaleFactor,
		int32 SampleIndex,
		FRDGBufferRef& GatherPointsBuffer,
		FIntVector& GatherPointsResolution);

	void RenderRayTracingGlobalIlluminationFinalGather(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
		int32 UpscaleFactor,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);
	
	void RenderRayTracingAmbientOcclusion(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		const FSceneTextureParameters& SceneTextures,
		FRDGTextureRef* OutAmbientOcclusionTexture);
	

#if RHI_RAYTRACING
	template <int TextureImportanceSampling>
	void RenderRayTracingRectLightInternal(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const TArray<FViewInfo>& Views,
		const FLightSceneInfo& RectLightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef RayDistanceTexture);

	void VisualizeRectLightMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FRWBuffer& RectLightMipTree, const FIntVector& RectLightMipTreeDimensions);

	void VisualizeSkyLightMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRWBuffer& SkyLightMipTreePosX, FRWBuffer& SkyLightMipTreePosY, FRWBuffer& SkyLightMipTreePosZ, FRWBuffer& SkyLightMipTreeNegX, FRWBuffer& SkyLightMipTreeNegY, FRWBuffer& SkyLightMipTreeNegZ, const FIntVector& SkyLightMipDimensions);

	void RenderRayTracingSkyLight(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef& OutSkyLightTexture,
		FRDGTextureRef& OutHitDistanceTexture,
		const FHairStrandsRenderingData* HairDatas);

	void RenderRayTracingPrimaryRaysView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef* InOutColorTexture,
		FRDGTextureRef* InOutRayHitDistanceTexture,
		int32 SamplePerPixel,
		int32 HeightFog,
		float ResolutionFraction,
		ERayTracingPrimaryRaysFlag Flags);

	void RenderRayTracingTranslucency(FRDGBuilder& GraphBuilder, FRDGTextureMSAA SceneColorTexture);

	void RenderRayTracingTranslucencyView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef* OutColorTexture,
		FRDGTextureRef* OutRayHitDistanceTexture,
		int32 SamplePerPixel,
		int32 HeightFog,
		float ResolutionFraction);

	/** Lighting Evaluation shader setup (used by ray traced reflections and translucency) */
	void SetupRayTracingLightingMissShader(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Path tracing functions. */
	void RenderPathTracing(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef SceneColorOutputTexture);

	void WaitForRayTracingScene(FRDGBuilder& GraphBuilder);

	/** Debug ray tracing functions. */
	void RenderRayTracingDebug(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorOutputTexture);
	void RenderRayTracingBarycentrics(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorOutputTexture);

	bool GatherRayTracingWorldInstances(FRHICommandListImmediate& RHICmdList);
	bool DispatchRayTracingWorldUpdates(FRHICommandListImmediate& RHICmdList);
	FRayTracingPipelineState* BindRayTracingMaterialPipeline(FRHICommandList& RHICmdList, FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable, FRHIRayTracingShader* DefaultClosestHitShader);
	FRayTracingPipelineState* BindRayTracingDeferredMaterialGatherPipeline(FRHICommandList& RHICmdList, const FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable);

	// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. Register each effect at startup and just loop over them automatically
	static void PrepareRayTracingReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDeferredReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareSingleLayerWaterRayTracingReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingShadows(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingAmbientOcclusion(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingSkyLight(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIllumination(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIlluminationPlugin(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingTranslucency(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDebug(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PreparePathTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);

	// Versions for setting up the deferred material pipeline
	static void PrepareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDeferredReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIlluminationDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);


	/** Lighting evaluation shader registration */
	static FRHIRayTracingShader* GetRayTracingLightingMissShader(FViewInfo& View);

	const FRHITransition* RayTracingDynamicGeometryUpdateEndTransition = nullptr; // Signaled when all AS for this frame are built

#endif // RHI_RAYTRACING

	/** Set to true if the lights needed for clustered shading have been injected in the light grid (set in GatherLightsAndComputeLightGrid). */
	bool bClusteredShadingLightsInLightGrid;
};

DECLARE_CYCLE_STAT_EXTERN(TEXT("PrePass"), STAT_CLM_PrePass, STATGROUP_CommandListMarkers, );