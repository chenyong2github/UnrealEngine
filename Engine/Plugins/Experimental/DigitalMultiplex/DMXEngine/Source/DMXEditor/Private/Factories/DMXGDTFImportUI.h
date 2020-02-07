// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "DMXGDTFImportUI.generated.h"

UCLASS(config = Editor, HideCategories=Object, MinimalAPI)
class UDMXGDTFImportUI 
    : public UObject
{
	GENERATED_BODY()

public:
    UDMXGDTFImportUI();

	void ResetToDefault();

public:
    UPROPERTY(EditAnywhere)
    bool bUseSubDirectory;

    UPROPERTY(EditAnywhere)
    bool bImportXML;

    UPROPERTY(EditAnywhere)
    bool bImportTextures;

    UPROPERTY(EditAnywhere)
    bool bImportModels;
};


