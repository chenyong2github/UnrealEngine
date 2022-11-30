// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShadowRendering.h: Shadow rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Templates/RefCounting.h"
#include "RHI.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "HitProxies.h"
#include "ConvexVolume.h"
#include "RHIStaticStates.h"
#include "RendererInterface.h"
#include "SceneManagement.h"
#include "ScenePrivateBase.h"
#include "SceneCore.h"
#include "GlobalShader.h"
#include "SystemTextures.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "ShaderParameterUtils.h"
#include "LightRendering.h"
#include "LightPropagationVolume.h"
#include "HairStrands/HairStrandsRendering.h"

ENGINE_API IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);

class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FProjectedShadowInfo;
class FScene;
class FSceneRenderer;
class FViewInfo;

/** Renders a cone with a spherical cap, used for rendering spot lights in deferred passes. */
extern void DrawStencilingCone(const FMatrix& ConeToWorld, float ConeAngle, float SphereRadius, const FVector& PreViewTranslation);

class FShadowDepthBasePS;

/** 
 * Overrides a material used for shadow depth rendering with the default material when appropriate.
 * Overriding in this manner can reduce state switches and the number of shaders that have to be compiled.
 * This logic needs to stay in sync with shadow depth shader ShouldCache logic.
 */
void OverrideWithDefaultMaterialForShadowDepth(
	const FMaterialRenderProxy*& InOutMaterialRenderProxy, 
	const FMaterial*& InOutMaterialResource,
	bool bReflectiveShadowmap,
	ERHIFeatureLevel::Type InFeatureLevel
	);

enum EShadowDepthRenderMode
{
	/** The render mode used by regular shadows */
	ShadowDepthRenderMode_Normal,

	/** The render mode used when injecting emissive-only objects into the RSM. */
	ShadowDepthRenderMode_EmissiveOnly,

	/** The render mode used when rendering volumes which block global illumination. */
	ShadowDepthRenderMode_GIBlockingVolumes,
};

class FShadowDepthType
{
public:
	bool bDirectionalLight;
	bool bOnePassPointLightShadow;
	bool bReflectiveShadowmap;

	FShadowDepthType(bool bInDirectionalLight, bool bInOnePassPointLightShadow, bool bInReflectiveShadowmap) 
	: bDirectionalLight(bInDirectionalLight)
	, bOnePassPointLightShadow(bInOnePassPointLightShadow)
	, bReflectiveShadowmap(bInReflectiveShadowmap)
	{}

	inline bool operator==(const FShadowDepthType& rhs) const
	{
		if (bDirectionalLight != rhs.bDirectionalLight || 
			bOnePassPointLightShadow != rhs.bOnePassPointLightShadow || 
			bReflectiveShadowmap != rhs.bReflectiveShadowmap) 
		{
			return false;
		}

		return true;
	}
};

extern FShadowDepthType CSMShadowDepthType;

class FShadowDepthPassMeshProcessor : public FMeshPassProcessor
{
public:

