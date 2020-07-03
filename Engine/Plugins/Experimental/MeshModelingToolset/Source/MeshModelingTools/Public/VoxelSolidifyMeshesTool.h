// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#include "BaseTools/BaseVoxelTool.h"

#include "VoxelSolidifyMeshesTool.generated.h"




/**
 * Properties of the solidify operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelSolidifyMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Winding number threshold to determine what is consider inside the mesh */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.1", UIMax = ".9", ClampMin = "-10", ClampMax = "10"))
	double WindingThreshold = .5;

	/** How far we allow bounds of solid surface to go beyond the bounds of the original input surface before clamping / cutting the surface off */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "10", ClampMin = "0", ClampMax = "1000"))
	double ExtendBounds = 1;

	/** How many binary search steps to take when placing vertices on the surface */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "6", ClampMin = "0", ClampMax = "10"))
	int SurfaceSearchSteps = 4;

	/** Whether to fill at the border of the bounding box, if the surface extends beyond the voxel boundaries */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bSolidAtBoundaries = true;

	/** If true, treats mesh surfaces with open boundaries as having a fixed, user-defined thickness */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bMakeOffsetSurfaces = false;

	/** Thickness of offset surfaces */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = ".1", UIMax = "100", ClampMin = ".001", ClampMax = "1000", EditCondition = "bMakeOffsetSurfaces == true"))
	double OffsetThickness = 5;
};



/**
 * Tool to take one or more meshes, possibly intersecting and possibly with holes, and create a single solid mesh with consistent inside/outside
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelSolidifyMeshesTool : public UBaseVoxelTool
{
	GENERATED_BODY()

public:

	UVoxelSolidifyMeshesTool() {}

	virtual void SetupProperties() override;
	virtual void SaveProperties() override;

	virtual FString GetCreatedAssetName() const override;
	virtual FText GetActionName() const override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

protected:

	UPROPERTY()
	UVoxelSolidifyMeshesToolProperties* SolidifyProperties;

};


UCLASS()
class MESHMODELINGTOOLS_API UVoxelSolidifyMeshesToolBuilder : public UBaseCreateFromSelectedToolBuilder
{
	GENERATED_BODY()

public:
	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const override
	{
		return NewObject<UVoxelSolidifyMeshesTool>(Outer);
	}
};


