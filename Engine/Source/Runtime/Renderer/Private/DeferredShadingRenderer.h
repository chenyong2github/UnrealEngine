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
#include "Lumen/LumenProbeHierarchy.h"
#include "Lumen/LumenSceneRendering.h"
#include "IndirectLightRendering.h"
#include "ScreenSpaceRayTracing.h"
#include "RenderGraphUtils.h"

enum class ERayTracingPrimaryRaysFlag : uint32;
enum class EVelocityPass : uint32;

class FSceneTextureParameters;
class FDistanceFieldAOParameters;
class UStaticMeshComponent;
class FExponentialHeightFogSceneInfo;
class FRaytracingLightDataPacked;
class FLumenCardScatterContext;
namespace LumenRadianceCache
{
	class FRadianceCacheParameters;
}

struct FSceneWithoutWaterTextures;
struct FHeightFogRenderingParameters;
struct FRayTracingReflectionOptions;
struct FHairStrandsTransmittanceMaskData;
struct FHairStrandsRenderingData;

struct FTranslucencyLightingVolumeTextures;

/**   
 * Data for rendering meshes into Lumen Lighting Cards.
 */
class FLumenCardRenderer
{
public:
	TArray<FCardRenderData, SceneRenderingAllocator> CardsToRender;

	TArray<uint32, SceneRenderingAllocator> CardIdsToRender;
	TRefCountPtr<FRDGPooledBuffer> CardsToRenderIndexBuffer;

	static const uint32 NumCardsToRenderHashMapBucketUInt32 = 4 * 1024;
	// Indexed with CardId % NumCardsToRenderHashMapBuckets. Returns 1 bit if card is on the to render list or not.
	TBitArray<TInlineAllocator<NumCardsToRenderHashMapBucketUInt32 * 32>> CardsToRenderHashMap;
	TRefCountPtr<FRDGPooledBuffer> CardsToRenderHashMapBuffer;

	int32 NumCardTexelsToCapture;
	FMeshCommandOneFrameArray MeshDrawCommands;
	TArray<int32, SceneRenderingAllocator> MeshDrawPrimitiveIds;

	void Reset()
	{
		CardsToRender.Reset();
		MeshDrawCommands.Reset();
		MeshDrawPrimitiveIds.Reset();
		CardsToRenderHashMap.Reset();
		NumCardTexelsToCapture = 0;
	}
};

/** Encapsulation of the pipeline state of the renderer that have to deal with very large number of dimensions
 * and make sure there is no cycle dependencies in the dimensions by setting them ordered by memory offset in the structure.
 */
template<typename PermutationVectorType>
class TPipelineState
{
public:
	TPipelineState()
	{
		FPlatformMemory::Memset(&Vector, 0, sizeof(Vector));
	}

	/** Set a member of the pipeline state committed yet. */
	template<typename DimensionType>
	void Set(DimensionType PermutationVectorType::*Dimension, const DimensionType& DimensionValue)
	{
		SIZE_T ByteOffset = GetByteOffset(Dimension);

		// Make sure not updating a value of the pipeline already initialized, to ensure there is no cycle in the dependency of the different dimensions.
		checkf(ByteOffset >= InitializedOffset, TEXT("This member of the pipeline state has already been committed."));

		Vector.*Dimension = DimensionValue;

		// Update the initialised offset to make sure this is not set only once.
		InitializedOffset = ByteOffset + sizeof(DimensionType);
	}

	/** Commit the pipeline state to its final immutable value. */
	void Commit()
	{
		// Force the pipeline state to be initialized exactly once.
		checkf(!IsCommitted(), TEXT("Pipeline state has already been committed."));
		InitializedOffset = ~SIZE_T(0);
	}

	/** Returns whether the pipeline state has been fully committed to its final immutable value. */
	bool IsCommitted() const
	{
		return InitializedOffset == ~SIZE_T(0);
	}

	/** Access a member of the pipeline state, even when the pipeline state hasn't been fully committed to it's final value yet. */
	template<typename DimensionType>
	const DimensionType& operator [](DimensionType PermutationVectorType::*Dimension) const
	{
		SIZE_T ByteOffset = GetByteOffset(Dimension);

		checkf(ByteOffset < InitializedOffset, TEXT("This dimension has not been initialized yet."));

		return Vector.*Dimension;
	}

