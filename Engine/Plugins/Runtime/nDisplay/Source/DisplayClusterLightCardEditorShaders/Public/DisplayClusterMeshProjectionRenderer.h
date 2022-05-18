// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"

class AActor;
class UPrimitiveComponent;
class FSceneView;
class FViewInfo;
class FPrimitiveDrawInterface;
class FSimpleElementCollector;
struct FSceneViewInitOptions;
struct FEngineShowFlags;

/** Indicates which kind of projection is used by the renderer */
enum DISPLAYCLUSTERLIGHTCARDEDITORSHADERS_API EDisplayClusterMeshProjectionType
{
	/** Default linear projection */
	Perspective,

	/** Non-linear spherical projection based on the azimuthal equidistant map projection */
	Azimuthal
};

/** A filter that allows specific primitive components to be filtered from a render pass */
class DISPLAYCLUSTERLIGHTCARDEDITORSHADERS_API FDisplayClusterMeshProjectionPrimitiveFilter
{
public:
	/** A delegate that returns true if the primitive component should be included in the render pass */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FPrimitiveFilter, const UPrimitiveComponent*);
	FPrimitiveFilter PrimitiveFilterDelegate;

	/** Gets whether a primitive component should be filtered out of the render pass or not */
	bool IsPrimitiveComponentFiltered(const UPrimitiveComponent* InPrimitiveComponent) const;
};

/** A transform that can be passed around to project and unprojection positions for a specific projection type */
class DISPLAYCLUSTERLIGHTCARDEDITORSHADERS_API FDisplayClusterMeshProjectionTransform
{
public:
	FDisplayClusterMeshProjectionTransform()
		: FDisplayClusterMeshProjectionTransform(EDisplayClusterMeshProjectionType::Perspective, FMatrix::Identity)
	{ }

	FDisplayClusterMeshProjectionTransform(EDisplayClusterMeshProjectionType InProjection, const FMatrix& InViewMatrix)
		: Projection(InProjection)
		, ViewMatrix(InViewMatrix)
		, InvViewMatrix(InViewMatrix.Inverse())
	{ }

	FVector ProjectPosition(const FVector& WorldPosition) const;
	FVector UnprojectPosition(const FVector& ProjectedPosition) const;

private:
	EDisplayClusterMeshProjectionType Projection;
	FMatrix ViewMatrix;
	FMatrix InvViewMatrix;
};

/** A renderer that projects meshes to screen space using non-linear projection methods */
class DISPLAYCLUSTERLIGHTCARDEDITORSHADERS_API FDisplayClusterMeshProjectionRenderer
{
public:
	/** Projects a position in view coordinates into the projected view space of the specified projection type */
	static FVector ProjectViewPosition(const FVector& ViewPosition, EDisplayClusterMeshProjectionType  ProjectionType);

	/** Projects a position in the projected view space of the specified projection type to ordinary view coordinates */
	static FVector UnprojectViewPosition(const FVector& ProjectedViewPosition, EDisplayClusterMeshProjectionType  ProjectionType);

	/** Adds an actor's primitive components to the list of primitives to render */
	void AddActor(AActor* Actor);

	/** Adds an actor's primitive components to the list of primitives to render, filtering which primitive components get rendered using the specified callback */
	void AddActor(AActor* Actor, const TFunctionRef<bool(const UPrimitiveComponent*)>& PrimitiveFilter);

	/** Removes an actor's primitive components from the list of primitives to render */
	void RemoveActor(AActor* Actor);

	/** Clears the list of primitives to render */
	void ClearScene();

	/** Renders its list of primitive components to the specified canvas using the desired projection type. Can be called from the game thread */
	void Render(FCanvas* Canvas, FSceneInterface* Scene, const FSceneViewInitOptions& ViewInitOptions, const FEngineShowFlags& EngineShowFlags, EDisplayClusterMeshProjectionType  ProjectionType);

	/** Renders the normals and depth of the scene's primitives and performs blurring to create a continuous normal map */
	void RenderNormals(FCanvas* Canvas,
		FSceneInterface* Scene,
		const FSceneViewInitOptions& ViewInitOptions,
		const FEngineShowFlags& EngineShowFlags,
		EDisplayClusterMeshProjectionType ProjectionType, 
		FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter = nullptr);

private:
	/** Adds a pass to perform the base render for the mesh projection. */
	void AddBaseRenderPass(FRDGBuilder& GraphBuilder, 
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FDepthStencilBinding& OutputDepthStencilBinding);

	/** Adds a pass to perform the translucency render for the mesh projection. */
	void AddTranslucencyRenderPass(FRDGBuilder& GraphBuilder, 
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FDepthStencilBinding& OutputDepthStencilBinding);

	/** Adds a pass to perform the hit proxy render for the mesh projection */
	void AddHitProxyRenderPass(FRDGBuilder& GraphBuilder, 
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FDepthStencilBinding& OutputDepthStencilBinding);

	/** Adds a pass to perform a render of the primitives' normals for the mesh projection */
	void AddNormalsRenderPass(FRDGBuilder& GraphBuilder,
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FDepthStencilBinding& OutputDepthStencilBinding);

	/** Adds a pass to filter the output normal map for the mesh projection, dilating and blurring it
	 * to obtain a continuous normal map from primitives into empty space */
	void AddNormalsFilterPass(FRDGBuilder& GraphBuilder,
		const FViewInfo* View,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FRDGTexture* SceneColor,
		FRDGTexture* SceneDepth);

#if WITH_EDITOR
	/** Adds a pass to perform the depth render for any selected primitives for the mesh projection */
	void AddSelectionDepthRenderPass(FRDGBuilder& GraphBuilder, 
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FDepthStencilBinding& OutputDepthStencilBinding);

	/** Adds a pass to perform the post process selection outline for the mesh projection */
	void AddSelectionOutlineScreenPass(FRDGBuilder& GraphBuilder,
		const FViewInfo* View,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FRDGTexture* SceneColor,
		FRDGTexture* SceneDepth,
		FRDGTexture* SelectionDepth);
#endif

	/** Adds a pass that renders any elements added to the PDI through the renderer's RenderSimpleElements callback */
	void AddSimpleElementPass(FRDGBuilder& GraphBuilder,
		const FViewInfo* View,
		FRenderTargetBinding& OutputRenderTargetBinding,
		FSimpleElementCollector& ElementCollector);

	/** Renders the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderPrimitives_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList, bool bTranslucencyPass);

	/** Renders the hit proxies of the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderHitProxies_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList);

	/** Renders the normals of the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderNormals_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList, FDisplayClusterMeshProjectionPrimitiveFilter* PrimitiveFilter);

#if WITH_EDITOR
	/** Renders the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderSelection_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList);
#endif

	/** Callback used to determine if a primitive component should be rendered with a selection outline */
	bool IsPrimitiveComponentSelected(const UPrimitiveComponent* InPrimitiveComponent);

public:
	/** Delegate to determine if an actor should be rendered as selected */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FSelection, const AActor*);
	FSelection ActorSelectedDelegate;

	/** Delegate raised during a render pass allowing simple elements ro be rendered to the viewport after meshes are projected to it */
	DECLARE_DELEGATE_TwoParams(FSimpleElementPass, const FSceneView*, FPrimitiveDrawInterface*);
	FSimpleElementPass RenderSimpleElementsDelegate;

private:
	/** The list of primitive components to render */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> PrimitiveComponents;
};