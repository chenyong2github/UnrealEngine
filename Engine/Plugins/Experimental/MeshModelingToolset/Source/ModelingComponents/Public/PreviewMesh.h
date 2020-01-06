// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimpleDynamicMeshComponent.h"
#include "DynamicMeshAABBTree3.h"
#include "InteractiveToolObjects.h"
#include "Changes/MeshVertexChange.h"
#include "Changes/MeshChange.h"
#include "PreviewMesh.generated.h"

class FDynamicMesh3;
struct FMeshDescription;
class FTangentsf;



/**
 * UPreviewMesh internally spawns a APreviewMeshActor to hold the preview mesh object.
 * We use this AInternalToolFrameworkActor subclass so that we can identify such objects
 * at higher levels (for example to prevent them from being deleted in the Editor)
 */
UCLASS()
class MODELINGCOMPONENTS_API APreviewMeshActor : public AInternalToolFrameworkActor
{
	GENERATED_BODY()
private:
	APreviewMeshActor();
public:

};



/** 
 * UPreviewMesh is a utility object that spawns and owns a transient mesh object in the World.
 * This can be used to show live preview geometry during modeling operations.
 * Call CreateInWorld() to set it up, and Disconnect() to shut it down.
 * 
 * Currently implemented via an internal Actor that has a USimpleDynamicMeshComponent root component,
 * with an AABBTree created/updated if UProperty bBuildSpatialDataStructure=true.
 * The Actor is destroyed on Disconnect().
 * 
 * The intention with UPreviewMesh is to provide a higher-level interface than the Component.
 * In future the internal Component may be replaced with another class (eg OctreeDynamicMeshComponent),
 * or automatically swap between the two, etc.
 * 
 * As a result direct access to the Actor/Component, or a non-const FDynamicMesh3, is intentionally not provided.
 * Wrapper functions are provided (or should be added) for necessary Actor/Component parameters.
 * To edit the mesh either a copy is done, or EditMesh()/ApplyChange() must be used.
 * These functions automatically update necessary internal data structures.
 * 
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UPreviewMesh : public UObject, public IMeshVertexCommandChangeTarget, public IMeshCommandChangeTarget, public IMeshReplacementCommandChangeTarget
{
	GENERATED_BODY()

public:
	UPreviewMesh();
	virtual ~UPreviewMesh();

	//
	// construction / destruction
	// 

	/**
	 * Create preview mesh in the World with the given transform
	 */
	void CreateInWorld(UWorld* World, const FTransform& WithTransform);

	/**
	 * Remove and destroy preview mesh
	 */
	void Disconnect();


	/**
	 * @return internal Root Component of internal Actor
	 */
	UPrimitiveComponent* GetRootComponent() { return DynamicMeshComponent; }


	//
	// visualization parameters
	// 

	/**
	 * Enable/disable wireframe overlay rendering
	 */
	void EnableWireframe(bool bEnable);

	/**
	 * Set material on the preview mesh
	 */
	void SetMaterial(UMaterialInterface* Material);

	/**
	 * Set material on the preview mesh
	 */
	void SetMaterial(int MaterialIndex, UMaterialInterface* Material);

	/**
	 * Set the entire material set on the preview mesh
	 */
	void SetMaterials(const TArray<UMaterialInterface*>& Materials);

	/**
	* Get material from the preview mesh
	*/
	UMaterialInterface* GetMaterial(int MaterialIndex = 0) const;

	/**
	 * Set an override material for the preview mesh. This material will override all the given materials.
	 */
	void SetOverrideRenderMaterial(UMaterialInterface* Material);

	/**
	 * Clear the override material for the preview mesh.
	 */
	void ClearOverrideRenderMaterial();

	/**
	 * @return the actual material that will be used for rendering for the given MaterialIndex. Will return override material if set.
	 * 
	 */
	UMaterialInterface* GetActiveMaterial(int MaterialIndex = 0) const;


	/**
	 * Set an secondary material for the preview mesh. This material will be applied to secondary triangle buffer if enabled.
	 */
	void SetSecondaryRenderMaterial(UMaterialInterface* Material);

	/**
	 * Clear the secondary material for the preview mesh.
	 */
	void ClearSecondaryRenderMaterial();


	/**
	 * Enable secondary triangle buffers. The Secondary material will be applied to any triangles that pass TriangleFilterFunc.
	 * @param TriangleFilterFunc predicate used to identify secondary triangles
	 */
	void EnableSecondaryTriangleBuffers(TUniqueFunction<bool(const FDynamicMesh3*, int32)> TriangleFilterFunc);

	/**
	 * Disable secondary triangle buffers
	 */
	void DisableSecondaryTriangleBuffers();



	/**
	 * Set the tangents mode for the underlying component, if available. 
	 * Note that this function may need to be called before the mesh is initialized.
	 */
	void SetTangentsMode(EDynamicMeshTangentCalcType TangentsType);

	/**
	 * @return a MeshTangents data structure for the underlying component, if available, otherwise nullptr
	 */
	FMeshTangentsf* GetTangents() const;


	/**
	 * Get the current transform on the preview mesh
	 */
	FTransform GetTransform() const;

	/**
	 * Set the transform on the preview mesh
	 */
	void SetTransform(const FTransform& UseTransform);

	/**
	 * @return true if the preview mesh is visible
	 */
	bool IsVisible() const;

	/**
	 * Set visibility state of the preview mesh
	 */
	void SetVisible(bool bVisible);


	/** Render data update hint */
	enum class ERenderUpdateMode
	{
		/** Do not update render data */
		NoUpdate,	
		/** Invalidate overlay of internal component, rebuilding all render data */
		FullUpdate,
		/** Attempt to do partial update of render data if possible */
		FastUpdate
	};

	/**
	 * Set the triangle color function for rendering / render data construction
	 */
	void SetTriangleColorFunction(TFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFunc, ERenderUpdateMode UpdateMode = ERenderUpdateMode::FullUpdate);

	/**
	 * Clear the triangle color function for rendering / render data construction
	 */
	void ClearTriangleColorFunction(ERenderUpdateMode UpdateMode = ERenderUpdateMode::FullUpdate);





	//
	// Queries
	// 

	/**
	 * Test for ray intersection with the preview mesh.
	 * Requires that bBuildSpatialDataStructure = true
	 * @return true if ray intersections mesh
	 */
	bool TestRayIntersection(const FRay3d& WorldRay);

	/**
	 * Find ray intersection with the preview mesh.
	 * Requires that bBuildSpatialDataStructure = true
	 * @param WorldRay ray in world space
	 * @param HitOut output hit data (only certain members are initialized, see implementation)
	 * @return true if ray intersections mesh
	 */
	bool FindRayIntersection(const FRay3d& WorldRay, FHitResult& HitOut);






	//
	// Read access to internal mesh
	// 

	/**
	 * Clear the preview mesh
	 */
	void ClearPreview();

	/**
	 * Update the internal mesh by copying the given Mesh
	 */
	void UpdatePreview(const FDynamicMesh3* Mesh);

	/**
	 * Initialize the internal mesh based on the given MeshDescription
	 */
	void InitializeMesh(FMeshDescription* MeshDescription);

	/**
	* @return pointer to the current FDynamicMesh used for preview  @todo deprecate this function, use GetMesh() instead
	*/
	const FDynamicMesh3* GetPreviewDynamicMesh() const { return GetMesh(); }

	/**
	* @return pointer to the current FDynamicMesh used for preview
	*/
	const FDynamicMesh3* GetMesh() const;

	/**
	 * @return the current preview FDynamicMesh, and replace with a new empty mesh
	 */
	TUniquePtr<FDynamicMesh3> ExtractPreviewMesh() const;

	/**
	 * Write the internal mesh to a MeshDescription
	 * @param bHaveModifiedToplogy if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	 */
	void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedToplogy);




	//
	// Edit access to internal mesh, and change-tracking/notification
	// 

	/**
	 * Apply EditFunc to the internal mesh and update internal data structures as necessary
	 */
	void EditMesh(TFunctionRef<void(FDynamicMesh3&)> EditFunc);

	/**
	 * Apply EditFunc to the internal mesh, and update spatial data structure if requested,
	 * but do not update/rebuild rendering data structures. NotifyDeferredEditOcurred() must be
	 * called to complete a deferred edit, this will update the rendering mesh.
	 * DeferredEditMesh can be called multiple times before NotifyDeferredEditCompleted() is called.
	 * @param EditFunc function that is applied to the internal mes
	 * @param bRebuildSpatial if true, and internal spatial data structure is enabled, rebuild it for updated mesh
	 */
	void DeferredEditMesh(TFunctionRef<void(FDynamicMesh3&)> EditFunc, bool bRebuildSpatial);

	/**
	 * Notify that a DeferredEditMesh sequence is complete and cause update of rendering data structures.
	 * @param UpdateMode type of rendering update required for the applied mesh edits
	 * @param bRebuildSpatial if true, and internal spatial data structure is enabled, rebuild it for updated mesh
	 */
	void NotifyDeferredEditCompleted(ERenderUpdateMode UpdateMode, bool bRebuildSpatial);


	/**
	 * Apply EditFunc to the internal mesh and update internal data structures as necessary.
	 * EditFunc is required to notify the given FDynamicMeshChangeTracker about all mesh changes
	 * @return FMeshChange extracted from FDynamicMeshChangeTracker that represents mesh edit
	 */
	TUniquePtr<FMeshChange> TrackedEditMesh(TFunctionRef<void(FDynamicMesh3&, FDynamicMeshChangeTracker&)> EditFunc);

	/**
	 * Apply/Revert a vertex deformation change to the internal mesh (implements IMeshVertexCommandChangeTarget)
	 */
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override;

	/**
	 * Apply/Revert a general mesh change to the internal mesh   (implements IMeshCommandChangeTarget)
	 */
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) override;

	/**
	* Apply/Revert a general mesh change to the internal mesh   (implements IMeshReplacementCommandChangeTarget)
	*/
	virtual void ApplyChange(const FMeshReplacementChange* Change, bool bRevert) override;

	/** @return delegate that is broadcast whenever the internal mesh component is changed */
	FSimpleMulticastDelegate& GetOnMeshChanged();


	/**
	 * Force rebuild of internal spatial data structure. Can be used in context of DeferredEditMesh to rebuild spatial data
	 * structure w/o rebuilding render data
	 */
	void ForceRebuildSpatial();




public:
	/** If true, we build a spatial data structure internally for the preview mesh, which allows for hit-testing */
	UPROPERTY()
	bool bBuildSpatialDataStructure;

	// results in component drawing w/o z-testing and with editor compositing. Do not recommend using this flag, will be deprecated/removed.
	UPROPERTY()
	bool bDrawOnTop;

protected:
	/** The temporary actor we create internally to own the preview mesh component */
	APreviewMeshActor* TemporaryParentActor = nullptr;

	/** This component is set as the root component of TemporaryParentActor */
	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent = nullptr;

	/** Spatial data structure that is initialized if bBuildSpatialDataStructure = true when UpdatePreview() is called */
	FDynamicMeshAABBTree3 MeshAABBTree;
};
