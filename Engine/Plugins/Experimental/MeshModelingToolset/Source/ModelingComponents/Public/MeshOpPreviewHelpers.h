// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PreviewMesh.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "BackgroundModelingComputeSource.h"
#include "MeshOpPreviewHelpers.generated.h"


/**
 * FBackgroundDynamicMeshComputeSource is an instantiation of the TBackgroundModelingComputeSource
 * template for FDynamicMeshOperator / IDynamicMeshOperatorFactory
 */
using FBackgroundDynamicMeshComputeSource = TBackgroundModelingComputeSource<FDynamicMeshOperator, IDynamicMeshOperatorFactory>;


/**
 * FDynamicMeshOpResult is a container for a computed Mesh and Transform
 */
struct MODELINGCOMPONENTS_API FDynamicMeshOpResult
{
	TUniquePtr<FDynamicMesh3> Mesh;
	FTransform3d Transform;
};


/**
 * UMeshOpPreviewWithBackgroundCompute is an infrastructure object that implements a common UI
 * pattern in interactive 3D tools, where we want to run an expensive computation on a mesh that
 * is based on user-specified parameters, and show a preview of the result. The expensive computation 
 * (a MeshOperator) must run in a background thread so as to not block the UI. If the user
 * changes parameters while the Operator is running, it should be canceled and restarted. 
 * When it completes, the Preview will be updated. When the user is happy, the current Mesh is
 * returned to the owner of this object.
 * 
 * The MeshOperators are provided by the owner via a IDynamicMeshOperatorFactory implementation.
 * The owner must also Tick() this object regularly to allow the Preview to update when the
 * background computations complete.
 * 
 * If an InProgress Material is set (via ConfigureMaterials) then when a background computation
 * is active, this material will be used to draw the previous Preview result, to give the 
 * user a visual indication that work is happening.
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UMeshOpPreviewWithBackgroundCompute : public UObject
{
	GENERATED_BODY()
public:

	//
	// required calls to setup/update/shutdown this object
	// 

	/**
	 * @param InWorld the Preview mesh actor will be created in this UWorld
	 * @param OpGenerator This factory is called to create new MeshOperators on-demand
	 */
	void Setup(UWorld* InWorld, IDynamicMeshOperatorFactory* OpGenerator);

	/**
	 * Terminate any active computation and return the current Preview Mesh/Transform
	 */
	FDynamicMeshOpResult Shutdown();

	/**
	* Terminate any active computation without returning anything
	*/
	void Cancel();

	/**
	 * Tick the background computation and Preview update. 
	 * @warning this must be called regularly for the class to function properly
	 */
	void Tick(float DeltaTime);


	//
	// Control flow
	// 


	/**
	 * Request that the current computation be canceled and a new one started
	 */
	void InvalidateResult();

	/**
	 * @return true if the current PreviewMesh result is valid, ie no update being actively computed
	 */
	bool HaveValidResult() const { return bResultValid; }


	//
	// Optional configuration
	// 


	/**
	 * Configure the Standard and In-Progress materials
	 */
	void ConfigureMaterials(UMaterialInterface* StandardMaterial, UMaterialInterface* InProgressMaterial);

	/**
	 * Configure the Standard and In-Progress materials
	 */
	void ConfigureMaterials(TArray<UMaterialInterface*> StandardMaterials, UMaterialInterface* InProgressMaterial);


	/**
	 * Set the visibility of the Preview mesh
	 */
	void SetVisibility(bool bVisible);



	//
	// Change notification
	//

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMeshUpdated, UMeshOpPreviewWithBackgroundCompute*);
	/** This delegate is broadcast whenever the embedded preview mesh is updated */
	FOnMeshUpdated OnMeshUpdated;



public:
	// preview of MeshOperator result
	UPROPERTY()
	UPreviewMesh* PreviewMesh = nullptr;

	// input set of materials to assign to PreviewMesh
	UPROPERTY()
	TArray<UMaterialInterface*> StandardMaterials;

	// override material to forward to PreviewMesh if set
	UPROPERTY()
	UMaterialInterface* OverrideMaterial = nullptr;

	// if non-null, this material is swapped in when a background compute is active
	UPROPERTY()
	UMaterialInterface* WorkingMaterial = nullptr;

protected:
	// state flag, if true then we have valid result
	bool bResultValid = false;

	bool bVisible = true;

	// this object manages the background computes
	TUniquePtr<FBackgroundDynamicMeshComputeSource> BackgroundCompute;

	// update the PreviewMesh if a new result is available from BackgroundCompute
	void UpdateResults();

};
