// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"

class AActor;
class UPrimitiveComponent;
class FSceneView;
class UPrimitiveComponent;

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
	void Render(FCanvas* Canvas, FSceneInterface* Scene, const FSceneViewInitOptions& ViewInitOptions, EDisplayClusterMeshProjectionType  ProjectionType);

private:
	/** Renders the list of primitive components using the appropriate mesh pass processor given by the template parameter */
	template<EDisplayClusterMeshProjectionType ProjectionType>
	void RenderPrimitives_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList);

private:
	/** The list of primitive components to render */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> PrimitiveComponents;
};