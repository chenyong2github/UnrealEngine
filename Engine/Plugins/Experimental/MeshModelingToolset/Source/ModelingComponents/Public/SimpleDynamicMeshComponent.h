// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseDynamicMeshComponent.h"
#include "MeshConversionOptions.h"
#include "Drawing/MeshRenderDecomposition.h"
#include "MeshTangents.h"
#include "TransformTypes.h"
#include "Async/Future.h"
#include "UDynamicMesh.h"

#include "SimpleDynamicMeshComponent.generated.h"

// predecl
struct FMeshDescription;

/** internal FPrimitiveSceneProxy defined in SimpleDynamicMeshSceneProxy.h */
class FSimpleDynamicMeshSceneProxy;


/**
 * Interface for a render mesh processor. Use this to process the Mesh stored in USimpleDynamicMeshComponent before
 * sending it off for rendering.
 * NOTE: This is called whenever the Mesh is updated and before rendering, so performance matters.
 */
class MODELINGCOMPONENTS_API IRenderMeshPostProcessor
{
public:
	virtual ~IRenderMeshPostProcessor() = default;

	virtual void ProcessMesh(const FDynamicMesh3& Mesh, FDynamicMesh3& OutRenderMesh) = 0;
};


/** Render data update hint */
UENUM()
enum class EDynamicMeshComponentRenderUpdateMode
{
	/** Do not update render data */
	NoUpdate = 0,
	/** Invalidate overlay of internal component, rebuilding all render data */
	FullUpdate = 1,
	/** Attempt to do partial update of render data if possible */
	FastUpdate = 2
};


/** 
 * USimpleDynamicMeshComponent is a mesh component similar to UProceduralMeshComponent,
 * except it bases the renderable geometry off an internal UDynamicMesh instance (which
 * encapsulates a FDynamicMesh3). 
 * 
 * There is extensive support for partial updates to render buffers, customizing colors,
 * internally decomposing the mesh into separate chunks for more efficient render updates,
 * and support for attaching a 'Postprocessor' to generate a render mesh on-the-fly
 * See comment sections below for details.
 * 
 */
UCLASS(hidecategories = (LOD, Physics, Collision), meta = (BlueprintSpawnableComponent), ClassGroup = Rendering)
class MODELINGCOMPONENTS_API USimpleDynamicMeshComponent : public UBaseDynamicMeshComponent
{
	GENERATED_UCLASS_BODY()



	//===============================================================================================================
	// Mesh Access. Usage via GetDynamicMesh() is preferred, the GetMesh() pass-throughs exist largely to support
	// existing code from before UDynamicMesh was added.
public:
	/**
	 * @return pointer to internal mesh
	 * @warning avoid usage of this function, access via GetDynamicMesh() instead
	 */
	virtual FDynamicMesh3* GetMesh() override { return MeshObject->GetMeshPtr(); }

	/**
	 * @return pointer to internal mesh
	 * @warning avoid usage of this function, access via GetDynamicMesh() instead
	 */
	virtual const FDynamicMesh3* GetMesh() const override { return MeshObject->GetMeshPtr(); }

	/**
	 * @return the child UDynamicMesh
	 */
	//UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	virtual UDynamicMesh* GetDynamicMesh() override { return MeshObject; }

	/**
	 * Set the child UDynamicMesh. This can be used to 'share' a UDynamicMesh between Component instances.
	 * @warning Currently this is somewhat risky, it is on the caller/clients to make sure that the actual mesh is not being simultaneously modified on multiple threads
	 */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Mesh Component")
	void SetDynamicMesh(UDynamicMesh* NewMesh);

protected:
	/**
	 * Internal FDynamicMesh is stored inside a UDynamicMesh container, which allows it to be
	 * used from BP, shared with other UObjects, and so on
	 */
	UPROPERTY(Instanced)
	TObjectPtr<UDynamicMesh> MeshObject;




