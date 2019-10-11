// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepOperation.h"
#include "DatasmithImportOptions.h"
#include "DatasmithUtils.h"

#include "DataprepTessellationOperation.generated.h"

namespace CADLibrary
{
	struct FImportParameters;
}
struct FMeshDescription;
class UCoreTechParametricSurfaceData;
class UDatasmithStaticMeshCADImportData;

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Datasmith Tessellation", ToolTip = "For each static mesh to process, retessellate the mesh if the object contains the required data") )
class UDataprepTessellationOperation : public UDataprepOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = TessellationOptions, meta = (ShowOnlyInnerProperties) )
	FDatasmithTessellationOptions TessellationSettings;

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
