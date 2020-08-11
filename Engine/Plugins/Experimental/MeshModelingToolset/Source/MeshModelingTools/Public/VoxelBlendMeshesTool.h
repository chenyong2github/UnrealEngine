// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/BaseVoxelTool.h"

#include "VoxelBlendMeshesTool.generated.h"


/**
 * Properties of the blend operation
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelBlendMeshesToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** Blend power controls the shape of the blend between shapes */
	UPROPERTY(EditAnywhere, Category = Blend, meta = (UIMin = "1", UIMax = "4", ClampMin = "1", ClampMax = "10"))
	double BlendPower = 2;

	/** Blend falloff controls the size of the blend region */
	UPROPERTY(EditAnywhere, Category = Blend, meta = (UIMin = ".1", UIMax = "100", ClampMin = ".001", ClampMax = "1000"))
	double BlendFalloff = 10;

	/** Solidify the input mesh(es) before processing, fixing results for inputs with holes and/or self-intersections */
	UPROPERTY(EditAnywhere, Category = Solidify)
	bool bSolidifyInput = false;

	/** Remove internal surfaces from the solidified input */
	UPROPERTY(EditAnywhere, Category = Solidify, meta = (EditCondition = "bSolidifyInput == true"))
	bool bRemoveInternalsAfterSolidify = false;

	/** Offset surface to create when solidifying any open-boundary inputs; if 0 then no offset surfaces are created */
	UPROPERTY(EditAnywhere, Category = Solidify, meta = (EditCondition = "bSolidifyInput == true"))
	double OffsetSolidifySurface = 0.0;
};



/**
 * Tool to smoothly blend meshes together
 */
UCLASS()
class MESHMODELINGTOOLS_API UVoxelBlendMeshesTool : public UBaseVoxelTool
{
	GENERATED_BODY()

public:

	UVoxelBlendMeshesTool() {}

protected:

	virtual void SetupProperties() override;
	virtual void SaveProperties() override;

	virtual FString GetCreatedAssetName() const override;
	virtual FText GetActionName() const override;

	// IDynamicMeshOperatorFactory API
	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

	UPROPERTY()
	UVoxelBlendMeshesToolProperties* BlendProperties;
};


UCLASS()
class MESHMODELINGTOOLS_API UVoxelBlendMeshesToolBuilder : public UBaseCreateFromSelectedToolBuilder
{
	GENERATED_BODY()

public:
	virtual int32 MinComponentsSupported() const override { return 2; }

	virtual UBaseCreateFromSelectedTool* MakeNewToolInstance(UObject* Outer) const override
	{
		return NewObject<UVoxelBlendMeshesTool>(Outer);
	}
};