	//===============================================================================================================
	// RenderBuffer Update API. These functions can be used by external code (and internally in some places)
	// to tell the Component that the Mesh data has been modified in some way, and that the RenderBuffers in the RenderProxy
	// need to be updated (or rebuilt entirely). On large meshes a full rebuild is expensive, so there are quite a few
	// variants that can be used to minimize the amount of data updated in different situations.
	//
public:
	/**
	 * Call this if you update the mesh via GetMesh(). This will destroy the existing RenderProxy and create a new one.
	 * @todo should provide a function that calls a lambda to modify the mesh, and only return const mesh pointer
	 */
	virtual void NotifyMeshUpdated() override;

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex colors (or triangle color function).
	 * This function will update the existing RenderProxy buffers if possible
	 */
	void FastNotifyColorsUpdated();

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex positions (and possibly some attributes).
	 * This function will update the existing RenderProxy buffers if possible
	 */
	void FastNotifyPositionsUpdated(bool bNormals = false, bool bColors = false, bool bUVs = false);

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex attributes (but not positions).
	 * This function will update the existing RenderProxy buffers if possible, rather than create new ones.
	 */
	void FastNotifyVertexAttributesUpdated(bool bNormals, bool bColors, bool bUVs);

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex positions/attributes
	 * This function will update the existing RenderProxy buffers if possible, rather than create new ones.
	 */
	void FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags UpdatedAttributes);

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated the vertex uvs.
	 * This function will update the existing RenderProxy buffers if possible
	 */
	void FastNotifyUVsUpdated();

	/**
	 * Call this instead of NotifyMeshUpdated() if you have only updated secondary triangle sorting.
	 * This function will update the existing buffers if possible, without rebuilding entire RenderProxy.
	 */
	void FastNotifySecondaryTrianglesChanged();

	/**
	 * This function updates vertex positions/attributes of existing SceneProxy render buffers if possible, for the given triangles.
	 * If a FMeshRenderDecomposition has not been explicitly set, call is forwarded to FastNotifyVertexAttributesUpdated()
	 */
	void FastNotifyTriangleVerticesUpdated(const TArray<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes);

	/**
	 * This function updates vertex positions/attributes of existing SceneProxy render buffers if possible, for the given triangles.
	 * If a FMeshRenderDecomposition has not been explicitly set, call is forwarded to FastNotifyVertexAttributesUpdated()
	 */
	void FastNotifyTriangleVerticesUpdated(const TSet<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes);

	/**
	 * If a Decomposition is set on this Component, and everything is currently valid (proxy/etc), precompute the set of
	 * buffers that will be modified, as well as the bounds of the modified region. These are both computed in parallel.
	 * Use FastNotifyTriangleVerticesUpdated_ApplyPrecompute() with the returned future to apply this precomputation.
	 * @return a future that will (eventually) return true if the precompute is OK, and (immediately) false if it is not
	 */
	TFuture<bool> FastNotifyTriangleVerticesUpdated_TryPrecompute(const TArray<int32>& Triangles, TArray<int32>& UpdateSetsOut, UE::Geometry::FAxisAlignedBox3d& BoundsOut);

	/**
	 * This function updates vertex positions/attributes of existing SceneProxy render buffers if possible, for the given triangles.
	 * The assumption is that FastNotifyTriangleVerticesUpdated_TryPrecompute() was used to get the Precompute future, this function
	 * will Wait() until it is done and then use the UpdateSets and UpdateSetBounds that were computed (must be the same variables
	 * passed to FastNotifyTriangleVerticesUpdated_TryPrecompute). 
	 * If the Precompute future returns false, then we forward the call to FastNotifyTriangleVerticesUpdated(), which will do more work.
	 */
	void FastNotifyTriangleVerticesUpdated_ApplyPrecompute(const TArray<int32>& Triangles, EMeshRenderAttributeFlags UpdatedAttributes,
		TFuture<bool>& Precompute, const TArray<int32>& UpdateSets, const UE::Geometry::FAxisAlignedBox3d& UpdateSetBounds);





	//===============================================================================================================
	// Change Support. These changes are primarily used for Undo/Redo, however there is no strict assumption
	// about this internally, objects of these change types could also be used to perform more structured editing.
	// (Note that these functions simply forward the change events to the child UDynamicMesh, which will
	// 	post a mesh-change event that 
	//
public:
	/**
	 * Apply a vertex deformation change to the mesh
	 */
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override;

	/**
	 * Apply a general mesh change to the mesh
	 */
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) override;

	/**
	* Apply a mesh replacement change to mesh
	*/
	virtual void ApplyChange(const FMeshReplacementChange* Change, bool bRevert) override;

	/**
	 * This delegate fires when the mesh has been changed
	 */
	FSimpleMulticastDelegate OnMeshChanged;

	/**
	 * This delegate fires when the mesh vertices have been changed via an FMeshVertexChange
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FMeshVerticesModified, USimpleDynamicMeshComponent*, const FMeshVertexChange*, bool);
	FMeshVerticesModified OnMeshVerticesChanged;

	/**
	 * When a FMeshChange or FMeshVertexChange is applied, by default we currently fully invalidate the render proxy. However in certain
	 * realtime situations (eg like Sculpting tools) it can be critical to undo/redo performance to do more optimized render data updates
	 * (eg using one of the FastXYZ functions above). To allow for that, the full proxy invalidation on change can be (temporarily!) disabled 
	 * using this function. 
	 */
	void SetInvalidateProxyOnChangeEnabled(bool bEnabled);

	/** @return true if InvalidateProxyOnChange is enabled (default) */
	bool GetInvalidateProxyOnChangeEnabled() const { return bInvalidateProxyOnChange; }