	/** Access the fully committed pipeline state structure. */
	const PermutationVectorType* operator->() const
	{
		// Make sure the pipeline state is committed to catch accesses to uninitialized settings. 
		checkf(IsCommitted(), TEXT("The pipeline state needs to be fully commited before being able to reference directly the pipeline state structure."));
		return &Vector;
	}

	/** Access the fully committed pipeline state structure. */
	const PermutationVectorType& operator * () const
	{
		// Make sure the pipeline state is committed to catch accesses to uninitialized settings. 
		checkf(IsCommitted(), TEXT("The pipeline state needs to be fully commited before being able to reference directly the pipeline state structure."));
		return Vector;
	}

private:

	template<typename DimensionType>
	static SIZE_T GetByteOffset(DimensionType PermutationVectorType::*Dimension)
	{
		return (SIZE_T)(&(((PermutationVectorType*) 0)->*Dimension));
	}

	PermutationVectorType Vector;

	SIZE_T InitializedOffset = 0;
};


/**
 * Scene renderer that implements a deferred shading pipeline and associated features.
 */
class FDeferredShadingSceneRenderer : public FSceneRenderer
{
public:
	/** Defines which objects we want to render in the EarlyZPass. */
	FDepthPassInfo DepthPass;

	FLumenCardRenderer LumenCardRenderer;

	FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);

	/** Determine and commit the final state of the pipeline for the view family and views. */
	void CommitFinalPipelineState();

	/** Commit all the pipeline state for indirect ligthing. */
	void CommitIndirectLightingState();

	/** Clears a view */
	void ClearView(FRHICommandListImmediate& RHICmdList);

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
		const FSceneTextures& SceneTextures,
		const FDBufferTextures& DBufferTextures,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		FRDGTextureRef ForwardShadowMaskTexture);

	void RenderBasePassInternal(
		FRDGBuilder& GraphBuilder,
		const FRenderTargetBindingSlots& BasePassRenderTargets,
		FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
		const FForwardBasePassTextures& ForwardBasePassTextures,
		const FDBufferTextures& DBufferTextures,
		bool bParallelBasePass,
		bool bRenderLightmapDensity);

	bool ShouldRenderAnisotropyPass() const;

	void RenderAnisotropyPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneDepthTexture,
		bool bDoParallelPass);

	void RenderSingleLayerWater(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		bool bShouldRenderVolumetricCloud,
		FSceneWithoutWaterTextures& SceneWithoutWaterTextures);
	
	void RenderSingleLayerWaterInner(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures);

	void RenderSingleLayerWaterReflections(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SceneColorTexture,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures);

	void RenderOcclusion(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		bool bIsOcclusionTesting);

	bool RenderHzb(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture);

	/** Renders the view family. */
	virtual void Render(FRHICommandListImmediate& RHICmdList) override;

	/** Render the view family's hit proxies. */
	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) override;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void RenderVisualizeTexturePool(FRHICommandListImmediate& RHICmdList);
#endif

