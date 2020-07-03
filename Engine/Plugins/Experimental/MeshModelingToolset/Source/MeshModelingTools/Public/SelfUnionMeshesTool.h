// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Drawing/LineSetComponent.h"
#include "BaseTools/BaseCreateFromSelectedTool.h"

#include "CompositionOps/SelfUnionMeshesOp.h"

#include "SelfUnionMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;



/**
 * Standard properties of the self-union operation
 */
UCLASS()
class MESHMODELINGTOOLS_API USelfUnionMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Automatically attempt to fill any holes left by merging (e.g. due to numerical errors) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAttemptFixHoles = false;

	/** Show boundary edges created by the union operation -- often due to numerical error */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowNewBoundaryEdges = true;

	/** If true, remove open, visible geometry */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bTrimFlaps = false;

	/** Winding number threshold to determine what is consider inside the mesh */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	double WindingNumberThreshold = .5;

	/** If true, only the first mesh will keep its materials assignments; all other triangles will be assigned material 0 */
	UPROPERTY(EditAnywhere, Category = Materials)
	bool bOnlyUseFirstMeshMaterials = false;
};



/**
 * Union of meshes, resolving self intersections
 */
UCLASS()
class MESHMODELINGTOOLS_API USelfUnionMeshesTool : public UBaseCreateFromSelectedTool
{
	GENERATED_BODY()

public:

	USelfUnionMeshesTool() {}

protected:

	void TransformChanged(UTransformProxy* Proxy, FTransform Transform) override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual void ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh = true) override;

	virtual void SetupProperties() override;
	virtual void SaveProperties() override;
	virtual void SetPreviewCallbacks() override;

	virtual FString GetCreatedAssetName() const;
	virtual FText GetActionName() const;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:

	void UpdateVisualization();

protected:

	UPROPERTY()
	USelfUnionMeshesToolProperties* Properties;

	UPROPERTY()
	ULineSetComponent* DrawnLineSet;

	TSharedPtr<FDynamicMesh3> CombinedSourceMeshes;

	// for visualization of any errors in the currently-previewed merge operation
	TArray<int> CreatedBoundaryEdges;
};


UCLASS()
class MESHMODELINGTOOLS_API USelfUnionMeshesToolBuilder : public UBaseCreateFromSelectedToolBuilder
{
	GENERATED_BODY()

public:
	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const override
	{
		return NewObject<USelfUnionMeshesTool>(Outer);
	}
};



