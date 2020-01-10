// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseDynamicMeshComponent.h"
#include "MeshConversionOptions.h"

#include "MeshTangents.h"

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
	void InitializeMesh(FMeshDescription* MeshDescription);

	/**
	 * @return pointer to internal mesh
	 */
	FDynamicMesh3* GetMesh() { return Mesh.Get(); }

	/**
	 * @return the current internal mesh, which is replaced with an empty mesh
	 */
	TUniquePtr<FDynamicMesh3> ExtractMesh(bool bNotifyUpdate);

	/**
	 * @return pointer to internal tangents object. 
	 * @warning calling this with TangentsType = AutoCalculated will result in possibly-expensive Tangents calculation
	 */
	FMeshTangentsf* GetTangents();


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
	 * This delegate fires when a FCommandChange is applied to this component, so that
	 * parent objects know the mesh has changed.
	 */
	FSimpleMulticastDelegate OnMeshChanged;


	void SetDrawOnTop(bool bSet);

	/**
	 * if true, we always show the wireframe on top of the shaded mesh, even when not in wireframe mode
	 */
	UPROPERTY()
	bool bExplicitShowWireframe = false;


	UPROPERTY()
	bool bDrawOnTop = false;



	TFunction<FColor(int)> TriangleColorFunc = nullptr;
	void FastNotifyColorsUpdated();

	void FastNotifyPositionsUpdated();

private:

	FSimpleDynamicMeshSceneProxy* CurrentProxy = nullptr;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ Begin USceneComponent Interface.

	TUniquePtr<FDynamicMesh3> Mesh;
	void InitializeNewMesh();

	bool bTangentsValid = false;
	FMeshTangentsf Tangents;
	
	FColor GetTriangleColor(int TriangleID);

	//friend class FCustomMeshSceneProxy;
};