private:
	enum class EDiffuseIndirectMethod
	{
		Disabled,
		RTGI,
		Lumen,
	};

	enum class EAmbientOcclusionMethod
	{
		Disabled,
		SSAO,
		SSGI, // SSGI can produce AO buffer at same time to correctly comp SSGI within the other indirect light such as skylight and lightmass.
		RTAO,
	};

	enum class EReflectionsMethod
	{
		Disabled,
		SSR,
		RTR,
		Lumen
	};

	/** Structure that contains the final state of deferred shading pipeline for a FViewInfo */
	struct FPerViewPipelineState
	{
		/** Method to use for dynamic diffuse indirect.
		 * SSGI can be enabled independently of the fall back method if supported. But there is only one denoiser invocation.
		 */
		bool bEnableSSGI;
		EDiffuseIndirectMethod DiffuseIndirectMethod;
		IScreenSpaceDenoiser::EMode DiffuseIndirectDenoiser;

		// Whether all indirect lighting should denoise using prove hierarchy denoiser.
		bool bUseLumenProbeHierarchy;

		// Method to use for ambient occlusion.
		EAmbientOcclusionMethod AmbientOcclusionMethod;

		// Method to use for reflections. 
		EReflectionsMethod ReflectionsMethod;

		// Whether there is planar reflection to compose to the reflection.
		bool bComposePlanarReflections;

		// Whether need to generate HZB from the depth buffer.
		bool bFurthestHZB;
		bool bClosestHZB;
	};

	// Structure that contains the final state of deferred shading pipeline for the FSceneViewFamily
	struct FFamilyPipelineState
	{
		// Whether Nanite is enabled.
		bool bNanite;

		// Whether the scene occlusion is made using HZB.
		bool bHZBOcclusion;
	};

	/** Pipeline states that describe the high level topology of the entire renderer.
	 *
	 * Once initialized by CommitFinalPipelineState(), it becomes immutable for the rest of the execution of the renderer.
	 */
	TArray<TPipelineState<FPerViewPipelineState>, TInlineAllocator<1>> ViewPipelineStates;
	TPipelineState<FFamilyPipelineState> FamilyPipelineState;

	FORCEINLINE const FPerViewPipelineState& GetViewPipelineState(const FViewInfo& View) const
	{
		int32 ViewIndex = 0;
		bool bFound = ViewFamily.Views.Find(&View, ViewIndex);
		check(bFound);
		return *ViewPipelineStates[ViewIndex];
	}

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
	void PreVisibilityFrameSetup(FRDGBuilder& GraphBuilder);

	/** Determines which primitives are visible for each view. */
	bool InitViews(FRDGBuilder& GraphBuilder, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, struct FILCUpdatePrimTaskData& ILCTaskData);

	void InitViewsPossiblyAfterPrepass(FRDGBuilder& GraphBuilder, struct FILCUpdatePrimTaskData& ILCTaskData);
	void UpdateLumenCardAtlasAllocation(FRDGBuilder& GraphBuilder, const FViewInfo& MainView, bool bReallocateAtlas, bool bRecaptureLumenSceneOnce);
	void BeginUpdateLumenSceneTasks(FRDGBuilder& GraphBuilder);
	void UpdateLumenScene(FRDGBuilder& GraphBuilder);
	void RenderLumenSceneLighting(FRDGBuilder& GraphBuilder, FViewInfo& View);

	void RenderDirectLightingForLumenScene(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef FinalLightingAtlas,
		FRDGTextureRef OpacityAtlas,
		FGlobalShaderMap* GlobalShaderMap,
		const FLumenCardScatterContext& VisibleCardScatterContext);
	
	void RenderRadiosityForLumenScene(FRDGBuilder& GraphBuilder, const class FLumenCardTracingInputs& TracingInputs, FGlobalShaderMap* GlobalShaderMap, FRDGTextureRef RadiosityAtlas);

	void PrefilterLumenSceneDepth(
		FRDGBuilder& GraphBuilder, 
		const TArray<uint32, SceneRenderingAllocator>& CardIdsToRender,
		const FViewInfo& View);

	void PrefilterLumenSceneLighting(
		FRDGBuilder& GraphBuilder, 
		const FViewInfo& View,
		FLumenCardTracingInputs& TracingInputs,
		FGlobalShaderMap* GlobalShaderMap,
		const FLumenCardScatterContext& VisibleCardScatterContext);

	void ComputeLumenSceneVoxelLighting(FRDGBuilder& GraphBuilder, FLumenCardTracingInputs& TracingInputs, FGlobalShaderMap* GlobalShaderMap);

	void ComputeLumenTranslucencyGIVolume(FRDGBuilder& GraphBuilder, FLumenCardTracingInputs& TracingInputs, FGlobalShaderMap* GlobalShaderMap);

	void CreateIndirectCapsuleShadows();

	void RenderPrePass(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture);
	void RenderPrePassHMD(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture);

	void RenderFog(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef LightShaftOcclusionTexture);

	void RenderUnderWaterFog(
		FRDGBuilder& GraphBuilder,
		const FSceneWithoutWaterTextures& SceneWithoutWaterTextures,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesWithDepth);

	void RenderAtmosphere(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef LightShaftOcclusionTexture);

	/** Render debug visualizations about the sky atmosphere into the scene render target.*/
	void RenderDebugSkyAtmosphere(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture);

	// TODO: Address tech debt to that directly in RenderDiffuseIndirectAndAmbientOcclusion()
	void SetupCommonDiffuseIndirectParameters(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		HybridIndirectLighting::FCommonParameters& OutCommonDiffuseParameters);

	/** Render diffuse indirect (regardless of the method) of the views into the scene color. */
	FSSDSignalTextures RenderLumenProbeHierarchy(
		FRDGBuilder& GraphBuilder,
		const HybridIndirectLighting::FCommonParameters& CommonParameters,
		const ScreenSpaceRayTracing::FPrevSceneColorMip& PrevSceneColor,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos);

	/** Render diffuse indirect (regardless of the method) of the views into the scene color. */
	void RenderDiffuseIndirectAndAmbientOcclusion(
		FRDGBuilder& GraphBuilder,
		FSceneTextures& SceneTextures,
		FHairStrandsRenderingData* HairDatas,
		bool bIsVisualizePass);

	/** Renders sky lighting and reflections that can be done in a deferred pass. */
	void RenderDeferredReflectionsAndSkyLighting(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		FRDGTextureRef DynamicBentNormalAOTexture,
		struct FHairStrandsRenderingData* HairDatas);

	void RenderDeferredReflectionsAndSkyLightingHair(FRDGBuilder& GraphBuilder, struct FHairStrandsRenderingData* HairDatas);

	/** Computes DFAO, modulates it to scene color (which is assumed to contain diffuse indirect lighting), and stores the output bent normal for use occluding specular. */
	void RenderDFAOAsIndirectShadowing(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		FRDGTextureRef& DynamicBentNormalAO);

	bool ShouldRenderDistanceFieldLighting() const;

	/** Render Ambient Occlusion using mesh distance fields and the surface cache, which supports dynamic rigid meshes. */
	void RenderDistanceFieldLighting(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const class FDistanceFieldAOParameters& Parameters,
		FRDGTextureRef& OutDynamicBentNormalAO,
		bool bModulateToSceneColor,
		bool bVisualizeAmbientOcclusion);

	/** Render Ambient Occlusion using mesh distance fields on a screen based grid. */
	void RenderDistanceFieldAOScreenGrid(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FViewInfo& View,
		const FDistanceFieldAOParameters& Parameters,
		FRDGTextureRef DistanceFieldNormal,
		FRDGTextureRef& OutDynamicBentNormalAO);

	void RenderMeshDistanceFieldVisualization(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FDistanceFieldAOParameters& Parameters);

	bool ShouldRenderLumenDiffuseGI(const FViewInfo& View) const;

	FSSDSignalTextures RenderLumenScreenProbeGather(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures,
		const ScreenSpaceRayTracing::FPrevSceneColorMip& PrevSceneColorMip,
		const FViewInfo& View,
		FPreviousViewInfo* PreviousViewInfos,
		bool bSSGI,
		bool& bLumenUseDenoiserComposite,
		class FLumenMeshSDFGridParameters& MeshSDFGridParameters);

	void RenderScreenProbeGatherVisualizeTraces(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef SceneColor);

	void RenderScreenProbeGatherVisualizeHardwareTraces(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef SceneColor);

	void RenderLumenProbe(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const LumenProbeHierarchy::FHierarchyParameters& HierarchyParameters,
		const LumenProbeHierarchy::FIndirectLightingAtlasParameters& IndirectLightingAtlasParameters,
		const LumenProbeHierarchy::FEmitProbeParameters& EmitProbeParameters,
		const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
		bool bUseRadianceCache);

	void RenderLumenProbeOcclusion(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const HybridIndirectLighting::FCommonParameters& CommonParameters,
		const LumenProbeHierarchy::FIndirectLightingProbeOcclusionParameters& ProbeOcclusionParameters);

	FRDGTextureRef RenderLumenReflections(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FSceneTextureParameters& SceneTextures,
		const class FLumenMeshSDFGridParameters& MeshSDFGridParameters,
		FLumenReflectionCompositeParameters& OutCompositeParameters);

	void RenderLumenSceneVisualization(FRDGBuilder& GraphBuilder);
	void RenderLumenRadianceCacheVisualization(FRDGBuilder& GraphBuilder);
	void LumenScenePDIVisualization();
	void LumenRadianceCachePDIVisualization();

	void RenderRadianceCache(
		FRDGBuilder& GraphBuilder, 
		const FLumenCardTracingInputs& TracingInputs, 
		const FViewInfo& View, 
		const class LumenProbeHierarchy::FHierarchyParameters* ProbeHierarchyParameters,
		const class FScreenProbeParameters* ScreenProbeParameters,
		LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters);

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
	 * Have the lights been injected into the light grid?
	 */
	bool AreLightsInLightGrid() const;


	/** Add a clustered deferred shading lighting render pass.	Note: in the future it should take the RenderGraph builder as argument */
	void AddClusteredDeferredShadingPass(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FSortedLightSetSceneInfo& SortedLightsSet);

	/** Renders the lights in SortedLights in the range [TiledDeferredLightsStart, TiledDeferredLightsEnd) using tiled deferred shading. */
	void RenderTiledDeferredLighting(
		FRDGBuilder& GraphBuilder,
		FMinimalSceneTextures& SceneTextures,
		const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights,
		int32 TiledDeferredLightsStart,
		int32 TiledDeferredLightsEnd,
		const FSimpleLightArray& SimpleLights);

	/** Renders the scene's lighting. */
	void RenderLights(
		FRDGBuilder& GraphBuilder,
		FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
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
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef LightingChannelsTexture);
	
	/** Renders the scene's translucency passes. */
	void RenderTranslucency(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		FSeparateTranslucencyTextures* OutSeparateTranslucencyTextures,
		ETranslucencyView ViewsToRender);

	/** Renders the scene's translucency given a specific pass. */
	void RenderTranslucencyInner(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		FSeparateTranslucencyTextures* OutSeparateTranslucencyTextures,
		ETranslucencyView ViewsToRender,
		FRDGTextureRef SceneColorCopyTexture,
		ETranslucencyPass::Type TranslucencyPass);

	/** Renders the scene's light shafts */
	FRDGTextureRef RenderLightShaftOcclusion(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures);

	void RenderLightShaftBloom(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FSeparateTranslucencyTextures& OutSeparateTranslucencyTextures);

	bool ShouldRenderVelocities() const;

	void RenderVelocities(
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		EVelocityPass VelocityPass,
		bool bForceVelocity);

	bool ShouldRenderDistortion() const;
	void RenderDistortion(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneColorTexture, FRDGTextureRef SceneDepthTexture);

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
	void RenderIndirectCapsuleShadows(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures) const;

	/** Renders capsule shadows for movable skylights, using the cone of visibility (bent normal) from DFAO. */
	void RenderCapsuleShadowsForMovableSkylight(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef& BentNormalOutput) const;

	void RenderShadowProjections(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		const FLightSceneInfo* LightSceneInfo,
		const FHairStrandsVisibilityViews* HairVisibilityViews,
		bool bProjectingForForwardShading);

	void RenderDeferredShadowProjections(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FTranslucencyLightingVolumeTextures& TranslucencyLightingVolumeTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef ScreenShadowMaskSubPixelTexture,
		const FHairStrandsRenderingData* HairDatas,
		bool& bInjectedTranslucentVolume);

	void RenderForwardShadowProjections(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		FRDGTextureRef& ForwardScreenSpaceShadowMask,
		FRDGTextureRef& ForwardScreenSpaceShadowMaskSubPixel,
		const FHairStrandsRenderingData* InHairDatas);

	/** Used by RenderLights to render a light function to the attenuation buffer. */
	bool RenderLightFunction(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		bool bLightAttenuationCleared,
		bool bProjectingForForwardShading);

	/** Renders a light function indicating that whole scene shadowing being displayed is for previewing only, and will go away in game. */
	bool RenderPreviewShadowsIndicator(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
		const FLightSceneInfo* LightSceneInfo,
		FRDGTextureRef ScreenShadowMaskTexture,
		bool bLightAttenuationCleared);

	/** Renders a light function with the given material. */
	bool RenderLightFunctionForMaterial(
		FRDGBuilder& GraphBuilder,
		const FMinimalSceneTextures& SceneTextures,
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
		const FMinimalSceneTextures& SceneTextures,
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
		const FMinimalSceneTextures& SceneTextures,
		const FSimpleLightArray& SimpleLights);

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
		FRDGTexture*& OutLocalShadowedLightScattering,
		FRDGTextureRef ConservativeDepthTexture);

	void RenderLightFunctionForVolumetricFog(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
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
		float VolumetricFogDistance,
		bool bVoxelizeEmissive);

	void ComputeVolumetricFog(FRDGBuilder& GraphBuilder);

	void VisualizeVolumetricLightmap(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures);

	/** Render image based reflections (SSR, Env, SkyLight) without compute shaders */
	void RenderStandardDeferredImageBasedReflections(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, bool bReflectionEnv, const TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO, TRefCountPtr<IPooledRenderTarget>& VelocityRT);

	bool HasDeferredPlanarReflections(const FViewInfo& View) const;
	void RenderDeferredPlanarReflections(FRDGBuilder& GraphBuilder, const FSceneTextureParameters& SceneTextures, const FViewInfo& View, FRDGTextureRef& ReflectionsOutput);

	bool ShouldDoReflectionEnvironment() const;
	
	bool ShouldRenderDistanceFieldAO() const;

	/** Whether distance field global data structures should be prepared for features that use it. */
	bool ShouldPrepareForDistanceFieldShadows() const;
	bool ShouldPrepareForDistanceFieldAO() const;
	bool ShouldPrepareForDFInsetIndirectShadow() const;

	bool ShouldPrepareDistanceFieldScene() const;
	bool ShouldPrepareGlobalDistanceField() const;
	bool ShouldPrepareHeightFieldScene() const;

	void UpdateGlobalDistanceFieldObjectBuffers(FRDGBuilder& GraphBuilder);
	void UpdateGlobalHeightFieldObjectBuffers(FRDGBuilder& GraphBuilder);
	void AddOrRemoveSceneHeightFieldPrimitives(bool bSkipAdd = false);
	void PrepareDistanceFieldScene(FRDGBuilder& GraphBuilder, bool bSplitDispatch);

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
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderRayTracingDeferredReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
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
		const FMinimalSceneTextures& SceneTextures,
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

	void GenerateSkyLightVisibilityRays(FRDGBuilder& GraphBuilder, FRDGBufferRef& SkyLightVisibilityRays, FIntVector& Dimensions);
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

	void BuildVarianceMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FTextureRHIRef MeanAndDeviationTexture,
		FRWBuffer& VarianceMipTree, FIntVector& VarianceMipTreeDimensions);

	void VisualizeVarianceMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FRWBuffer& VarianceMipTree, FIntVector VarianceMipTreeDimensions);

	void ComputePathCompaction(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHITexture* RadianceTexture, FRHITexture* SampleCountTexture, FRHITexture* PixelPositionTexture,
		FRHIUnorderedAccessView* RadianceSortedRedUAV, FRHIUnorderedAccessView* RadianceSortedGreenUAV, FRHIUnorderedAccessView* RadianceSortedBlueUAV, FRHIUnorderedAccessView* RadianceSortedAlphaUAV, FRHIUnorderedAccessView* SampleCountSortedUAV);

	void ComputeRayCount(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHITexture* RayCountPerPixelTexture);

	void WaitForRayTracingScene(FRDGBuilder& GraphBuilder);

	/** Debug ray tracing functions. */
	void RenderRayTracingDebug(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorOutputTexture);
	void RenderRayTracingBarycentrics(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorOutputTexture);

	bool GatherRayTracingWorldInstances(FRHICommandListImmediate& RHICmdList);
	bool DispatchRayTracingWorldUpdates(FRDGBuilder& GraphBuilder);
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
	static void PrepareRayTracingTranslucency(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDebug(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PreparePathTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingLumenDirectLighting(const FViewInfo& View,const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingScreenProbeGather(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareLumenHardwareRayTracingVisualize(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);

	// Versions for setting up the deferred material pipeline
	static void PrepareRayTracingReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDeferredReflectionsDeferredMaterial(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIlluminationDeferredMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);


	/** Lighting evaluation shader registration */
	static FRHIRayTracingShader* GetRayTracingLightingMissShader(FViewInfo& View);

	const FRHITransition* RayTracingDynamicGeometryUpdateEndTransition = nullptr; // Signaled when all AS for this frame are built

#endif // RHI_RAYTRACING

	/** Set to true if lights were injected into the light grid (this controlled by somewhat complex logic, this flag is used to cross-check). */
	bool bAreLightsInLightGrid;
};

DECLARE_CYCLE_STAT_EXTERN(TEXT("PrePass"), STAT_CLM_PrePass, STATGROUP_CommandListMarkers, );