protected:
	/** If false, we don't completely invalidate the RenderProxy when ApplyChange() is called (assumption is it will be handled elsewhere) */
	bool bInvalidateProxyOnChange = true;

	/** Handle for OnMeshObjectChanged which is registered with MeshObject::OnMeshChanged delegate */
	FDelegateHandle MeshObjectChangedHandle;

	/** Called whenever internal MeshObject is modified, fires OnMeshChanged and OnMeshVerticesChanged above */
	void OnMeshObjectChanged(UDynamicMesh* ChangedMeshObject, FDynamicMeshChangeInfo ChangeInfo);




	//===============================================================================================================
	// Support for specifying per-triangle colors as vertex colors. This allows external code to dynamically override 
	// the vertex colors on the rendered mesh. The lambda that is passed is held for the lifetime of the Component and
	// must remain valid. A Material that uses the vertex colors must be applied, otherwise setting this override will
	// have no visible effect. If the colors change externally, FastNotifyColorsUpdated() can be used to do the 
	// minimal vertex buffer updates necessary in the RenderProxy
	//
public:
	/** Clear an active triangle color function if one exists, and update the mesh */
	virtual void SetTriangleColorFunction(TUniqueFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFuncIn,
										  EDynamicMeshComponentRenderUpdateMode UpdateMode = EDynamicMeshComponentRenderUpdateMode::FastUpdate);

	/** Clear an active triangle color function if one exists, and update the mesh */
	virtual void ClearTriangleColorFunction(EDynamicMeshComponentRenderUpdateMode UpdateMode = EDynamicMeshComponentRenderUpdateMode::FastUpdate);

	/** @return true if a triangle color function is configured */
	virtual bool HasTriangleColorFunction();

protected:
	/** If this function is set, we will use these colors instead of vertex colors */
	TUniqueFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFunc = nullptr;

	/** This function is passed via lambda to the RenderProxy to be able to access TriangleColorFunc */
	FColor GetTriangleColor(const FDynamicMesh3* Mesh, int TriangleID);





	//===============================================================================================================
	// Support for Secondary triangle index buffers. When this is configured, then triangles identified
	// by the filtering predicate function will be placed in a second set of RenderBuffers at the SceneProxy level.
	// This can be combined with the SecondaryRenderMaterial support in UBaseDynamicMeshComponent to draw
	// that triangle set with a different material, to efficiently accomplish UI features like highlighting a
	// subset of mesh triangles.
	//
public:
	/**
	 * If Secondary triangle buffers are enabled, then we will filter triangles that pass the given predicate
	 * function into a second index buffer. These triangles will be drawn with the Secondary render material
	 * that is set in the BaseDynamicMeshComponent. Calling this function invalidates the SceneProxy.
	 */
	virtual void EnableSecondaryTriangleBuffers(TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFunc);

	/**
	 * Disable secondary triangle buffers. This invalidates the SceneProxy.
	 */
	virtual void DisableSecondaryTriangleBuffers();

protected:
	TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFunc = nullptr;




	//===============================================================================================================
	// Support for a Render Decomposition, which is basically a segmentation of the mesh triangles into
	// subsets which will be turned into separate RenderBuffers in the Render Proxy. If this is configured,
	// then various of the FastNotifyXYZUpdated() functions above will only need to rebuild the RenderBuffers
	// that include affected triangles. The FMeshRenderDecomposition implementation has various options for
	// building decompositions based on material, spatial clustering, etc.
	//
public:
	/**
	 * Configure a decomposition of the mesh, which will result in separate render buffers for each 
	 * decomposition triangle group. Invalidates existing SceneProxy.
	 */
	virtual void SetExternalDecomposition(TUniquePtr<FMeshRenderDecomposition> Decomposition);

protected:
	TUniquePtr<FMeshRenderDecomposition> Decomposition;




	//===============================================================================================================
	// IRenderMeshPostProcessor Support. If a RenderMesh Postprocessor is configured, then instead of directly 
	// passing the internal mesh to the RenderProxy, IRenderMeshPostProcessor::PostProcess is applied to populate
	// the internal RenderMesh which is passed instead. This allows things like Displacement or Subdivision to be 
	// done on-the-fly at the rendering level (which is potentially more efficient).
	//
public:
	/**
	 * Add a render mesh processor, to be called before the mesh is sent for rendering.
	 */
	virtual void SetRenderMeshPostProcessor(TUniquePtr<IRenderMeshPostProcessor> Processor);

	/**
	 * The SceneProxy should call these functions to get the post-processed RenderMesh. (See IRenderMeshPostProcessor.)
	 */
	virtual FDynamicMesh3* GetRenderMesh();

	/**
	 * The SceneProxy should call these functions to get the post-processed RenderMesh. (See IRenderMeshPostProcessor.)
	 */
	virtual const FDynamicMesh3* GetRenderMesh() const;

