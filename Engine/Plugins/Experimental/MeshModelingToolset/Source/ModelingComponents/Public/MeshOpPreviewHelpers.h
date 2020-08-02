// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
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


	/**
	 * Read back a copy of current preview mesh.
	 * @param bOnlyIfValid if true, then only create mesh copy if HaveValidResult() == true
	 * @return true if MeshOut was initialized
	 */
	bool GetCurrentResultCopy(FDynamicMesh3& MeshOut, bool bOnlyIfValid = true);


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

	/**
	 * Set time that Preview will wait before showing working material
	 */
	void SetWorkingMaterialDelay(float TimeInSeconds) { SecondsBeforeWorkingMaterial = TimeInSeconds; }

	/**
	 * @return true if currently using the 'in progress' working material
	 */
	bool IsUsingWorkingMaterial();


	//
	// Change notification
	//
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMeshUpdated, UMeshOpPreviewWithBackgroundCompute*);
	/** This delegate is broadcast whenever the embedded preview mesh is updated */
	FOnMeshUpdated OnMeshUpdated;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpCompleted, const FDynamicMeshOperator*);
	FOnOpCompleted OnOpCompleted;

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

	float SecondsBeforeWorkingMaterial = 2.0;

	// this object manages the background computes
	TUniquePtr<FBackgroundDynamicMeshComputeSource> BackgroundCompute;

	// update the PreviewMesh if a new result is available from BackgroundCompute
	void UpdateResults();

};







/**
 * TGenericDataBackgroundCompute is an infrastructure object that implements a common UI
 * pattern in interactive 3D tools, where we want to run an expensive parameterized computation 
 * (via a TGenericDataOperator) in a background thread so as to not block the UI. If the user changes 
 * parameters while the Operator is running, it should be canceled and restarted.
 *
 * The TGenericDataOperator are provided by the owner via a IGenericDataOperatorFactory implementation.
 * The owner must also Tick() this object regularly to allow results to be extracted from the
 * background thread and appropriate delegates fired when that occurs.
 */
template<typename ResultDataType>
class TGenericDataBackgroundCompute
{
public:
	using OperatorType = TGenericDataOperator<ResultDataType>;
	using FactoryType = IGenericDataOperatorFactory<ResultDataType>;
	using ComputeSourceType = TBackgroundModelingComputeSource<OperatorType, FactoryType>;

	//
	// required calls to setup/update/shutdown this object
	// 

	/**
	 * @param OpGenerator This factory is called to create new Operators on-demand
	 */
	void Setup(FactoryType* OpGenerator)
	{
		BackgroundCompute = MakeUnique<ComputeSourceType>(OpGenerator);
		bResultValid = false;
	}

	/**
	 * Terminate any active computation and return the current Result
	 */
	TUniquePtr<ResultDataType> Shutdown()
	{
		BackgroundCompute->CancelActiveCompute();
		return MoveTemp(CurrentResult);
	}

	/**
	* Terminate any active computation without returning anything
	*/
	void Cancel()
	{
		BackgroundCompute->CancelActiveCompute();
	}

	/**
	 * Tick the background computation to check for updated results
	 * @warning this must be called regularly for the class to function properly
	 */
	void Tick(float DeltaTime)
	{
		if (BackgroundCompute)
		{
			BackgroundCompute->Tick(DeltaTime);
		}
		UpdateResults();
	}

	//
	// Control flow
	// 

	/**
	 * Request that the current computation be canceled and a new one started
	 */
	void InvalidateResult()
	{
		check(BackgroundCompute);
		if (BackgroundCompute)
		{
			BackgroundCompute->NotifyActiveComputeInvalidated();
			bResultValid = false;
		}
	}

	/**
	 * @return true if the current Result is valid, ie no update being actively computed
	 */
	bool HaveValidResult() const { return bResultValid; }


	//
	// Change notification
	//

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpCompleted, const OperatorType*);
	/** OnOpCompleted is fired via Tick() when an Operator finishes, with the operator pointer as argument  */
	FOnOpCompleted OnOpCompleted;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnResultUpdated, const TUniquePtr<ResultDataType>& );
	/** OnResultUpdated is fired via Tick() when an Operator finishes, with the computed result as argument  */
	FOnResultUpdated OnResultUpdated;

protected:
	// state flag, if true then we have valid result
	bool bResultValid = false;

	// current result value
	TUniquePtr<ResultDataType> CurrentResult;

	// this object manages the background computes
	TUniquePtr<ComputeSourceType> BackgroundCompute;

	// update CurrentResult if a new result is available from BackgroundCompute, and fires relevant signals
	void UpdateResults()
	{
		check(BackgroundCompute);
		if (BackgroundCompute)
		{
			EBackgroundComputeTaskStatus Status = BackgroundCompute->CheckStatus();
			if (Status == EBackgroundComputeTaskStatus::NewResultAvailable)
			{
				TUniquePtr<OperatorType> ResultOp = BackgroundCompute->ExtractResult();
				OnOpCompleted.Broadcast(ResultOp.Get());

				CurrentResult = ResultOp->ExtractResult();
				bResultValid = true;

				OnResultUpdated.Broadcast(CurrentResult);
			}
		}
	}

};