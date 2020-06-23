// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseDynamicMeshComponent.h"
#include "MeshConversionOptions.h"
#include "Drawing/MeshRenderDecomposition.h"
#include "MeshTangents.h"
#include "TransformTypes.h"

#include "SimpleDynamicMeshComponent.generated.h"

// predecl
struct FMeshDescription;

/** internal FPrimitiveSceneProxy defined in SimpleDynamicMeshSceneProxy.h */
class FSimpleDynamicMeshSceneProxy;



/** 
 * USimpleDynamicMeshComponent is a mesh component similar to UProceduralMeshComponent,
 * except it bases the renderable geometry off an internal FDynamicMesh3 instance.
 * 
 * There is some support for undo/redo on the component (@todo is this the right place?)
 * 
 * This component draws wireframe-on-shaded when Wireframe is enabled, or when bExplicitShowWireframe = true
 *
 */
UCLASS(hidecategories = (LOD, Physics, Collision), editinlinenew, ClassGroup = Rendering)
class MODELINGCOMPONENTS_API USimpleDynamicMeshComponent : public UBaseDynamicMeshComponent
{
	GENERATED_UCLASS_BODY()


public:
	/** How should Tangents be calculated/handled */
	UPROPERTY()
	EDynamicMeshTangentCalcType TangentsType = EDynamicMeshTangentCalcType::NoTangents;

public:
	/**
	 * initialize the internal mesh from a MeshDescription
	 */
	virtual void InitializeMesh(FMeshDescription* MeshDescription) override;

	/**
	 * @return pointer to internal mesh
	 */
	virtual FDynamicMesh3* GetMesh() override { return Mesh.Get(); }

	/**
	 * @return pointer to internal mesh
	 */
	virtual const FDynamicMesh3* GetMesh() const override { return Mesh.Get(); }

	/**
	 * @return the current internal mesh, which is replaced with an empty mesh
	 */
	TUniquePtr<FDynamicMesh3> ExtractMesh(bool bNotifyUpdate);

	/**
	 * Copy externally-calculated tangents into the internal tangets buffer.
	 * @param bFastUpdateIfPossible if true, will try to do a fast normals/tangets update of the SceneProxy, instead of full invalidatiohn
	 */
	void UpdateTangents(const FMeshTangentsf* ExternalTangents, bool bFastUpdateIfPossible);

	/**
	 * Copy externally-calculated tangents into the internal tangets buffer.
	 * @param bFastUpdateIfPossible if true, will try to do a fast normals/tangets update of the SceneProxy, instead of full invalidatiohn
	 */
	void UpdateTangents(const FMeshTangentsd* ExternalTangents, bool bFastUpdateIfPossible);


	/**
	 * @return pointer to internal tangents object. 
	 * @warning calling this with TangentsType = AutoCalculated will result in possibly-expensive Tangents calculation
	 * @warning this is only currently safe to call on the Game Thread!!
	 */
	const FMeshTangentsf* GetTangents();




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
	virtual void ApplyTransform(const FTransform3d& Transform, bool bInvert) override;


	//
	// change tracking/etc
	//

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


	/** If false, we don't completely invalidate the RenderProxy when ApplyChange() is called (assumption is it will be handled elsewhere) */
	UPROPERTY()
	bool bInvalidateProxyOnChange = true;

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
	 * This delegate fires when ApplyChange(FMeshVertexChange) executes
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FMeshVerticesModified, USimpleDynamicMeshComponent*, const FMeshVertexChange*, bool);
	FMeshVerticesModified OnMeshVerticesChanged;


	/**
	 * if true, we always show the wireframe on top of the shaded mesh, even when not in wireframe mode
	 */
	UPROPERTY()
	bool bExplicitShowWireframe = false;

	/**
	 * Configure whether wireframe rendering is enabled or not
	 */
	virtual void SetEnableWireframeRenderPass(bool bEnable) override { bExplicitShowWireframe = bEnable; }

	/**
	 * @return true if wireframe rendering pass is enabled
	 */
	virtual bool EnableWireframeRenderPass() const override { return bExplicitShowWireframe; }


	/**
	 * If this function is set, we will use these colors instead of vertex colors
	 */
	TFunction<FColor(const FDynamicMesh3*, int)> TriangleColorFunc = nullptr;


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

	/**
	 * Configure a decomposition of the mesh, which will result in separate render buffers for each decomposition triangle group.
	 * Invalidates existing SceneProxy.
	 */
	virtual void SetExternalDecomposition(TUniquePtr<FMeshRenderDecomposition> Decomposition);


public:

	// do not use this
	UPROPERTY()
	bool bDrawOnTop = false;

	// do not use this
	void SetDrawOnTop(bool bSet);


protected:
	/**
	 * This is called to tell our RenderProxy about modifications to the material set.
	 * We need to pass this on for things like material validation in the Editor.
	 */
	virtual void NotifyMaterialSetUpdated();

private:

	FSimpleDynamicMeshSceneProxy* GetCurrentSceneProxy() { return (FSimpleDynamicMeshSceneProxy*)SceneProxy; }

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	TUniquePtr<FDynamicMesh3> Mesh;
	void InitializeNewMesh();

	// local-space bounding of Mesh
	FAxisAlignedBox3d LocalBounds;

	bool bTangentsValid = false;
	FMeshTangentsf Tangents;
	
	FColor GetTriangleColor(const FDynamicMesh3* Mesh, int TriangleID);

	TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFunc = nullptr;

	TUniquePtr<FMeshRenderDecomposition> Decomposition;
};