protected:
	TUniquePtr<IRenderMeshPostProcessor> RenderMeshPostProcessor;
	TUniquePtr<FDynamicMesh3> RenderMesh;




	//===============================================================================================================
	// Support for Component attachment change notifications via delegates. Standard UE
	// Actor/Component hierarchy does not generally provide these capabilities, but in some use
	// cases (eg procedural mesh Actors) we need to know things like when the Component set inside
	// an Actor is modified.
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FComponentChildrenChangedDelegate, USceneComponent*, bool);

	/**
	 * The OnChildAttached() and OnChildDetached() implementations (from USceneComponent API) broadcast this delegate. This
	 * allows Actors that have USimpleDynamicMeshComponent's to respond to changes in their Component hierarchy.
	 */
	FComponentChildrenChangedDelegate OnChildAttachmentModified;




protected:

	//
	// standard Component internals, for computing bounds and managing the SceneProxy
	//

	/** Current local-space bounding box of Mesh */
	UE::Geometry::FAxisAlignedBox3d LocalBounds;

	/** Recompute LocalBounds from the current Mesh */
	void UpdateLocalBounds();

	/**
	 * This is called to tell our RenderProxy about modifications to the material set.
	 * We need to pass this on for things like material validation in the Editor.
	 */
	virtual void NotifyMaterialSetUpdated();

	/**
	 * If the render proxy is invalidated (eg by MarkRenderStateDirty()), it will be destroyed at the end of
	 * the frame, but the base SceneProxy pointer is not nulled out immediately. As a result if we call various
	 * partial-update functions after invalidating the proxy, they may be operating on an invalid proxy.
	 * So we have to keep track of proxy-valid state ourselves.
	 */
	bool bProxyValid = false;

	/** 
	 * @return current render proxy, if valid, otherwise nullptr 
	 */
	FSimpleDynamicMeshSceneProxy* GetCurrentSceneProxy();

	/**
	 * Fully invalidate all rendering data for this Component. Current Proxy will be discarded, Bounds and possibly Tangents recomputed, etc
	 */
	void ResetProxy();

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	//~ USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void OnChildAttached(USceneComponent* ChildComponent) override;
	virtual void OnChildDetached(USceneComponent* ChildComponent) override;

	//~ UObject Interface.
	virtual void PostLoad() override;








	///===============================================================================================================
	// The functions below should be considered DEPRECATED
	// They are slated for removal (but require some 
	// refactoring for that to be possible)
	//
public:

	/**
	 * initialize the internal mesh from a MeshDescription
	 */
	virtual void InitializeMesh(const FMeshDescription* MeshDescription) override;


	/** How should Tangents be calculated/handled */
	UPROPERTY()
	EDynamicMeshTangentCalcType TangentsType = EDynamicMeshTangentCalcType::NoTangents;

	//
	// Separate Tangents support (todo: remove this)
	//

	/**
	 * Copy externally-calculated tangents into the internal tangets buffer.
	 * @param bFastUpdateIfPossible if true, will try to do a fast normals/tangets update of the SceneProxy, instead of full invalidatiohn
	 */
	void UpdateTangents(const UE::Geometry::FMeshTangentsf* ExternalTangents, bool bFastUpdateIfPossible);

	/**
	 * Copy externally-calculated tangents into the internal tangets buffer.
	 * @param bFastUpdateIfPossible if true, will try to do a fast normals/tangets update of the SceneProxy, instead of full invalidatiohn
	 */
	void UpdateTangents(const UE::Geometry::FMeshTangentsd* ExternalTangents, bool bFastUpdateIfPossible);

	/**
	 * @return pointer to internal tangents object.
	 * @warning calling this with TangentsType = AutoCalculated will result in possibly-expensive Tangents calculation
	 * @warning this is only currently safe to call on the Game Thread!!
	 */
	const UE::Geometry::FMeshTangentsf* GetTangents();


	/**
	 * Write the internal mesh to a MeshDescription
	 * @param bHaveModifiedTopology if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	 * @param ConversionOptions struct of additional options for the conversion
	 */
	virtual void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions) override;

	/**
	* Write the internal mesh to a MeshDescription with default conversion options
	* @param bHaveModifiedTopology if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	*/
	void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology)
	{
		FConversionToMeshDescriptionOptions ConversionOptions;
		Bake(MeshDescription, bHaveModifiedTopology, ConversionOptions);
	}

	/**
	 * Apply transform to internal mesh. Updates Octree and RenderProxy if available.
	 * @param bInvert if true, inverse tranform is applied instead of forward transform
	 */
	virtual void ApplyTransform(const UE::Geometry::FTransform3d& Transform, bool bInvert) override;


protected:
	/** Separate tangents storage (should be replaced with DynamicMesh Tangents attribute overlays) */
	bool bTangentsValid = false;
	UE::Geometry::FMeshTangentsf Tangents;
};