	FShadowDepthPassMeshProcessor(
		const FScene* Scene, 
		const FSceneView* InViewIfDynamicMeshCommand, 
		const TUniformBufferRef<FViewUniformShaderParameters>& InViewUniformBuffer,
		FRHIUniformBuffer* InPassUniformBuffer,
		FShadowDepthType InShadowDepthType,
		FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;

private:

	template<bool bRenderReflectiveShadowMap>
	void Process(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FShadowDepthType ShadowDepthType;
};

enum EShadowDepthCacheMode
{
	SDCM_MovablePrimitivesOnly,
	SDCM_StaticPrimitivesOnly,
	SDCM_Uncached
};

inline bool IsShadowCacheModeOcclusionQueryable(EShadowDepthCacheMode CacheMode)
{
	// SDCM_StaticPrimitivesOnly shadowmaps are emitted randomly as the cache needs to be updated,
	// And therefore not appropriate for occlusion queries which are latent and therefore need to be stable.
	// Only one the cache modes from ComputeWholeSceneShadowCacheModes should be queryable
	return CacheMode != SDCM_StaticPrimitivesOnly;
}

class FShadowMapRenderTargets
{
public:
	TArray<IPooledRenderTarget*, SceneRenderingAllocator> ColorTargets;
	IPooledRenderTarget* DepthTarget;

	FShadowMapRenderTargets() :
		DepthTarget(nullptr)
	{}

	FIntPoint GetSize() const
	{
		if (DepthTarget)
		{
			return DepthTarget->GetDesc().Extent;
		}
		else 
		{
			check(ColorTargets.Num() > 0);
			return ColorTargets[0]->GetDesc().Extent;
		}
	}

	void CopyReferencesFromRenderTargets(const FShadowMapRenderTargetsRefCounted& SourceTargets)
	{
		int32 ColorTargetsCount = SourceTargets.ColorTargets.Num();
		ColorTargets.Empty(ColorTargetsCount);
		ColorTargets.AddDefaulted(ColorTargetsCount);
		for (int32 TargetIndex = 0; TargetIndex < ColorTargetsCount; TargetIndex++)
		{
			ColorTargets[TargetIndex] = SourceTargets.ColorTargets[TargetIndex].GetReference();
		}

		DepthTarget = SourceTargets.DepthTarget.GetReference();
	}
};

typedef TFunctionRef<void(FRHICommandList& RHICmdList, bool bFirst)> FBeginShadowRenderPassFunction;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FShadowDepthPassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_STRUCT(FLpvWriteUniformBufferParameters, LPV)
	SHADER_PARAMETER(FMatrix, ProjectionMatrix)
	SHADER_PARAMETER(FMatrix, ViewMatrix)
	SHADER_PARAMETER(FVector4, ShadowParams)
	SHADER_PARAMETER(float, bClampToNearPlane)
	SHADER_PARAMETER_ARRAY(FMatrix, ShadowViewProjectionMatrices, [6])
	SHADER_PARAMETER_ARRAY(FMatrix, ShadowViewMatrices, [6])

	SHADER_PARAMETER_UAV(RWStructuredBuffer<VplListEntry>, RWGvListBuffer)
	SHADER_PARAMETER_UAV(RWByteAddressBuffer, RWGvListHeadBuffer)
	SHADER_PARAMETER_UAV(RWStructuredBuffer<VplListEntry>, RWVplListBuffer)
	SHADER_PARAMETER_UAV(RWByteAddressBuffer, RWVplListHeadBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileShadowDepthPassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER(FMatrix, ProjectionMatrix)
	SHADER_PARAMETER(FMatrix, ViewMatrix)
	SHADER_PARAMETER(FVector4, ShadowParams)
	SHADER_PARAMETER(float, bClampToNearPlane)
	SHADER_PARAMETER_ARRAY(FMatrix, ShadowViewProjectionMatrices, [6])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FShadowMeshDrawCommandPass
{
public:
	FMeshCommandOneFrameArray VisibleMeshDrawCommands;
	FRHIVertexBuffer* PrimitiveIdVertexBuffer;
};

/**
 * Information about a projected shadow.
 */
class FProjectedShadowInfo : public FRefCountedObject
{
public:
	typedef TArray<const FPrimitiveSceneInfo*,SceneRenderingAllocator> PrimitiveArrayType;

	/** The view to be used when rendering this shadow's depths. */
	FViewInfo* ShadowDepthView;

	TUniformBufferRef<FShadowDepthPassUniformParameters> ShadowDepthPassUniformBuffer;
	TUniformBufferRef<FMobileShadowDepthPassUniformParameters> MobileShadowDepthPassUniformBuffer;

	/** The depth or color targets this shadow was rendered to. */
	FShadowMapRenderTargets RenderTargets;

	EShadowDepthCacheMode CacheMode;

	/** The main view this shadow must be rendered in, or NULL for a view independent shadow. */
	FViewInfo* DependentView;

	/** Index of the shadow into FVisibleLightInfo::AllProjectedShadows. */
	int32 ShadowId;

	/** A translation that is applied to world-space before transforming by one of the shadow matrices. */
	FVector PreShadowTranslation;

	/** The effective view matrix of the shadow, used as an override to the main view's view matrix when rendering the shadow depth pass. */
	FMatrix ShadowViewMatrix;

	/** 
	 * Matrix used for rendering the shadow depth buffer.  
	 * Note that this does not necessarily contain all of the shadow casters with CSM, since the vertex shader flattens them onto the near plane of the projection.
	 */
	FMatrix SubjectAndReceiverMatrix;
	FMatrix ReceiverMatrix;

	FMatrix InvReceiverMatrix;

	float InvMaxSubjectDepth;

	/** 
	 * Subject depth extents, in world space units. 
	 * These can be used to convert shadow depth buffer values back into world space units.
	 */
	float MaxSubjectZ;
	float MinSubjectZ;

	/** Frustum containing all potential shadow casters. */
	FConvexVolume CasterFrustum;
	FConvexVolume ReceiverFrustum;

	float MinPreSubjectZ;

	FSphere ShadowBounds;

	FShadowCascadeSettings CascadeSettings;

	/** 
	 * X and Y position of the shadow in the appropriate depth buffer.  These are only initialized after the shadow has been allocated. 
	 * The actual contents of the shadowmap are at X + BorderSize, Y + BorderSize.
	 */
	uint32 X;
	uint32 Y;

	/** 
	 * Resolution of the shadow, excluding the border. 
	 * The full size of the region allocated to this shadow is therefore ResolutionX + 2 * BorderSize, ResolutionY + 2 * BorderSize.
	 */
	uint32 ResolutionX;
	uint32 ResolutionY;

	/** Size of the border, if any, used to allow filtering without clamping for shadows stored in an atlas. */
	uint32 BorderSize;

	/** The largest percent of either the width or height of any view. */
	float MaxScreenPercent;

	/** Fade Alpha per view. */
	TArray<float, TInlineAllocator<2> > FadeAlphas;

	/** Whether the shadow has been allocated in the shadow depth buffer, and its X and Y properties have been initialized. */
	uint32 bAllocated : 1;

	/** Whether the shadow's projection has been rendered. */
	uint32 bRendered : 1;

	/** Whether the shadow has been allocated in the preshadow cache, so its X and Y properties offset into the preshadow cache depth buffer. */
	uint32 bAllocatedInPreshadowCache : 1;

	/** Whether the shadow is in the preshadow cache and its depths are up to date. */
	uint32 bDepthsCached : 1;

	// redundant to LightSceneInfo->Proxy->GetLightType() == LightType_Directional, could be made ELightComponentType LightType
	uint32 bDirectionalLight : 1;

	/** Whether the shadow is a point light shadow that renders all faces of a cubemap in one pass. */
	uint32 bOnePassPointLightShadow : 1;

	/** Whether this shadow affects the whole scene or only a group of objects. */
	uint32 bWholeSceneShadow : 1;

	/** Whether the shadow needs to render reflective shadow maps. */ 
	uint32 bReflectiveShadowmap : 1; 

	/** Whether this shadow should support casting shadows from translucent surfaces. */
	uint32 bTranslucentShadow : 1;

	/** Whether the shadow will be computed by ray tracing the distance field. */
	uint32 bRayTracedDistanceField : 1;

	/** Whether this is a per-object shadow that should use capsule shapes to shadow instead of the mesh's triangles. */
	uint32 bCapsuleShadow : 1;

	/** Whether the shadow is a preshadow or not.  A preshadow is a per object shadow that handles the static environment casting on a dynamic receiver. */
	uint32 bPreShadow : 1;

	/** To not cast a shadow on the ground outside the object and having higher quality (useful for first person weapon). */
	uint32 bSelfShadowOnly : 1;

	/** Whether the shadow is a per object shadow or not. */
	uint32 bPerObjectOpaqueShadow : 1;

	/** Whether turn on back-lighting transmission. */
	uint32 bTransmission : 1;

	/** Whether turn on hair strands deep shadow. */
	uint32 bHairStrandsDeepShadow : 1;

	/** View projection matrices for each cubemap face, used by one pass point light shadows. */
	TArray<FMatrix> OnePassShadowViewProjectionMatrices;

	/** View matrices for each cubemap face, used by one pass point light shadows. */
	TArray<FMatrix> OnePassShadowViewMatrices;
	
	/** Controls fading out of per-object shadows in the distance to avoid casting super-sharp shadows far away. */
	float PerObjectShadowFadeStart;
	float InvPerObjectShadowFadeLength;

public:

	// default constructor
	FProjectedShadowInfo();

	/**
	 * for a per-object shadow. e.g. translucent particle system or a dynamic object in a precomputed shadow situation
	 * @param InParentSceneInfo must not be 0
	 * @return success, if false the shadow project is invalid and the projection should nto be created
	 */
	bool SetupPerObjectProjection(
		FLightSceneInfo* InLightSceneInfo,
		const FPrimitiveSceneInfo* InParentSceneInfo,
		const FPerObjectProjectedShadowInitializer& Initializer,
		bool bInPreShadow,
		uint32 InResolutionX,
		uint32 MaxShadowResolutionY,
		uint32 InBorderSize,
		float InMaxScreenPercent,
		bool bInTranslucentShadow
		);

	/** for a whole-scene shadow. */
	void SetupWholeSceneProjection(
		FLightSceneInfo* InLightSceneInfo,
		FViewInfo* InDependentView,
		const FWholeSceneProjectedShadowInitializer& Initializer,
		uint32 InResolutionX,
		uint32 InResolutionY,
		uint32 InBorderSize,
		bool bInReflectiveShadowMap
		);

	float GetShaderDepthBias() const { return ShaderDepthBias; }
	float GetShaderSlopeDepthBias() const { return ShaderSlopeDepthBias; }
	float GetShaderMaxSlopeDepthBias() const { return ShaderMaxSlopeDepthBias; }
	float GetShaderReceiverDepthBias() const;

	/**
	 * Renders the shadow subject depth.
	 */
	void RenderDepth(FRHICommandListImmediate& RHICmdList, class FSceneRenderer* SceneRenderer, FBeginShadowRenderPassFunction BeginShadowRenderPass, bool bDoParallelDispatch);

	void SetStateForView(FRHICommandList& RHICmdList) const;

	/** Set state for depth rendering */
	void SetStateForDepth(FMeshPassProcessorRenderState& DrawRenderState) const;

	void ClearDepth(FRHICommandList& RHICmdList, class FSceneRenderer* SceneRenderer, int32 NumColorTextures, FRHITexture** ColorTextures, FRHITexture* DepthTexture, bool bPerformClear);

	/** Renders shadow maps for translucent primitives. */
	void RenderTranslucencyDepths(FRHICommandList& RHICmdList, class FSceneRenderer* SceneRenderer);

	static FRHIBlendState* GetBlendStateForProjection(
		int32 ShadowMapChannel,
		bool bIsWholeSceneDirectionalShadow,
		bool bUseFadePlane,
		bool bProjectingForForwardShading,
		bool bMobileModulatedProjections);

	FRHIBlendState* GetBlendStateForProjection(bool bProjectingForForwardShading, bool bMobileModulatedProjections) const;

	/**
	 * Projects the shadow onto the scene for a particular view.
	 */
	void RenderProjection(FRHICommandListImmediate& RHICmdList, int32 ViewIndex, const class FViewInfo* View, const class FSceneRenderer* SceneRender, bool bProjectingForForwardShading, bool bMobile, const struct FHairStrandsVisibilityData* HairVisibilityData, const struct FHairStrandsMacroGroupDatas* HairMacroGroupData) const;

	FRDGTextureRef BeginRenderRayTracedDistanceFieldProjection(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		const FViewInfo& View) const;

	/** Renders ray traced distance field shadows. */
	void RenderRayTracedDistanceFieldProjection(
		FRDGBuilder& GraphBuilder,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
		FRDGTextureRef ScreenShadowMaskTexture,
		FRDGTextureRef SceneDepthTexture,
		const FViewInfo& View,
		FIntRect ScissorRect,
		bool bProjectingForForwardShading) const;

	/** Render one pass point light shadow projections. */
	void RenderOnePassPointLightProjection(FRHICommandListImmediate& RHICmdList, int32 ViewIndex, const FViewInfo& View, bool bProjectingForForwardShading, const FHairStrandsVisibilityData* HairVisibilityData, const struct FHairStrandsMacroGroupDatas* HairMacroGroupData) const;

	/**
	 * Renders the projected shadow's frustum wireframe with the given FPrimitiveDrawInterface.
	 */
	void RenderFrustumWireframe(FPrimitiveDrawInterface* PDI) const;

	/**
	 * Adds a primitive to the shadow's subject list.
	 */
	void AddSubjectPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, TArray<FViewInfo>* ViewArray, ERHIFeatureLevel::Type FeatureLevel, bool bRecordShadowSubjectForMobileShading);

	uint64 AddSubjectPrimitive_AnyThread(
		const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact,
		TArray<FViewInfo>* ViewArray,
		ERHIFeatureLevel::Type FeatureLevel,
		struct FAddSubjectPrimitiveStats& OutStats,
		struct FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer) const;

	void PresizeSubjectPrimitiveArrays(struct FAddSubjectPrimitiveStats const& Stats);

	void FinalizeAddSubjectPrimitive(
		struct FAddSubjectPrimitiveOp const& Op,
		TArray<FViewInfo>* ViewArray,
		ERHIFeatureLevel::Type FeatureLevel,
		struct FFinalizeAddSubjectPrimitiveContext& Context);

	/**
	* @return TRUE if this shadow info has any casting subject prims to render
	*/
	bool HasSubjectPrims() const;

	/**
	 * Adds a primitive to the shadow's receiver list.
	 */
	void AddReceiverPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo);

	/** Gathers dynamic mesh elements for all the shadow's primitives arrays. */
	void GatherDynamicMeshElements(FSceneRenderer& Renderer, class FVisibleLightInfo& VisibleLightInfo, TArray<const FSceneView*>& ReusedViewsArray, 
		FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer);

	void SetupMeshDrawCommandsForShadowDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer);

