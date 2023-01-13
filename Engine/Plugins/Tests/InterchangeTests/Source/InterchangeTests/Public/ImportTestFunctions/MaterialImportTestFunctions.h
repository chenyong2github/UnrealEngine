// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImportTestFunctionsBase.h"
#include "MaterialImportTestFunctions.generated.h"

class UMaterialInterface;

struct FInterchangeTestFunctionResult;


UCLASS()
class INTERCHANGETESTS_API UMaterialImportTestFunctions : public UImportTestFunctionsBase
{
	GENERATED_BODY()

public:

	// UImportTestFunctionsBase interface
	virtual UClass* GetAssociatedAssetType() const override;

	/** Check whether the expected number of materials are imported */
	UFUNCTION(Exec)
	static FInterchangeTestFunctionResult CheckImportedMaterialCount(const TArray<UMaterialInterface*>& MaterialInterfaces, int32 ExpectedNumberOfImportedMaterials);

	/** Check whether the expected number of material instances are imported */
	UFUNCTION(Exec)
	static FInterchangeTestFunctionResult CheckImportedMaterialInstanceCount(const TArray<UMaterialInterface*>& MaterialInterfaces, int32 ExpectedNumberOfImportedMaterialInstances);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "InterchangeTestFunction.h"
#endif
