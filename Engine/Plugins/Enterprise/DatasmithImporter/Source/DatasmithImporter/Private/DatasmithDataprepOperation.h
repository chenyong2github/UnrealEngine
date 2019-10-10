// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepOperation.h"

#include "DatasmithDataprepOperation.generated.h"

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Compute Lightmap Resolution", ToolTip = "For each static mesh to process, recompute the lilghtmap resolution based on the specified ratio") )
class UDatasmithComputeLightmapResolutionOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDatasmithComputeLightmapResolutionOperation()
		: IdealRatio( 0.2f )
	{
	}

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LightmapOptions )
	float IdealRatio;

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