	void SetupMeshDrawCommandsForProjectionStenciling(FSceneRenderer& Renderer);

	/** 
	 * @param View view to check visibility in
	 * @return true if this shadow info has any subject prims visible in the view
	 */
	bool SubjectsVisible(const FViewInfo& View) const;

	/** Clears arrays allocated with the scene rendering allocator. */
	void ClearTransientArrays();
	
	/** Hash function. */
	friend uint32 GetTypeHash(const FProjectedShadowInfo* ProjectedShadowInfo)
	{
		return PointerHash(ProjectedShadowInfo);
	}

	/** Returns a matrix that transforms a screen space position into shadow space. */
	FMatrix GetScreenToShadowMatrix(const FSceneView& View) const
	{
		return GetScreenToShadowMatrix(View, X, Y, ResolutionX, ResolutionY);
	}

	/** Returns a matrix that transforms a screen space position into shadow space. 
		Additional parameters allow overriding of shadow's tile location.
		Used with modulated shadows to reduce precision problems when calculating ScreenToShadow in pixel shader.
	*/
	FMatrix GetScreenToShadowMatrix(const FSceneView& View, uint32 TileOffsetX, uint32 TileOffsetY, uint32 TileResolutionX, uint32 TileResolutionY) const;

	/** Returns a matrix that transforms a world space position into shadow space. */
	FMatrix GetWorldToShadowMatrix(FVector4& ShadowmapMinMax, const FIntPoint* ShadowBufferResolutionOverride = nullptr) const;

	/** Returns the resolution of the shadow buffer used for this shadow, based on the shadow's type. */
	FIntPoint GetShadowBufferResolution() const
	{
		return RenderTargets.GetSize();
	}

	/** Computes and updates ShaderDepthBias and ShaderSlopeDepthBias */
	void UpdateShaderDepthBias();

	/** How large the soft PCF comparison should be, similar to DepthBias, before this was called TransitionScale and 1/Size */
	float ComputeTransitionSize() const;

	inline bool IsWholeSceneDirectionalShadow() const 
	{ 
		return bWholeSceneShadow && CascadeSettings.ShadowSplitIndex >= 0 && bDirectionalLight; 
	}

	inline bool IsWholeScenePointLightShadow() const
	{
		return bWholeSceneShadow && ( LightSceneInfo->Proxy->GetLightType() == LightType_Point || LightSceneInfo->Proxy->GetLightType() == LightType_Rect );
	}

	// 0 if Setup...() wasn't called yet
	const FLightSceneInfo& GetLightSceneInfo() const { return *LightSceneInfo; }
	const FLightSceneInfoCompact& GetLightSceneInfoCompact() const { return LightSceneInfoCompact; }
	/**
	 * Parent primitive of the shadow group that created this shadow, if not a bWholeSceneShadow.
	 * 0 if Setup...() wasn't called yet
	 */	
	const FPrimitiveSceneInfo* GetParentSceneInfo() const { return ParentSceneInfo; }

	/** Creates a new view from the pool and caches it in ShadowDepthView for depth rendering. */
	void SetupShadowDepthView(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer);

	FShadowDepthType GetShadowDepthType() const 
	{
		return FShadowDepthType(bDirectionalLight, bOnePassPointLightShadow, bReflectiveShadowmap);
	}

	/**
	* Setup uniformbuffers and update Primitive Shader Data
	*/
	void SetupShadowUniformBuffers(FRHICommandListImmediate& RHICmdList, FScene* Scene, FLightPropagationVolume* LPV = nullptr);

	/**
	* Ensure Cached Shadowmap is in EReadable state
	*/
	void TransitionCachedShadowmap(FRHICommandListImmediate& RHICmdList, FScene* Scene);


private:
	// 0 if Setup...() wasn't called yet
	const FLightSceneInfo* LightSceneInfo;
	FLightSceneInfoCompact LightSceneInfoCompact;

	/**
	 * Parent primitive of the shadow group that created this shadow, if not a bWholeSceneShadow.
	 * 0 if Setup...() wasn't called yet or for whole scene shadows
	 */	
	const FPrimitiveSceneInfo* ParentSceneInfo;

	/** dynamic shadow casting elements */
	PrimitiveArrayType DynamicSubjectPrimitives;
	/** For preshadows, this contains the receiver primitives to mask the projection to. */
	PrimitiveArrayType ReceiverPrimitives;
	/** Subject primitives with translucent relevance. */
	PrimitiveArrayType SubjectTranslucentPrimitives;

	/** Dynamic mesh elements for subject primitives. */
	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator> DynamicSubjectMeshElements;
	/** Dynamic mesh elements for translucent subject primitives. */
	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator> DynamicSubjectTranslucentMeshElements;

	TArray<const FStaticMeshBatch*, SceneRenderingAllocator> SubjectMeshCommandBuildRequests;

	/** Number of elements of DynamicSubjectMeshElements meshes. */
	int32 NumDynamicSubjectMeshElements;

	/** Number of elements of SubjectMeshCommandBuildRequests meshes. */
	int32 NumSubjectMeshCommandBuildRequestElements;

	FMeshCommandOneFrameArray ShadowDepthPassVisibleCommands;
	FParallelMeshDrawCommandPass ShadowDepthPass;

	TArray<FShadowMeshDrawCommandPass, TInlineAllocator<2>> ProjectionStencilingPasses;

	FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
	FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
	bool NeedsShaderInitialisation;

	/**
	 * Bias during in shadowmap rendering, stored redundantly for better performance 
	 * Set by UpdateShaderDepthBias(), get with GetShaderDepthBias(), -1 if not set
	 */
	float ShaderDepthBias;
	float ShaderSlopeDepthBias;
	float ShaderMaxSlopeDepthBias;

	void CopyCachedShadowMap(FRHICommandList& RHICmdList, const FMeshPassProcessorRenderState& DrawRenderState, FSceneRenderer* SceneRenderer, const FViewInfo& View);

	/**
	* Renders the shadow subject depth, to a particular hacked view
	*/
	void RenderDepthInner(FRHICommandListImmediate& RHICmdList, class FSceneRenderer* SceneRenderer, FBeginShadowRenderPassFunction BeginShadowRenderPass, bool bDoParallelDispatch);

	/**
	* Modifies the passed in view for this shadow
	*/
	void ModifyViewForShadow(FRHICommandList& RHICmdList, FViewInfo* FoundView) const;

	/**
	* Finds a relevant view for a shadow
	*/
	FViewInfo* FindViewForShadow(FSceneRenderer* SceneRenderer) const;

	void AddCachedMeshDrawCommandsForPass(
		int32 PrimitiveIndex,
		const FPrimitiveSceneInfo* InPrimitiveSceneInfo,
		const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance,
		const FStaticMeshBatch& StaticMesh,
		const FScene* Scene,
		EMeshPass::Type PassType,
		FMeshCommandOneFrameArray& VisibleMeshCommands,
		TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& MeshCommandBuildRequests,
		int32& NumMeshCommandBuildRequestElements);

