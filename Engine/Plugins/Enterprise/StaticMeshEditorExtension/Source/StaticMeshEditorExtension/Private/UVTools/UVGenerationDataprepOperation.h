// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataPrepOperation.h"

#include "UVGenerationDataprepOperation.generated.h"

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName = "Generate Flatten Mapping UVs", ToolTip = "For each static mesh to process, generate a flat UV map in the specified channel"))
class STATICMESHEDITOREXTENSION_API UUVGenerationFlattenMappingOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UUVGenerationFlattenMappingOperation()
		: UVChannel(0),
		AngleThreshold(66.f),
		AreaWeight(0.7f)
	{
	}

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV Generation Settings", meta = (ToolTip="The UV channel where to generate the flatten mapping", ClampMin = "0", ClampMax = "7")) //Clampmax is from MAX_MESH_TEXTURE_COORDS_MD - 1
	int UVChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV Generation Settings", meta = (ClampMin = "1", ClampMax = "90"))
	float AngleThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV Generation Settings", meta = (ClampMin = "0", ClampMax = "1"))
	float AreaWeight;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};