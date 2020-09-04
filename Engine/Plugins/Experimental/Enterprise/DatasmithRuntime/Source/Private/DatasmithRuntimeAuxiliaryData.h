// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"

#include "DatasmithRuntimeAuxiliaryData.generated.h"

/** Asset user data that can be used with DatasmithRuntime on Actors and other objects  */
UCLASS(MinimalAPI)
class UDatasmithRuntimeAuxiliaryData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "DatasmithRuntime Internal")
	UObject* Auxiliary;
};