	void AddCachedMeshDrawCommands_AnyThread(
		const FScene* Scene,
		const FStaticMeshBatchRelevance& RESTRICT StaticMeshRelevance,
		int32 StaticMeshIdx,
		int32& NumAcceptedStaticMeshes,
		struct FAddSubjectPrimitiveResult& OutResult,
		struct FAddSubjectPrimitiveStats& OutStats,
		struct FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer) const;

	/** Will return if we should draw the static mesh for the shadow, and will perform lazy init of primitive if it wasn't visible */
	bool ShouldDrawStaticMeshes(FViewInfo& InCurrentView, FPrimitiveSceneInfo* InPrimitiveSceneInfo);

	bool ShouldDrawStaticMeshes_AnyThread(
		FViewInfo& InCurrentView,
		const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact,
		bool bMayBeFading,
		bool bNeedUpdateStaticMeshes,
		struct FAddSubjectPrimitiveResult& OutResult,
		struct FAddSubjectPrimitiveStats& OutStats,
		struct FAddSubjectPrimitiveOverflowedIndices& OverflowBuffer) const;

	void GetShadowTypeNameForDrawEvent(FString& TypeName) const;

	/** Updates object buffers needed by ray traced distance field shadows. */
	int32 UpdateShadowCastingObjectBuffers() const;

	/** Gathers dynamic mesh elements for the given primitive array. */
	void GatherDynamicMeshElementsArray(
		FViewInfo* FoundView,
		FSceneRenderer& Renderer, 
		FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
		FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
		FGlobalDynamicReadBuffer& DynamicReadBuffer,
		const PrimitiveArrayType& PrimitiveArray, 
		const TArray<const FSceneView*>& ReusedViewsArray,
		TArray<FMeshBatchAndRelevance,SceneRenderingAllocator>& OutDynamicMeshElements,
		int32& OutNumDynamicSubjectMeshElements);

	void SetupFrustumForProjection(const FViewInfo* View, TArray<FVector4, TInlineAllocator<8>>& OutFrustumVertices, bool& bOutCameraInsideShadowFrustum, FPlane* OutPlanes) const;

	void SetupProjectionStencilMask(
		FRHICommandListImmediate& RHICmdList,
		const FViewInfo* View, 
		int32 ViewIndex,
		const class FSceneRenderer* SceneRender,
		const TArray<FVector4, TInlineAllocator<8>>& FrustumVertices,
		bool bMobileModulatedProjections,
		bool bCameraInsideShadowFrustum) const;

	friend class FShadowDepthVS;
	friend class FShadowDepthBasePS;
	friend class FShadowVolumeBoundProjectionVS;
	friend class FShadowProjectionPS;
};

/**
* A generic vertex shader for projecting a shadow depth buffer onto the scene.
*/
class FShadowProjectionVertexShaderInterface : public FGlobalShader
{
	DECLARE_TYPE_LAYOUT(FShadowProjectionVertexShaderInterface, NonVirtual);
public:
	FShadowProjectionVertexShaderInterface() {}
	FShadowProjectionVertexShaderInterface(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{ }

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo) {}

	
	
};

/**
* A vertex shader for projecting a shadow depth buffer onto the scene.
*/
class FShadowVolumeBoundProjectionVS : public FShadowProjectionVertexShaderInterface
{
	DECLARE_SHADER_TYPE(FShadowVolumeBoundProjectionVS,Global);
public:

	FShadowVolumeBoundProjectionVS() {}
	FShadowVolumeBoundProjectionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FShadowProjectionVertexShaderInterface(Initializer) 
	{
		StencilingGeometryParameters.Bind(Initializer.ParameterMap);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
	
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShadowProjectionVertexShaderInterface::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_TRANSFORM"), (uint32)1);
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo);

private:
	LAYOUT_FIELD(FStencilingGeometryShaderParameters, StencilingGeometryParameters);
};

class FShadowProjectionNoTransformVS : public FShadowProjectionVertexShaderInterface
{
	DECLARE_SHADER_TYPE(FShadowProjectionNoTransformVS,Global);
public:
	FShadowProjectionNoTransformVS() {}
	FShadowProjectionNoTransformVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FShadowProjectionVertexShaderInterface(Initializer) 
	{
	}

	/**
	 * Add any defines required by the shader
	 * @param OutEnvironment - shader environment to modify
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShadowProjectionVertexShaderInterface::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_TRANSFORM"), (uint32)0);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	void SetParameters(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), ViewUniformBuffer);
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FProjectedShadowInfo*)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
	}
};

/**
 * FShadowProjectionPixelShaderInterface - used to handle templated versions
 */

class FShadowProjectionPixelShaderInterface : public FGlobalShader
{
	DECLARE_TYPE_LAYOUT(FShadowProjectionPixelShaderInterface, NonVirtual);
public:

	FShadowProjectionPixelShaderInterface() 
		:	FGlobalShader()
	{}

