// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImportTestFunctionsBase.h"
#include "TextureImportTestFunctions.generated.h"

struct FInterchangeTestFunctionResult;


UCLASS()
class INTERCHANGETESTS_API UTextureImportTestFunctions : public UImportTestFunctionsBase
{
	GENERATED_BODY()

public:

	// UImportTestFunctionsBase interface
	virtual UClass* GetAssociatedAssetType() const override;
};
