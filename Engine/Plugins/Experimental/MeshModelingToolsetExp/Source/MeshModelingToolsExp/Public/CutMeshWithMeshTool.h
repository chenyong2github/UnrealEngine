// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveToolBuilder.h"
#include "Drawing/LineSetComponent.h"
#include "BaseTools/BaseCreateFromSelectedTool.h"

#include "CutMeshWithMeshTool.generated.h"

// predeclarations
class UPreviewMesh;
PREDECLARE_USE_GEOMETRY_CLASS(FDynamicMesh3);


/**
 * Standard properties of the CutMeshWithMesh operation
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UCutMeshWithMeshToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Show boundary edges created by the Boolean operations -- often due to numerical error */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bShowNewBoundaryEdges = true;

	/** Automatically attempt to fill any holes left by Booleans (e.g. due to numerical errors) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAttemptFixHoles = false;

	/** Try to collapse extra edges created by the Boolean operation */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bCollapseExtraEdges = true;

	/** If true, only the first mesh will keep its materials assignments; all other triangles will be assigned material 0 */
	UPROPERTY(EditAnywhere, Category = Materials)
	bool bOnlyUseFirstMeshMaterials = false;
};


/**
 * UCutMeshWithMeshTool cuts an input mesh into two pieces based on a second input mesh.
 * Essentially this just both a Boolean Subtract and a Boolean Intersection. However
 * doing those as two separate operations involves quite a few steps, so this Tool
 * does it in a single step and with some improved efficiency.
 */
UCLASS()
class MESHMODELINGTOOLSEXP_API UCutMeshWithMeshTool : public UBaseCreateFromSelectedTool
{
	GENERATED_BODY()

public:

	UCutMeshWithMeshTool() {}

protected:

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual void ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh = true) override;

	virtual void SetupProperties() override;
	virtual void SaveProperties() override;
	virtual void SetPreviewCallbacks() override;

	virtual FString GetCreatedAssetName() const;
	virtual FText GetActionName() const;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

	virtual void Shutdown(EToolShutdownType ShutdownType) override;

protected:
	void UpdateVisualization();

	UPROPERTY()
	TObjectPtr<UCutMeshWithMeshToolProperties> CutProperties;

	UPROPERTY()
	TObjectPtr<UPreviewMesh> IntersectPreviewMesh;

	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalTargetMesh;
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalCuttingMesh;

	UPROPERTY()
	TObjectPtr<ULineSetComponent> DrawnLineSet;

	// for visualization of any errors in the currently-previewed CSG operation
	TArray<int> CreatedSubtractBoundaryEdges;
	TArray<int> CreatedIntersectBoundaryEdges;

	FDynamicMesh3 IntersectionMesh;
};




UCLASS()
class MESHMODELINGTOOLSEXP_API UCutMeshWithMeshToolBuilder : public UBaseCreateFromSelectedToolBuilder
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
		return NewObject<UCutMeshWithMeshTool>(Outer);
	}
};