	/**
	 * Constructor - binds all shader params and initializes the sample offsets
	 * @param Initializer - init data from shader compiler
	 */
	FShadowProjectionPixelShaderInterface(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FGlobalShader(Initializer)
	{ }

	/**
	 * Sets the current pixel shader params
	 * @param View - current view
	 * @param ShadowInfo - projected shadow info for a single light
	 */
	void SetParameters(
		FRHICommandList& RHICmdList, 
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{ 
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
	}
};

/** Shadow projection parameters used by multiple shaders. */
class FShadowProjectionShaderParameters
{
	DECLARE_TYPE_LAYOUT(FShadowProjectionShaderParameters, NonVirtual);
public:
	void Bind(const FShader::CompiledShaderInitializerType& Initializer)
	{
		const FShaderParameterMap& ParameterMap = Initializer.ParameterMap;
		ScreenToShadowMatrix.Bind(ParameterMap,TEXT("ScreenToShadowMatrix"));
		SoftTransitionScale.Bind(ParameterMap,TEXT("SoftTransitionScale"));
		ShadowBufferSize.Bind(ParameterMap,TEXT("ShadowBufferSize"));
		ShadowDepthTexture.Bind(ParameterMap,TEXT("ShadowDepthTexture"));
		ShadowDepthTextureSampler.Bind(ParameterMap,TEXT("ShadowDepthTextureSampler"));
		ProjectionDepthBias.Bind(ParameterMap,TEXT("ProjectionDepthBiasParameters"));
		FadePlaneOffset.Bind(ParameterMap,TEXT("FadePlaneOffset"));
		InvFadePlaneLength.Bind(ParameterMap,TEXT("InvFadePlaneLength"));
		ShadowTileOffsetAndSizeParam.Bind(ParameterMap, TEXT("ShadowTileOffsetAndSize"));
		LightPositionOrDirection.Bind(ParameterMap, TEXT("LightPositionOrDirection"));
		HairCategorizationTexture.Bind(ParameterMap, TEXT("HairCategorizationTexture"));
		PerObjectShadowFadeStart.Bind(ParameterMap, TEXT("PerObjectShadowFadeStart"));
		InvPerObjectShadowFadeLength.Bind(ParameterMap, TEXT("InvPerObjectShadowFadeLength"));
		ShadowNearAndFarDepth.Bind(ParameterMap, TEXT("ShadowNearAndFarDepth"));
		bCascadeUseFadePlane.Bind(ParameterMap, TEXT("bCascadeUseFadePlane"));
	}

	void Set(FRHICommandList& RHICmdList, FShader* Shader, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo, const FHairStrandsVisibilityData* HairVisibilityData, bool bModulatedShadows, bool bUseFadePlane)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		const FIntPoint ShadowBufferResolution = ShadowInfo->GetShadowBufferResolution();

		if (ShadowTileOffsetAndSizeParam.IsBound())
		{
			FVector2D InverseShadowBufferResolution(1.0f / ShadowBufferResolution.X, 1.0f / ShadowBufferResolution.Y);
			FVector4 ShadowTileOffsetAndSize(
				(ShadowInfo->BorderSize + ShadowInfo->X) * InverseShadowBufferResolution.X,
				(ShadowInfo->BorderSize + ShadowInfo->Y) * InverseShadowBufferResolution.Y,
				ShadowInfo->ResolutionX * InverseShadowBufferResolution.X,
				ShadowInfo->ResolutionY * InverseShadowBufferResolution.Y);
			SetShaderValue(RHICmdList, ShaderRHI, ShadowTileOffsetAndSizeParam, ShadowTileOffsetAndSize);
		}

		// Set the transform from screen coordinates to shadow depth texture coordinates.
		if (bModulatedShadows)
		{
			// UE-29083 : work around precision issues with ScreenToShadowMatrix on low end devices.
			const FMatrix ScreenToShadow = ShadowInfo->GetScreenToShadowMatrix(View, 0, 0, ShadowBufferResolution.X, ShadowBufferResolution.Y);
			SetShaderValue(RHICmdList, ShaderRHI, ScreenToShadowMatrix, ScreenToShadow);
		}
		else
		{
			const FMatrix ScreenToShadow = ShadowInfo->GetScreenToShadowMatrix(View);
			SetShaderValue(RHICmdList, ShaderRHI, ScreenToShadowMatrix, ScreenToShadow);
		}

		if (SoftTransitionScale.IsBound())
		{
			const float TransitionSize = ShadowInfo->ComputeTransitionSize();

			SetShaderValue(RHICmdList, ShaderRHI, SoftTransitionScale, FVector(0, 0, 1.0f / TransitionSize));
		}

		if (ShadowBufferSize.IsBound())
		{
			FVector2D ShadowBufferSizeValue(ShadowBufferResolution.X, ShadowBufferResolution.Y);

			SetShaderValue(RHICmdList, ShaderRHI, ShadowBufferSize,
				FVector4(ShadowBufferSizeValue.X, ShadowBufferSizeValue.Y, 1.0f / ShadowBufferSizeValue.X, 1.0f / ShadowBufferSizeValue.Y));
		}

		FRHITexture* ShadowDepthTextureValue;

		// Translucency shadow projection has no depth target
		if (ShadowInfo->RenderTargets.DepthTarget)
		{
			ShadowDepthTextureValue = ShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture.GetReference();
		}
		else
		{
			ShadowDepthTextureValue = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture.GetReference();
		}
			
		FRHISamplerState* DepthSamplerState = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();

		SetTextureParameter(RHICmdList, ShaderRHI, ShadowDepthTexture, ShadowDepthTextureSampler, DepthSamplerState, ShadowDepthTextureValue);		

		if (ShadowDepthTextureSampler.IsBound())
		{
			RHICmdList.SetShaderSampler(
				ShaderRHI, 
				ShadowDepthTextureSampler.GetBaseIndex(),
				DepthSamplerState
				);
		}

		SetShaderValue(RHICmdList, ShaderRHI, ProjectionDepthBias, FVector4(ShadowInfo->GetShaderDepthBias(), ShadowInfo->GetShaderSlopeDepthBias(), ShadowInfo->GetShaderReceiverDepthBias(), ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ));
		SetShaderValue(RHICmdList, ShaderRHI, FadePlaneOffset, ShadowInfo->CascadeSettings.FadePlaneOffset);

		if(InvFadePlaneLength.IsBound() && bUseFadePlane)
		{
			check(ShadowInfo->CascadeSettings.FadePlaneLength > 0);
			SetShaderValue(RHICmdList, ShaderRHI, InvFadePlaneLength, 1.0f / ShadowInfo->CascadeSettings.FadePlaneLength);
		}

		if (LightPositionOrDirection.IsBound())
		{
			const FVector LightDirection = ShadowInfo->GetLightSceneInfo().Proxy->GetDirection();
			const FVector LightPosition = ShadowInfo->GetLightSceneInfo().Proxy->GetPosition();
			const bool bIsDirectional = ShadowInfo->GetLightSceneInfo().Proxy->GetLightType() == LightType_Directional;
			SetShaderValue(RHICmdList, ShaderRHI, LightPositionOrDirection, bIsDirectional ? FVector4(LightDirection,0) : FVector4(LightPosition,1));
		}

		if (HairVisibilityData && HairVisibilityData->CategorizationTexture)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, HairCategorizationTexture, HairVisibilityData->CategorizationTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture);

			float DeviceZNear = 1;
			float DeviceZFar = 0;
			const bool bIsCascadedShadow = ShadowInfo->bDirectionalLight && !(ShadowInfo->bPerObjectOpaqueShadow || ShadowInfo->bPreShadow);
			if (bIsCascadedShadow)
			{
				FVector4 Near = View.ViewMatrices.GetProjectionMatrix().TransformFVector4(FVector4(0, 0, ShadowInfo->CascadeSettings.SplitNear));
				FVector4 Far = View.ViewMatrices.GetProjectionMatrix().TransformFVector4(FVector4(0, 0, ShadowInfo->CascadeSettings.SplitFar));
				DeviceZNear = Near.Z / Near.W;
				DeviceZFar = Far.Z / Far.W;
			}

			FVector2D SliceNearAndFarDepth;
			SliceNearAndFarDepth.X = DeviceZNear;
			SliceNearAndFarDepth.Y = DeviceZFar;
			SetShaderValue(RHICmdList, ShaderRHI, ShadowNearAndFarDepth, SliceNearAndFarDepth);
			SetShaderValue(RHICmdList, ShaderRHI, bCascadeUseFadePlane, bUseFadePlane ? 1 : 0);
		}

		SetShaderValue(RHICmdList, ShaderRHI, PerObjectShadowFadeStart, ShadowInfo->PerObjectShadowFadeStart);
		SetShaderValue(RHICmdList, ShaderRHI, InvPerObjectShadowFadeLength, ShadowInfo->InvPerObjectShadowFadeLength);
	}

private:
	LAYOUT_FIELD(FShaderParameter, ScreenToShadowMatrix);
	LAYOUT_FIELD(FShaderParameter, SoftTransitionScale);
	LAYOUT_FIELD(FShaderParameter, ShadowBufferSize);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTextureSampler);
	LAYOUT_FIELD(FShaderParameter, ProjectionDepthBias);
	LAYOUT_FIELD(FShaderParameter, FadePlaneOffset);
	LAYOUT_FIELD(FShaderParameter, InvFadePlaneLength);
	LAYOUT_FIELD(FShaderParameter, ShadowTileOffsetAndSizeParam);
	LAYOUT_FIELD(FShaderParameter, LightPositionOrDirection);
	LAYOUT_FIELD(FShaderResourceParameter, HairCategorizationTexture);
	LAYOUT_FIELD(FShaderParameter, PerObjectShadowFadeStart);
	LAYOUT_FIELD(FShaderParameter, InvPerObjectShadowFadeLength);
	LAYOUT_FIELD(FShaderParameter, ShadowNearAndFarDepth);
	LAYOUT_FIELD(FShaderParameter, bCascadeUseFadePlane);
};

/**
 * TShadowProjectionPS
 * A pixel shader for projecting a shadow depth buffer onto the scene.  Used with any light type casting normal shadows.
 */
template<uint32 Quality, bool bUseFadePlane = false, bool bModulatedShadows = false, bool bUseTransmission = false, bool SubPixelShadow = false>
class TShadowProjectionPS : public FShadowProjectionPixelShaderInterface
{
	DECLARE_SHADER_TYPE(TShadowProjectionPS,Global);
public:

	TShadowProjectionPS()
		: FShadowProjectionPixelShaderInterface()
	{ 
	}

