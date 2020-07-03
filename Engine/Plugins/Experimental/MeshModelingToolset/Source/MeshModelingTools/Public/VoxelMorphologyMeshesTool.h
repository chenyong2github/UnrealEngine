// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/BaseVoxelTool.h"
#include "CompositionOps/VoxelMorphologyMeshesOp.h"

#include "VoxelMorphologyMeshesTool.generated.h"



/**
 * Properties of the morphology tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelMorphologyMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Morphology)
	EMorphologyOperation Operation = EMorphologyOperation::Dilate;

	UPROPERTY(EditAnywhere, Category = Morphology, meta = (UIMin = ".1", UIMax = "100", ClampMin = ".001", ClampMax = "1000"))
	double Distance = 5;

	/** Solidify the input mesh(es) before processing, fixing results for inputs with holes and/or self-intersections */
	UPROPERTY(EditAnywhere, Category = Solidify)
	bool bSolidifyInput = false;

	/** Remove internal surfaces from the solidified input, before running morphology */
	UPROPERTY(EditAnywhere, Category = Solidify, meta = (EditCondition = "bSolidifyInput == true"))
	bool bRemoveInternalsAfterSolidify = false;

	/** Offset surface to create when solidifying any open-boundary inputs; if 0 then no offset surfaces are created */
	UPROPERTY(EditAnywhere, Category = Solidify, meta = (EditCondition = "bSolidifyInput == true"))
	double OffsetSolidifySurface = 0.0;
};



/**
 * Morphology tool -- dilate, contract, close, open operations on the input shape
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelMorphologyMeshesTool : public UBaseVoxelTool
{
	GENERATED_BODY()

public:

	UVoxelMorphologyMeshesTool() {}

protected:

	virtual void SetupProperties() override;
	virtual void SaveProperties() override;

	virtual FString GetCreatedAssetName() const override;
	virtual FText GetActionName() const override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UVoxelMorphologyMeshesToolProperties* MorphologyProperties;

};




UCLASS()
class MESHMODELINGTOOLS_API UVoxelMorphologyMeshesToolBuilder : public UBaseCreateFromSelectedToolBuilder
{
	GENERATED_BODY()

public:
	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const override
	{
		return NewObject<UVoxelMorphologyMeshesTool>(Outer);
	}
};

