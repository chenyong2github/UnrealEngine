// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImportTestFunctionsBase.h"
#include "InterchangeTestFunction.h"
#include "InterchangeResultImportTestFunctions.generated.h"

struct FInterchangeTestFunctionResult;
class UInterchangeResultsContainer;


UCLASS()
class INTERCHANGETESTS_API UInterchangeResultImportTestFunctions : public UImportTestFunctionsBase
{
	GENERATED_BODY()

public:

	// UImportTestFunctionsBase interface
	virtual UClass* GetAssociatedAssetType() const override;
};