	/**
	 * Constructor - binds all shader params and initializes the sample offsets
	 * @param Initializer - init data from shader compiler
	 */
	TShadowProjectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FShadowProjectionPixelShaderInterface(Initializer)
	{
		ProjectionParameters.Bind(Initializer);
		ShadowFadeFraction.Bind(Initializer.ParameterMap,TEXT("ShadowFadeFraction"));
		ShadowSharpen.Bind(Initializer.ParameterMap,TEXT("ShadowSharpen"));
		TransmissionProfilesTexture.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		LightPosition.Bind(Initializer.ParameterMap, TEXT("LightPositionAndInvRadius"));

	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/**
	 * Add any defines required by the shader
	 * @param OutEnvironment - shader environment to modify
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FShadowProjectionPixelShaderInterface::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADOW_QUALITY"), Quality);
		OutEnvironment.SetDefine(TEXT("SUBPIXEL_SHADOW"), (uint32)(SubPixelShadow ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("USE_FADE_PLANE"), (uint32)(bUseFadePlane ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("USE_TRANSMISSION"), (uint32)(bUseTransmission ? 1 : 0));
	}

	/**
	 * Sets the pixel shader's parameters
	 * @param View - current view
	 * @param ShadowInfo - projected shadow info for a single light
	 */
	void SetParameters(
		FRHICommandList& RHICmdList, 
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FShadowProjectionPixelShaderInterface::SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);

		const bool bUseFadePlaneEnable = ShadowInfo->CascadeSettings.FadePlaneLength > 0;

		ProjectionParameters.Set(RHICmdList, this, View, ShadowInfo, HairVisibilityData, bModulatedShadows, bUseFadePlaneEnable);
		const FLightSceneProxy& LightProxy = *(ShadowInfo->GetLightSceneInfo().Proxy);

		SetShaderValue(RHICmdList, ShaderRHI, ShadowFadeFraction, ShadowInfo->FadeAlphas[ViewIndex] );
		SetShaderValue(RHICmdList, ShaderRHI, ShadowSharpen, LightProxy.GetShadowSharpen() * 7.0f + 1.0f );
		SetShaderValue(RHICmdList, ShaderRHI, LightPosition, FVector4(LightProxy.GetPosition(), 1.0f / LightProxy.GetRadius()));

		auto DeferredLightParameter = GetUniformBufferParameter<FDeferredLightUniformStruct>();

		if (DeferredLightParameter.IsBound())
		{
			SetDeferredLightParameters(RHICmdList, ShaderRHI, DeferredLightParameter, &ShadowInfo->GetLightSceneInfo(), View);
		}


		FScene* Scene = nullptr;

		if (View.Family->Scene)
		{
			Scene = View.Family->Scene->GetRenderScene();
		}

		{
			const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT((FRHICommandListImmediate&)RHICmdList);

			if (!PooledRT)
			{
				// no subsurface profile was used yet
				PooledRT = GSystemTextures.BlackDummy;
			}

			const FSceneRenderTargetItem& Item = PooledRT->GetRenderTargetItem();

			SetTextureParameter(RHICmdList, ShaderRHI, TransmissionProfilesTexture, Item.ShaderResourceTexture);
		}
	}

protected:
	LAYOUT_FIELD(FShadowProjectionShaderParameters, ProjectionParameters);
	LAYOUT_FIELD(FShaderParameter, ShadowFadeFraction);
	LAYOUT_FIELD(FShaderParameter, ShadowSharpen);
	LAYOUT_FIELD(FShaderParameter, LightPosition);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesTexture);
};

/** Pixel shader to project modulated shadows onto the scene. */
template<uint32 Quality>
class TModulatedShadowProjection : public TShadowProjectionPS<Quality, false, true>
{
	DECLARE_SHADER_TYPE(TModulatedShadowProjection, Global);
public:

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TShadowProjectionPS<Quality, false, true>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MODULATED_SHADOWS"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform);
	}

	TModulatedShadowProjection() {}

	TModulatedShadowProjection(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		TShadowProjectionPS<Quality, false, true>(Initializer)
	{
		ModulatedShadowColorParameter.Bind(Initializer.ParameterMap, TEXT("ModulatedShadowColor"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{
		TShadowProjectionPS<Quality, false, true>::SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetShaderValue(RHICmdList, ShaderRHI, ModulatedShadowColorParameter, ShadowInfo->GetLightSceneInfo().Proxy->GetModulatedShadowColor());
	}

protected:
	LAYOUT_FIELD(FShaderParameter, ModulatedShadowColorParameter);
};

/** Translucency shadow projection uniform buffer containing data needed for Fourier opacity maps. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FTranslucentSelfShadowUniformParameters, )
	SHADER_PARAMETER(FMatrix, WorldToShadowMatrix)
	SHADER_PARAMETER(FVector4, ShadowUVMinMax)
	SHADER_PARAMETER(FVector4, DirectionalLightDirection)
	SHADER_PARAMETER(FVector4, DirectionalLightColor)
	SHADER_PARAMETER_TEXTURE(Texture2D, Transmission0)
	SHADER_PARAMETER_TEXTURE(Texture2D, Transmission1)
	SHADER_PARAMETER_SAMPLER(SamplerState, Transmission0Sampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, Transmission1Sampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern void SetupTranslucentSelfShadowUniformParameters(const FProjectedShadowInfo* ShadowInfo, FTranslucentSelfShadowUniformParameters& OutParameters);

/**
* Default translucent self shadow data.
*/
class FEmptyTranslucentSelfShadowUniformBuffer : public TUniformBuffer< FTranslucentSelfShadowUniformParameters >
{
	typedef TUniformBuffer< FTranslucentSelfShadowUniformParameters > Super;
public:
	virtual void InitDynamicRHI() override;
};

/** Global uniform buffer containing the default precomputed lighting data. */
extern TGlobalResource< FEmptyTranslucentSelfShadowUniformBuffer > GEmptyTranslucentSelfShadowUniformBuffer;

/** Pixel shader to project both opaque and translucent shadows onto opaque surfaces. */
template<uint32 Quality> 
class TShadowProjectionFromTranslucencyPS : public TShadowProjectionPS<Quality>
{
	DECLARE_SHADER_TYPE(TShadowProjectionFromTranslucencyPS,Global);
public:

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TShadowProjectionPS<Quality>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("APPLY_TRANSLUCENCY_SHADOWS"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && TShadowProjectionPS<Quality>::ShouldCompilePermutation(Parameters);
	}

	TShadowProjectionFromTranslucencyPS() {}

	TShadowProjectionFromTranslucencyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		TShadowProjectionPS<Quality>(Initializer)
	{
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{
		TShadowProjectionPS<Quality>::SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);

		FTranslucentSelfShadowUniformParameters TranslucentSelfShadowUniformParameters;
		SetupTranslucentSelfShadowUniformParameters(ShadowInfo, TranslucentSelfShadowUniformParameters);
		SetUniformBufferParameterImmediate(RHICmdList, RHICmdList.GetBoundPixelShader(), this->template GetUniformBufferParameter<FTranslucentSelfShadowUniformParameters>(), TranslucentSelfShadowUniformParameters);
	}
};


BEGIN_SHADER_PARAMETER_STRUCT(FOnePassPointShadowProjection, )
	SHADER_PARAMETER_TEXTURE(TextureCube, ShadowDepthCubeTexture)
	SHADER_PARAMETER_TEXTURE(TextureCube, ShadowDepthCubeTexture2)
	SHADER_PARAMETER_SAMPLER(SamplerComparisonState, ShadowDepthCubeTextureSampler)
	SHADER_PARAMETER_ARRAY(FMatrix, ShadowViewProjectionMatrices, [6])
	SHADER_PARAMETER(float, InvShadowmapResolution)
END_SHADER_PARAMETER_STRUCT()

extern void GetOnePassPointShadowProjectionParameters(const FProjectedShadowInfo* ShadowInfo, FOnePassPointShadowProjection& OutParameters);


/** One pass point light shadow projection parameters used by multiple shaders. */
class FOnePassPointShadowProjectionShaderParameters
{
	DECLARE_TYPE_LAYOUT(FOnePassPointShadowProjectionShaderParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ShadowDepthTexture.Bind(ParameterMap,TEXT("ShadowDepthCubeTexture"));
		ShadowDepthTexture2.Bind(ParameterMap, TEXT("ShadowDepthCubeTexture2"));
		ShadowDepthCubeComparisonSampler.Bind(ParameterMap,TEXT("ShadowDepthCubeTextureSampler"));
		ShadowViewProjectionMatrices.Bind(ParameterMap, TEXT("ShadowViewProjectionMatrices"));
		InvShadowmapResolution.Bind(ParameterMap, TEXT("InvShadowmapResolution"));
		LightPositionOrDirection.Bind(ParameterMap, TEXT("LightPositionOrDirection"));
	}

	template<typename ShaderRHIParamRef>
	void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FProjectedShadowInfo* ShadowInfo) const
	{
		FRHITexture* ShadowDepthTextureValue = ShadowInfo
			? ShadowInfo->RenderTargets.DepthTarget->GetRenderTargetItem().ShaderResourceTexture->GetTextureCube()
			: GBlackTextureDepthCube->TextureRHI.GetReference();
		if (!ShadowDepthTextureValue)
		{
			ShadowDepthTextureValue = GBlackTextureDepthCube->TextureRHI.GetReference();
		}

		SetTextureParameter(
			RHICmdList, 
			ShaderRHI, 
			ShadowDepthTexture, 
			ShadowDepthTextureValue
			);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			ShadowDepthTexture2,
			ShadowDepthTextureValue
		);

		if (LightPositionOrDirection.IsBound())
		{
			const FVector LightPosition = ShadowInfo ? FVector(ShadowInfo->GetLightSceneInfo().Proxy->GetPosition()) : FVector::ZeroVector;
			SetShaderValue(RHICmdList, ShaderRHI, LightPositionOrDirection, FVector4(LightPosition, 1));
		}
		
		if (ShadowDepthCubeComparisonSampler.IsBound())
		{
			RHICmdList.SetShaderSampler(
				ShaderRHI, 
				ShadowDepthCubeComparisonSampler.GetBaseIndex(), 
				// Use a comparison sampler to do hardware PCF
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI()
				);
		}

		if (ShadowInfo)
		{
			SetShaderValueArray<ShaderRHIParamRef, FMatrix>(
				RHICmdList, 
				ShaderRHI,
				ShadowViewProjectionMatrices,
				ShadowInfo->OnePassShadowViewProjectionMatrices.GetData(),
				ShadowInfo->OnePassShadowViewProjectionMatrices.Num()
				);

			SetShaderValue(RHICmdList, ShaderRHI,InvShadowmapResolution,1.0f / ShadowInfo->ResolutionX);
		}
		else
		{
			TArray<FMatrix, SceneRenderingAllocator> ZeroMatrices;
			ZeroMatrices.AddZeroed(FMath::DivideAndRoundUp<int32>(ShadowViewProjectionMatrices.GetNumBytes(), sizeof(FMatrix)));

			SetShaderValueArray<ShaderRHIParamRef, FMatrix>(
				RHICmdList, 
				ShaderRHI,
				ShadowViewProjectionMatrices,
				ZeroMatrices.GetData(),
				ZeroMatrices.Num()
				);

			SetShaderValue(RHICmdList, ShaderRHI,InvShadowmapResolution,0);
		}
	}

	/** Serializer. */ 
	/*friend FArchive& operator<<(FArchive& Ar,FOnePassPointShadowProjectionShaderParameters& P)
	{
		Ar << P.ShadowDepthTexture;
		Ar << P.ShadowDepthTexture2;
		Ar << P.ShadowDepthCubeComparisonSampler;
		Ar << P.ShadowViewProjectionMatrices;
		Ar << P.InvShadowmapResolution;
		Ar << P.LightPositionOrDirection;
		return Ar;
	}*/

private:
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture2);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthCubeComparisonSampler);
	LAYOUT_FIELD(FShaderParameter, ShadowViewProjectionMatrices);
	LAYOUT_FIELD(FShaderParameter, InvShadowmapResolution);
	LAYOUT_FIELD(FShaderParameter, LightPositionOrDirection);
};

