// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImportTestFunctionsBase.h"
#include "TextureImportTestFunctions.generated.h"

class UTexture;

struct FInterchangeTestFunctionResult;


UCLASS()
class INTERCHANGETESTS_API UTextureImportTestFunctions : public UImportTestFunctionsBase
{
	GENERATED_BODY()

public:

	// UImportTestFunctionsBase interface
	virtual UClass* GetAssociatedAssetType() const override;

	/** Check whether the expected number of textures are imported */
	UFUNCTION(Exec)
	static FInterchangeTestFunctionResult CheckImportedTextureCount(const TArray<UTexture*>& Textures, int32 ExpectedNumberOfImportedTextures);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "InterchangeTestFunction.h"
#endif
