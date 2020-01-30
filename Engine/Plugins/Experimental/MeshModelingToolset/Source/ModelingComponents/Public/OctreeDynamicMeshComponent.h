// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseDynamicMeshComponent.h"
#include "MeshConversionOptions.h"

#include "DynamicMeshOctree3.h"
#include "Util/IndexSetDecompositions.h"

#include "OctreeDynamicMeshComponent.generated.h"

// predecl
struct FMeshDescription;

/** internal FPrimitiveSceneProxy defined in OctreeDynamicMeshSceneProxy.h */
class FOctreeDynamicMeshSceneProxy;


/** 
 * UOctreeDynamicMeshComponent is a mesh component similar to UProceduralMeshComponent,
 * except it bases the renderable geometry off an internal FDynamicMesh3 instance.
 * The class generally has the same capabilities as USimpleDynamicMeshComponent.
 * 
 * A FDynamicMeshOctree3 is available to dynamically track the triangles of the mesh
 * (however the client is responsible for updating this octree).
 * Based on the Octree, the mesh is partitioned into chunks that are stored in separate
 * RenderBuffers in the FOctreeDynamicMeshSceneProxy.
 * Calling NotifyMeshUpdated() will result in only the "dirty" chunks being updated,
 * rather than the entire mesh.
 * 
 * (So, if you don't need this capability, and don't want to update an Octree, use USimpleDynamicMeshComponent!)
 */
UCLASS(hidecategories = (LOD, Physics, Collision), editinlinenew, ClassGroup = Rendering)
class MODELINGCOMPONENTS_API UOctreeDynamicMeshComponent : public UBaseDynamicMeshComponent
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * initialize the internal mesh from a MeshDescription
	 */
	void InitializeMesh(FMeshDescription* MeshDescription);

	/**
	 * @return pointer to internal mesh
	 */
	FDynamicMesh3* GetMesh() { return Mesh.Get(); }

	FDynamicMeshOctree3* GetOctree() { return Octree.Get(); }

	/**
	 * @return the current internal mesh, which is replaced with an empty mesh
	 */
	TUniquePtr<FDynamicMesh3> ExtractMesh(bool bNotifyUpdate);

	/**
	 * Write the internal mesh to a MeshDescription
	 * @param bHaveModifiedTopology if false, we only update the vertex positions in the MeshDescription, otherwise it is Empty()'d and regenerated entirely
	 * @param ConversionOptions struct of additional options for the conversion
	 */
	void Bake(FMeshDescription* MeshDescription, bool bHaveModifiedTopology, const FConversionToMeshDescriptionOptions& ConversionOptions);

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
	void ApplyTransform(const FTransform3d& Transform, bool bInvert);

	//
	// change tracking/etc
	//

	/**
	 * Call this if you update the mesh via GetMesh()
	 * @todo should provide a function that calls a lambda to modify the mesh, and only return const mesh pointer
	 */
	virtual void NotifyMeshUpdated() override;

	/**
	 * Apply a vertex deformation change to the internal mesh
	 */
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) override;

	/**
	 * Apply a general mesh change to the internal mesh
	 */
	virtual void ApplyChange(const FMeshChange* Change, bool bRevert) override;

	/**
	* Apply a general mesh replacement change to the internal mesh
	*/
	virtual void ApplyChange(const FMeshReplacementChange* Change, bool bRevert) override;


	/**
	 * This delegate fires when a FCommandChange is applied to this component, so that
	 * parent objects know the mesh has changed.
	 */
	FSimpleMulticastDelegate OnMeshChanged;

	/**
	 * if true, we always show the wireframe on top of the shaded mesh, even when not in wireframe mode
	 */
	UPROPERTY()
	bool bExplicitShowWireframe = false;

	/**
	 * @return true if wireframe rendering pass is enabled
	 */
	virtual bool EnableWireframeRenderPass() const override { return bExplicitShowWireframe; }

	/**
	 * If this function is set, we will use these colors instead of vertex colors
	 */
	TFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFunc = nullptr;

protected:
	/**
	 * This is called to tell our RenderProxy about modifications to the material set.
	 * We need to pass this on for things like material validation in the Editor.
	 */
	virtual void NotifyMaterialSetUpdated();

private:

	FOctreeDynamicMeshSceneProxy* GetCurrentSceneProxy() { return (FOctreeDynamicMeshSceneProxy*)SceneProxy; }

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	TUniquePtr<FDynamicMesh3> Mesh;
	TUniquePtr<FDynamicMeshOctree3> Octree;
	TUniquePtr<FDynamicMeshOctree3::FTreeCutSet> OctreeCut;
	FArrayIndexSetsDecomposition TriangleDecomposition;
	struct FCutCellIndexSet
	{
		FDynamicMeshOctree3::FCellReference CellRef;
		int32 DecompSetID;
	};
	TArray<FCutCellIndexSet> CutCellSetMap;
	int32 SpillDecompSetID;

	void InitializeNewMesh();



	FColor GetTriangleColor(int TriangleID);

	//friend class FCustomMeshSceneProxy;
};