/**
 * Pixel shader used to project one pass point light shadows.
 */
// Quality = 0 / 1
template <uint32 Quality, bool bUseTransmission, bool bUseSubPixel>
class TOnePassPointShadowProjectionPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TOnePassPointShadowProjectionPS,Global);
public:

	TOnePassPointShadowProjectionPS() {}

	TOnePassPointShadowProjectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		OnePassShadowParameters.Bind(Initializer.ParameterMap);
		ShadowDepthTextureSampler.Bind(Initializer.ParameterMap,TEXT("ShadowDepthTextureSampler"));
		LightPosition.Bind(Initializer.ParameterMap,TEXT("LightPositionAndInvRadius"));
		ShadowFadeFraction.Bind(Initializer.ParameterMap,TEXT("ShadowFadeFraction"));
		ShadowSharpen.Bind(Initializer.ParameterMap,TEXT("ShadowSharpen"));
		PointLightDepthBias.Bind(Initializer.ParameterMap,TEXT("PointLightDepthBias"));
		PointLightProjParameters.Bind(Initializer.ParameterMap, TEXT("PointLightProjParameters"));
		TransmissionProfilesTexture.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		HairCategorizationTexture.Bind(Initializer.ParameterMap, TEXT("HairCategorizationTexture"));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADOW_QUALITY"), Quality);
		OutEnvironment.SetDefine(TEXT("USE_TRANSMISSION"), (uint32)(bUseTransmission ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("SUBPIXEL_SHADOW"), (uint32)(bUseSubPixel ? 1 : 0));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		OnePassShadowParameters.Set(RHICmdList, ShaderRHI, ShadowInfo);

		const FLightSceneProxy& LightProxy = *(ShadowInfo->GetLightSceneInfo().Proxy);

		SetShaderValue(RHICmdList, ShaderRHI, LightPosition, FVector4(LightProxy.GetPosition(), 1.0f / LightProxy.GetRadius()));

		SetShaderValue(RHICmdList, ShaderRHI, ShadowFadeFraction, ShadowInfo->FadeAlphas[ViewIndex]);
		SetShaderValue(RHICmdList, ShaderRHI, ShadowSharpen, LightProxy.GetShadowSharpen() * 7.0f + 1.0f);
		//Near is always 1? // TODO: validate
		float Near = 1;
		float Far = LightProxy.GetRadius();
		FVector2D param = FVector2D(Far / (Far - Near), -Near * Far / (Far - Near));
		FVector2D projParam = FVector2D(1.0f / param.Y, param.X / param.Y);
		SetShaderValue(RHICmdList, ShaderRHI, PointLightDepthBias, FVector(ShadowInfo->GetShaderDepthBias(), ShadowInfo->GetShaderSlopeDepthBias(), ShadowInfo->GetShaderMaxSlopeDepthBias()));
		SetShaderValue(RHICmdList, ShaderRHI, PointLightProjParameters, FVector2D(projParam.X, projParam.Y));

		if (bUseSubPixel && HairVisibilityData && HairVisibilityData->CategorizationTexture)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, HairCategorizationTexture, HairVisibilityData->CategorizationTexture->GetPooledRenderTarget()->GetRenderTargetItem().ShaderResourceTexture);
		}

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		{
			const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT((FRHICommandListImmediate&)RHICmdList);

			if (!PooledRT)
			{
				// no subsurface profile was used yet
				PooledRT = GSystemTextures.BlackDummy;
			}

			const FSceneRenderTargetItem& Item = PooledRT->GetRenderTargetItem();

			SetTextureParameter(RHICmdList, ShaderRHI, TransmissionProfilesTexture, Item.ShaderResourceTexture);
		}

		FScene* Scene = nullptr;

		if (View.Family->Scene)
		{
			Scene = View.Family->Scene->GetRenderScene();
		}

		SetSamplerParameter(RHICmdList, ShaderRHI, ShadowDepthTextureSampler, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

		auto DeferredLightParameter = GetUniformBufferParameter<FDeferredLightUniformStruct>();

		if (DeferredLightParameter.IsBound())
		{
			SetDeferredLightParameters(RHICmdList, ShaderRHI, DeferredLightParameter, &ShadowInfo->GetLightSceneInfo(), View);
		}
	}

private:
	LAYOUT_FIELD(FOnePassPointShadowProjectionShaderParameters, OnePassShadowParameters);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTextureSampler);
	LAYOUT_FIELD(FShaderParameter, LightPosition);
	LAYOUT_FIELD(FShaderParameter, ShadowFadeFraction);
	LAYOUT_FIELD(FShaderParameter, ShadowSharpen);
	LAYOUT_FIELD(FShaderParameter, PointLightDepthBias);
	LAYOUT_FIELD(FShaderParameter, PointLightProjParameters);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesTexture);
	LAYOUT_FIELD(FShaderResourceParameter, HairCategorizationTexture);
};

/** A transform the remaps depth and potentially projects onto some plane. */
struct FShadowProjectionMatrix: FMatrix
{
	FShadowProjectionMatrix(float MinZ,float MaxZ,const FVector4& WAxis):
		FMatrix(
		FPlane(1,	0,	0,													WAxis.X),
		FPlane(0,	1,	0,													WAxis.Y),
		FPlane(0,	0,	(WAxis.Z * MaxZ + WAxis.W) / (MaxZ - MinZ),			WAxis.Z),
		FPlane(0,	0,	-MinZ * (WAxis.Z * MaxZ + WAxis.W) / (MaxZ - MinZ),	WAxis.W)
		)
	{}
};


