// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveToolBuilder.h"
#include "Drawing/LineSetComponent.h"
#include "BaseTools/BaseCreateFromSelectedTool.h"

#include "CompositionOps/BooleanMeshesOp.h"

#include "CSGMeshesTool.generated.h"

// predeclarations
class FDynamicMesh3;


/**
 * Standard properties of the CSG operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** The type of operation */
	UPROPERTY(EditAnywhere, Category = Options)
	ECSGOperation Operation = ECSGOperation::DifferenceAB;

	/** Show boundary edges created by the CSG operation -- often due to numerical error */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowNewBoundaryEdges = true;

	/** Automatically attempt to fill any holes left by CSG (e.g. due to numerical errors) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAttemptFixHoles = false;

	/** If true, only the first mesh will keep its materials assignments; all other triangles will be assigned material 0 */
	UPROPERTY(EditAnywhere, Category = Materials)
	bool bOnlyUseFirstMeshMaterials = false;
};



/**
 * Simple Mesh Plane Cutting Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesTool : public UBaseCreateFromSelectedTool
{
	GENERATED_BODY()

public:

	UCSGMeshesTool() {}

protected:

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

	UPROPERTY()
	UCSGMeshesToolProperties* CSGProperties;

	TArray<TSharedPtr<FDynamicMesh3>> OriginalDynamicMeshes;

	UPROPERTY()
	ULineSetComponent* DrawnLineSet;

	// for visualization of any errors in the currently-previewed CSG operation
	TArray<int> CreatedBoundaryEdges;
};




UCLASS()
class MESHMODELINGTOOLS_API UCSGMeshesToolBuilder : public UBaseCreateFromSelectedToolBuilder
{
	GENERATED_BODY()

public:

	virtual TOptional<int32> MaxComponentsSupported() const override
	{
		return TOptional<int32>(2);
	}


	virtual int32 MinComponentsSupported() const override
	{
		return 2;
	}

	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const override
	{
		return NewObject<UCSGMeshesTool>(Outer);
	}
};



