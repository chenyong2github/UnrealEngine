// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImportTestFunctions/MaterialImportTestFunctions.h"
#include "InterchangeTestFunction.h"
#include "Materials/MaterialInterface.h"


UClass* UMaterialImportTestFunctions::GetAssociatedAssetType() const
{
	return UMaterialInterface::StaticClass();
}



FInterchangeTestFunctionResult UMaterialImportTestFunctions::CheckImportedMaterialCount(const TArray<UMaterialInterface*>& Materials, int32 ExpectedNumberOfImportedMaterials)
{
	FInterchangeTestFunctionResult Result;
	if (Materials.Num() != ExpectedNumberOfImportedMaterials)
	{
		Result.AddError(FString::Printf(TEXT("Expected %d materials, imported %d."), ExpectedNumberOfImportedMaterials, Materials.Num()));
	}

	return Result;
}