/** Pixel shader to project directional PCSS onto the scene. */
template<uint32 Quality, bool bUseFadePlane>
class TDirectionalPercentageCloserShadowProjectionPS : public TShadowProjectionPS<Quality, bUseFadePlane>
{
	DECLARE_SHADER_TYPE(TDirectionalPercentageCloserShadowProjectionPS, Global);
public:

	TDirectionalPercentageCloserShadowProjectionPS() {}
	TDirectionalPercentageCloserShadowProjectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		TShadowProjectionPS<Quality, bUseFadePlane>(Initializer)
	{
		PCSSParameters.Bind(Initializer.ParameterMap, TEXT("PCSSParameters"));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TShadowProjectionPS<Quality, bUseFadePlane>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_PCSS"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return TShadowProjectionPS<Quality, bUseFadePlane>::ShouldCompilePermutation(Parameters)
			&& (Parameters.Platform == SP_PCD3D_SM5 || IsVulkanSM5Platform(Parameters.Platform) || Parameters.Platform == SP_METAL_SM5 || Parameters.Platform == SP_METAL_SM5_NOTESS);
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{
		TShadowProjectionPS<Quality, bUseFadePlane>::SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);

		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		// GetLightSourceAngle returns the full angle.
		float TanLightSourceAngle = FMath::Tan(0.5 * FMath::DegreesToRadians(ShadowInfo->GetLightSceneInfo().Proxy->GetLightSourceAngle()));

		static IConsoleVariable* CVarMaxSoftShadowKernelSize = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.MaxSoftKernelSize"));
		check(CVarMaxSoftShadowKernelSize);
		int32 MaxKernelSize = CVarMaxSoftShadowKernelSize->GetInt();

		float SW = 2.0 * ShadowInfo->ShadowBounds.W;
		float SZ = ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ;

		FVector4 PCSSParameterValues = FVector4(TanLightSourceAngle * SZ / SW, MaxKernelSize / float(ShadowInfo->ResolutionX), 0, 0);
		SetShaderValue(RHICmdList, ShaderRHI, PCSSParameters, PCSSParameterValues);
	}

protected:
	LAYOUT_FIELD(FShaderParameter, PCSSParameters);
};


/** Pixel shader to project PCSS spot light onto the scene. */
template<uint32 Quality, bool bUseFadePlane>
class TSpotPercentageCloserShadowProjectionPS : public TShadowProjectionPS<Quality, bUseFadePlane>
{
	DECLARE_SHADER_TYPE(TSpotPercentageCloserShadowProjectionPS, Global);
public:

	TSpotPercentageCloserShadowProjectionPS() {}
	TSpotPercentageCloserShadowProjectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		TShadowProjectionPS<Quality, bUseFadePlane>(Initializer)
	{
		PCSSParameters.Bind(Initializer.ParameterMap, TEXT("PCSSParameters"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& (Parameters.Platform == SP_PCD3D_SM5 || IsVulkanSM5Platform(Parameters.Platform) || Parameters.Platform == SP_METAL_SM5 || Parameters.Platform == SP_METAL_SM5_NOTESS);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TShadowProjectionPS<Quality, bUseFadePlane>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_PCSS"), 1);
		OutEnvironment.SetDefine(TEXT("SPOT_LIGHT_PCSS"), 1);
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		int32 ViewIndex,
		const FSceneView& View,
		const FHairStrandsVisibilityData* HairVisibilityData,
		const FProjectedShadowInfo* ShadowInfo)
	{
		check(ShadowInfo->GetLightSceneInfo().Proxy->GetLightType() == LightType_Spot);

		TShadowProjectionPS<Quality, bUseFadePlane>::SetParameters(RHICmdList, ViewIndex, View, HairVisibilityData, ShadowInfo);

		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		static IConsoleVariable* CVarMaxSoftShadowKernelSize = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.MaxSoftKernelSize"));
		check(CVarMaxSoftShadowKernelSize);
		int32 MaxKernelSize = CVarMaxSoftShadowKernelSize->GetInt();

		FVector4 PCSSParameterValues = FVector4(0, MaxKernelSize / float(ShadowInfo->ResolutionX), 0, 0);
		SetShaderValue(RHICmdList, ShaderRHI, PCSSParameters, PCSSParameterValues);
	}

protected:
	LAYOUT_FIELD(FShaderParameter, PCSSParameters);
};


// Sort by descending resolution
struct FCompareFProjectedShadowInfoByResolution
{
	FORCEINLINE bool operator() (const FProjectedShadowInfo& A, const FProjectedShadowInfo& B) const
	{
		return (B.ResolutionX * B.ResolutionY < A.ResolutionX * A.ResolutionY);
	}
};

// Sort by shadow type (CSMs first, then other types).
// Then sort CSMs by descending split index, and other shadows by resolution.
// Used to render shadow cascades in far to near order, whilst preserving the
// descending resolution sort behavior for other shadow types.
// Note: the ordering must match the requirements of blend modes set in SetBlendStateForProjection (blend modes that overwrite must come first)
struct FCompareFProjectedShadowInfoBySplitIndex
{
	FORCEINLINE bool operator()( const FProjectedShadowInfo& A, const FProjectedShadowInfo& B ) const
	{
		if (A.IsWholeSceneDirectionalShadow())
		{
			if (B.IsWholeSceneDirectionalShadow())
			{
				if (A.bRayTracedDistanceField != B.bRayTracedDistanceField)
				{
					// RTDF shadows need to be rendered after all CSM, because they overlap in depth range with Far Cascades, which will use an overwrite blend mode for the fade plane.
					if (!A.bRayTracedDistanceField && B.bRayTracedDistanceField)
					{
						return true;
					}

					if (A.bRayTracedDistanceField && !B.bRayTracedDistanceField)
					{
						return false;
					}
				}

				// Both A and B are CSMs
				// Compare Split Indexes, to order them far to near.
				return (B.CascadeSettings.ShadowSplitIndex < A.CascadeSettings.ShadowSplitIndex);
			}

			// A is a CSM, B is per-object shadow etc.
			// B should be rendered after A.
			return true;
		}
		else
		{
			if (B.IsWholeSceneDirectionalShadow())
			{
				// B should be rendered before A.
				return false;
			}
			
			// Neither shadow is a CSM
			// Sort by descending resolution.
			return FCompareFProjectedShadowInfoByResolution()(A, B);
		}
	}
};

/**
* Shaders used to ouput screen space modulated shadow.
*/
class TScreenSpaceModulatedShadowVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TScreenSpaceModulatedShadowVS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform);
	}

	TScreenSpaceModulatedShadowVS() {}
	TScreenSpaceModulatedShadowVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
	}
};

class TScreenSpaceModulatedShadowPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TScreenSpaceModulatedShadowPS, Global);
public:

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsMobilePlatform(Parameters.Platform);
	}

	TScreenSpaceModulatedShadowPS() {}

	TScreenSpaceModulatedShadowPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		ScreenShadowMaskTexture.Bind(Initializer.ParameterMap, TEXT("ScreenShadowMaskTexture"));
		ScreenShadowMaskTextureSampler.Bind(Initializer.ParameterMap, TEXT("ScreenShadowMaskTextureSampler"));
		ModulatedShadowColorParameter.Bind(Initializer.ParameterMap, TEXT("ModulatedShadowColor"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, FRHITexture* ScreenShadowMask, const FLinearColor& ModulatedShadowColor)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);

		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		FRHISamplerState* SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		SetTextureParameter(RHICmdList, ShaderRHI, ScreenShadowMaskTexture, ScreenShadowMask);
		SetSamplerParameter(RHICmdList, ShaderRHI, ScreenShadowMaskTextureSampler, SamplerState);

		SetShaderValue(RHICmdList, ShaderRHI, ModulatedShadowColorParameter, ModulatedShadowColor);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ScreenShadowMaskTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ScreenShadowMaskTextureSampler);
	LAYOUT_FIELD(FShaderParameter, ModulatedShadowColorParameter);
};
