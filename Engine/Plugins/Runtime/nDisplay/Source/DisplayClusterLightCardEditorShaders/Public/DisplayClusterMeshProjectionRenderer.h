// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"

class AActor;
class UPrimitiveComponent;
class FSceneView;
class FViewInfo;
struct FEngineShowFlags;

/** Indicates which kind of projection is used by the renderer */
enum DISPLAYCLUSTERLIGHTCARDEDITORSHADERS_API EDisplayClusterMeshProjectionType
{
	/** Default linear projection */
	Perspective,

	/** Non-linear spherical projection based on the azimuthal equidistant map projection */
	Azimuthal
};

/** A renderer that projects meshes to screen space using non-linear projection methods */
class DISPLAYCLUSTERLIGHTCARDEDITORSHADERS_API FDisplayClusterMeshProjectionRenderer
{
public:
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

private:
	/** Adds a pass to perform the base render for the mesh projection. */
	void AddBaseRenderPass(FRDGBuilder& GraphBuilder, 
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

	/** Renders the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderPrimitives_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList);

	/** Renders the hit proxies of the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderHitProxies_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList);

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

private:
	/** The list of primitive components to render */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> PrimitiveComponents;
};